#include "quadiscoveryregistration.h"

#include <iterator>

#include <QDeadlineTimer>

#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel_async.h>

namespace {

constexpr UA_UInt32 kRequestTimeoutMs = 10000;

#ifdef UA_ENABLE_ENCRYPTION
///
/// \brief Adds the RSA security policies to \a config; open62541 would also try ECC ones, which an RSA certificate
///        does not suit, and warn on every registration.
///
UA_StatusCode addRsaSecurityPolicies(UA_ClientConfig* config, const UA_ByteString& certificate,
	const UA_ByteString& privateKey)
{
	using CreatePolicy = UA_StatusCode (*)(UA_SecurityPolicy*, const UA_ByteString, const UA_ByteString,
		const UA_Logger*);
	static const CreatePolicy creators[] = {
		&UA_SecurityPolicy_Basic256Sha256,
		&UA_SecurityPolicy_Aes256Sha256RsaPss,
		&UA_SecurityPolicy_Aes128Sha256RsaOaep,
	};
	const size_t size = config->securityPoliciesSize + std::size(creators);
	auto policies = static_cast<UA_SecurityPolicy*>(
		UA_realloc(config->securityPolicies, size * sizeof(UA_SecurityPolicy)));
	if (!policies)
	{
		return UA_STATUSCODE_BADOUTOFMEMORY;
	}
	config->securityPolicies = policies;
	for (CreatePolicy create : creators)
	{
		const UA_StatusCode st = create(&policies[config->securityPoliciesSize], certificate, privateKey,
			config->logging);
		if (st != UA_STATUSCODE_GOOD)
		{
			return st;
		}
		config->securityPoliciesSize++;
	}
	return UA_STATUSCODE_GOOD;
}

///
/// \brief Returns the discovery server certificates the trust list of \a client rejected.
///
QList<QByteArray> rejectedCertificates(UA_Client* client)
{
	QList<QByteArray> certificates;
	UA_CertificateGroup* group = &UA_Client_getConfig(client)->certificateVerification;
	UA_ByteString* list = nullptr;
	size_t listSize = 0;
	if (!group->getRejectedList || group->getRejectedList(group, &list, &listSize) != UA_STATUSCODE_GOOD)
	{
		return certificates;
	}
	for (size_t i = 0; i < listSize; i++)
	{
		certificates << QByteArray(reinterpret_cast<const char*>(list[i].data), static_cast<qsizetype>(list[i].length));
	}
	UA_Array_delete(list, listSize, &UA_TYPES[UA_TYPES_BYTESTRING]);
	return certificates;
}
#endif // UA_ENABLE_ENCRYPTION

} // namespace

///
/// \brief Creates an idle registration for \a server; start() begins registering.
///
QUaDiscoveryRegistration::QUaDiscoveryRegistration(QUaServer* server)
	: _server(server)
	, _client(nullptr)
	, _isOnline(true)
	, _register2(true)
	, _encrypted(false)
	, _requestSent(false)
	, _disconnecting(false)
	, _completed(false)
	, _statusCode(UA_STATUSCODE_GOOD)
{
	QObject::connect(&_renewTimer, &QTimer::timeout, this, &QUaDiscoveryRegistration::renew);
}

///
/// \brief Drops a registration still under way without reporting it.
///
QUaDiscoveryRegistration::~QUaDiscoveryRegistration()
{
	this->abort();
}

///
/// \brief Registers the running server at \a discoveryServerUrl now and again every \a intervalMs.
///
void QUaDiscoveryRegistration::start(const QString& discoveryServerUrl, int intervalMs)
{
	_renewTimer.stop();
	this->abort();
	_url = discoveryServerUrl;
	this->sendRequest(true);
	_renewTimer.start(intervalMs);
}

///
/// \brief Stops renewing and unregisters the server, running the event loop of the server for at most
///        \a timeoutMs until the discovery server answers. Must be called before the server shuts down.
///
void QUaDiscoveryRegistration::stop(int timeoutMs)
{
	_renewTimer.stop();
	if (_url.isEmpty())
	{
		return;
	}
	this->abort();
	this->sendRequest(false);
	UA_EventLoop* eventLoop = UA_Server_getConfig(_server->_server)->eventLoop;
	const QDeadlineTimer deadline(timeoutMs);
	while (_client && !_completed && !deadline.hasExpired())
	{
		eventLoop->run(eventLoop, QUA_ITERATE_TIMEOUT_MS);
	}
	if (_completed)
	{
		this->finish();
	}
	else
	{
		this->abort();
		emit this->finished(false, UA_STATUSCODE_BADTIMEOUT);
	}
	_url.clear();
}

