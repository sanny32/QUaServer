#ifndef QUADISCOVERYREGISTRATION_H
#define QUADISCOVERYREGISTRATION_H

#include <QObject>
#include <QTimer>

#include <QUaServer>

///
/// \brief Registers a running QUaServer at a discovery server through a client on the event loop of the server.
///
class QUaDiscoveryRegistration : public QObject
{
	Q_OBJECT

public:
	explicit QUaDiscoveryRegistration(QUaServer* server);
	~QUaDiscoveryRegistration();

	void start(const QString& discoveryServerUrl, int intervalMs);
	void stop(int timeoutMs);

signals:
	void finished(bool isOnline, const QUaStatusCode& statusCode);
#ifdef UA_ENABLE_ENCRYPTION
	void certificateRejected(const QByteArray& certificate, const QUaStatusCode& statusCode);
#endif

private:
	void sendRequest(bool isOnline);
	UA_StatusCode createClient();
	void sendRegisterService(UA_Client* client);
	void complete(UA_StatusCode statusCode);
	void finish();
	void abort();
	void renew();

	static void stateCallback(UA_Client* client, UA_SecureChannelState channelState,
		UA_SessionState sessionState, UA_StatusCode connectStatus);
	static void responseCallback(UA_Client* client, void* userdata, UA_UInt32 requestId, void* response);

	QUaServer*    _server;
	QTimer        _renewTimer;
	QString       _url;
	UA_Client*    _client;
	bool          _isOnline;
	bool          _register2;
	bool          _encrypted;
	bool          _requestSent;
	bool          _disconnecting;
	bool          _completed;
	UA_StatusCode _statusCode;
};

#endif // QUADISCOVERYREGISTRATION_H