///
/// \brief Connects to the discovery server to register the server (\a isOnline) or to unregister it.
///        Does nothing while the previous request is still under way.
///
void QUaDiscoveryRegistration::sendRequest(bool isOnline)
{
	if (_client)
	{
		return;
	}
	_isOnline      = isOnline;
	_register2     = true;
	_requestSent   = false;
	_disconnecting = false;
	_completed     = false;
	UA_StatusCode st = this->createClient();
	if (st == UA_STATUSCODE_GOOD)
	{
		st = UA_Client_connectSecureChannelAsync(_client, _url.toUtf8().constData());
	}
	if (st != UA_STATUSCODE_GOOD)
	{
		this->complete(st);
	}
}

///
/// \brief Creates the client that opens a secure channel, without a session, to the discovery server. It runs
///        on the event loop of the server and, when the server has a certificate and a private key, encrypts
///        with them and checks the discovery server against the trusted certificates of the server.
/// \return Good, or the reason the client could not be configured.
///
UA_StatusCode QUaDiscoveryRegistration::createClient()
{
	UA_ServerConfig* serverConfig = UA_Server_getConfig(_server->_server);
	UA_ClientConfig config;
	memset(&config, 0, sizeof(UA_ClientConfig));
	// the server logger has no clear callback, so deleting the client keeps it
	config.logging           = &_server->_logger;
	config.eventLoop         = serverConfig->eventLoop;
	config.externalEventLoop = true;
	UA_StatusCode st = UA_ClientConfig_setDefault(&config);
	_encrypted = false;
#ifdef UA_ENABLE_ENCRYPTION
	if (st == UA_STATUSCODE_GOOD && !_server->_byteCertificate.isEmpty() && !_server->_bytePrivateKey.isEmpty())
	{
		_encrypted = true;
		const QVector<UA_ByteString> material =
			QUaServer::toByteStringArray({ _server->_byteCertificate, _server->_bytePrivateKey });
		st = addRsaSecurityPolicies(&config, material.at(0), material.at(1));
		if (st == UA_STATUSCODE_GOOD)
		{
			st = _server->applyTrustList(&config.certificateVerification,
				UA_NS0ID(SERVERCONFIGURATION_CERTIFICATEGROUPS_DEFAULTAPPLICATIONGROUP), config.logging);
		}
	}
#endif // UA_ENABLE_ENCRYPTION
	if (st != UA_STATUSCODE_GOOD)
	{
		UA_ClientConfig_clear(&config);
		return st;
	}
	config.securityMode = _encrypted ? UA_MESSAGESECURITYMODE_SIGNANDENCRYPT : UA_MESSAGESECURITYMODE_NONE;
	// the application URI must match the one in the certificate of the server
	UA_String_clear(&config.clientDescription.applicationUri);
	UA_String_copy(&serverConfig->applicationDescription.applicationUri, &config.clientDescription.applicationUri);
	config.timeout       = kRequestTimeoutMs;
	config.stateCallback = &QUaDiscoveryRegistration::stateCallback;
	config.clientContext = this;
	_client = UA_Client_newWithConfig(&config);
	if (!_client)
	{
		UA_ClientConfig_clear(&config);
		return UA_STATUSCODE_BADOUTOFMEMORY;
	}
	return UA_STATUSCODE_GOOD;
}

///
/// \brief Calls RegisterServer2, or RegisterServer after RegisterServer2 failed, with the description of the server.
///
void QUaDiscoveryRegistration::sendRegisterService(UA_Client* client)
{
	const UA_ApplicationDescription& description = UA_Server_getConfig(_server->_server)->applicationDescription;
	// borrows the strings of the server config; the request is encoded before the call returns
	UA_RegisteredServer server;
	UA_RegisteredServer_init(&server);
	server.serverUri          = description.applicationUri;
	server.productUri         = description.productUri;
	server.serverNames        = const_cast<UA_LocalizedText*>(&description.applicationName);
	server.serverNamesSize    = 1;
	server.serverType         = description.applicationType;
	server.gatewayServerUri   = description.gatewayServerUri;
	server.discoveryUrls      = description.discoveryUrls;
	server.discoveryUrlsSize  = description.discoveryUrlsSize;
	server.isOnline           = _isOnline;
	UA_StatusCode st;
	if (_register2)
	{
		UA_RegisterServer2Request request;
		UA_RegisterServer2Request_init(&request);
		request.server = server;
		st = __UA_Client_AsyncService(client, &request, &UA_TYPES[UA_TYPES_REGISTERSERVER2REQUEST],
			&QUaDiscoveryRegistration::responseCallback, &UA_TYPES[UA_TYPES_REGISTERSERVER2RESPONSE], this, nullptr);
	}
	else
	{
		UA_RegisterServerRequest request;
		UA_RegisterServerRequest_init(&request);
		request.server = server;
		st = __UA_Client_AsyncService(client, &request, &UA_TYPES[UA_TYPES_REGISTERSERVERREQUEST],
			&QUaDiscoveryRegistration::responseCallback, &UA_TYPES[UA_TYPES_REGISTERSERVERRESPONSE], this, nullptr);
	}
	_requestSent = true;
	if (st != UA_STATUSCODE_GOOD)
	{
		_statusCode    = st;
		_disconnecting = true;
		UA_Client_disconnectSecureChannelAsync(client);
	}
}

///
/// \brief Records the result of the request; finish() reports it once the event loop has returned.
///
void QUaDiscoveryRegistration::complete(UA_StatusCode statusCode)
{
	if (_completed)
	{
		return;
	}
	_statusCode = statusCode;
	_completed  = true;
	QMetaObject::invokeMethod(this, &QUaDiscoveryRegistration::finish, Qt::QueuedConnection);
}

///
/// \brief Deletes the client of a completed request and reports the discovery server certificates it rejected,
///        then the result.
///
void QUaDiscoveryRegistration::finish()
{
	if (!_completed)
	{
		return;
	}
	_completed = false;
	UA_Client* client = _client;
	_client = nullptr;
#ifdef UA_ENABLE_ENCRYPTION
	QList<QByteArray> rejected;
#endif
	if (client)
	{
#ifdef UA_ENABLE_ENCRYPTION
		rejected = rejectedCertificates(client);
#endif
		UA_Client_delete(client);
	}
#ifdef UA_ENABLE_ENCRYPTION
	for (const QByteArray& certificate : rejected)
	{
		emit this->certificateRejected(certificate, _statusCode);
	}
#endif
	emit this->finished(_isOnline, _statusCode);
}

///
/// \brief Deletes the client of the request under way without reporting a result.
///
void QUaDiscoveryRegistration::abort()
{
	_completed = false;
	UA_Client* client = _client;
	// callbacks fired while the client is deleted no longer match _client and are ignored
	_client = nullptr;
	if (client)
	{
		UA_Client_delete(client);
	}
}

///
/// \brief Renews the registration before the discovery server drops it.
///
void QUaDiscoveryRegistration::renew()
{
	this->sendRequest(true);
}

///
/// \brief Sends the register request once the secure channel is open with the requested security, and completes
///        the request when the connection fails or closes after the response.
///
void QUaDiscoveryRegistration::stateCallback(UA_Client* client, UA_SecureChannelState channelState,
	UA_SessionState sessionState, UA_StatusCode connectStatus)
{
	Q_UNUSED(sessionState);
	auto self = static_cast<QUaDiscoveryRegistration*>(UA_Client_getContext(client));
	if (!self || client != self->_client || self->_completed)
	{
		return;
	}
	if (channelState == UA_SECURECHANNELSTATE_CLOSED)
	{
		if (self->_disconnecting)
		{
			self->complete(self->_statusCode);
		}
		// a Good close is the client switching to the URL from FindServers or to the selected endpoint
		else if (connectStatus != UA_STATUSCODE_GOOD)
		{
			self->complete(connectStatus);
		}
		return;
	}
	if (connectStatus != UA_STATUSCODE_GOOD || channelState != UA_SECURECHANNELSTATE_OPEN || self->_requestSent)
	{
		return;
	}
	if (self->_encrypted)
	{
		UA_MessageSecurityMode mode = UA_MESSAGESECURITYMODE_INVALID;
		UA_Client_getConnectionAttribute_scalar(client, UA_QUALIFIEDNAME(0, const_cast<char*>("securityMode")),
			&UA_TYPES[UA_TYPES_MESSAGESECURITYMODE], &mode);
		if (mode != UA_MESSAGESECURITYMODE_SIGNANDENCRYPT)
		{
			return;
		}
	}
	self->sendRegisterService(client);
}

///
/// \brief Falls back from RegisterServer2 to RegisterServer once, then closes the channel with the result.
///
void QUaDiscoveryRegistration::responseCallback(UA_Client* client, void* userdata, UA_UInt32 requestId,
	void* response)
{
	Q_UNUSED(requestId);
	auto self = static_cast<QUaDiscoveryRegistration*>(userdata);
	if (client != self->_client || self->_completed || self->_disconnecting)
	{
		return;
	}
	// both response types start with the response header
	const UA_StatusCode st = static_cast<const UA_ResponseHeader*>(response)->serviceResult;
	if (st == UA_STATUSCODE_BADSECURECHANNELCLOSED)
	{
		// the client closed the channel to reconnect; the request goes again on the next one
		self->_requestSent = false;
		return;
	}
	if (st != UA_STATUSCODE_GOOD && self->_register2)
	{
		self->_register2 = false;
		UA_SecureChannelState channelState = UA_SECURECHANNELSTATE_CLOSED;
		UA_Client_getState(client, &channelState, nullptr, nullptr);
		if (channelState == UA_SECURECHANNELSTATE_OPEN)
		{
			self->sendRegisterService(client);
			return;
		}
	}
	self->_statusCode    = st;
	self->_disconnecting = true;
	UA_Client_disconnectSecureChannelAsync(client);
}
