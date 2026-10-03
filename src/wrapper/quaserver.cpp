// socket api to resolve the address of connected clients
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using quaSocket = SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
using quaSocket = int;
#endif

#include "quaserver_anex.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
#include <QUaBaseEvent>
#include <QUaGeneralModelChangeEvent>
#include <QUaSystemEvent>
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
#include <QUaConditionVariable>
#include <QUaStateVariable>
#include <QUaTwoStateVariable>
#include <QUaFiniteStateVariable>
#include <QUaTransitionVariable>
#include <QUaFiniteTransitionVariable>
#include <QUaCondition>
#include <QUaAcknowledgeableCondition>
#include <QUaAlarmCondition>
#include <QUaStateMachine>
#include <QUaFiniteStateMachine>
#include <QUaState>
#include <QUaTransition>
#include <QUaExclusiveLimitStateMachine>
#include <QUaDiscreteAlarm>
#include <QUaOffNormalAlarm>
#include <QUaLimitAlarm>
#include <QUaExclusiveLimitAlarm>
#include <QUaExclusiveLevelAlarm>
#include <QUaRefreshStartEvent>
#include <QUaRefreshEndEvent>
#include <QUaRefreshRequiredEvent>
#include <QUaTransitionEvent>
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
#include <QUaOptionSetVariable>
#endif

#include <QMetaProperty>
#include <QTimer>

#ifdef UA_ENABLE_ENCRYPTION
namespace {

///
/// \brief Maps an OPC UA security policy URI to its QUaSecurityPolicy flag.
/// \return The flag, or an empty set for a policy QUaServer does not know.
///
QUaSecurityPolicies securityPolicyFromUri(const UA_String& policyUri)
{
	static const QHash<QByteArray, QUaSecurityPolicy> policies = {
		{ "http://opcfoundation.org/UA/SecurityPolicy#None"                    , QUaSecurityPolicy::None                  },
		{ "http://opcfoundation.org/UA/SecurityPolicy#Basic128Rsa15"           , QUaSecurityPolicy::Basic128Rsa15         },
		{ "http://opcfoundation.org/UA/SecurityPolicy#Basic256"                , QUaSecurityPolicy::Basic256              },
		{ "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256"          , QUaSecurityPolicy::Basic256Sha256        },
		{ "http://opcfoundation.org/UA/SecurityPolicy#Aes128_Sha256_RsaOaep"   , QUaSecurityPolicy::Aes128Sha256RsaOaep   },
		{ "http://opcfoundation.org/UA/SecurityPolicy#Aes256_Sha256_RsaPss"    , QUaSecurityPolicy::Aes256Sha256RsaPss    },
		{ "http://opcfoundation.org/UA/SecurityPolicy#ECC_nistP256_AesGcm"     , QUaSecurityPolicy::EccNistP256AesGcm     },
		{ "http://opcfoundation.org/UA/SecurityPolicy#ECC_nistP256_ChaChaPoly" , QUaSecurityPolicy::EccNistP256ChaChaPoly },
		{ "http://opcfoundation.org/UA/SecurityPolicy#ECC_curve25519"          , QUaSecurityPolicy::EccCurve25519         },
		{ "http://opcfoundation.org/UA/SecurityPolicy#ECC_curve448"            , QUaSecurityPolicy::EccCurve448           }
	};
	const QByteArray uri(reinterpret_cast<const char*>(policyUri.data), static_cast<qsizetype>(policyUri.length));
	const auto it = policies.constFind(uri);
	return it == policies.constEnd() ? QUaSecurityPolicies() : QUaSecurityPolicies(it.value());
}

///
/// \brief Maps an open62541 message security mode to its QUaMessageSecurityMode flag.
///
QUaMessageSecurityModes securityModeFromUa(const UA_MessageSecurityMode mode)
{
	switch (mode)
	{
	case UA_MESSAGESECURITYMODE_NONE:
		return QUaMessageSecurityMode::None;
	case UA_MESSAGESECURITYMODE_SIGN:
		return QUaMessageSecurityMode::Sign;
	case UA_MESSAGESECURITYMODE_SIGNANDENCRYPT:
		return QUaMessageSecurityMode::SignAndEncrypt;
	default:
		return QUaMessageSecurityModes();
	}
}

} // namespace
#endif // UA_ENABLE_ENCRYPTION

/* helper null log for avoiding startup messages */
void UA_Log_Discard_log(void *context,
                        UA_LogLevel level,
                        UA_LogCategory category,
                        const char *msg,
                        va_list args)
{
    // do nothing
    Q_UNUSED(context);
    Q_UNUSED(level);
    Q_UNUSED(category);
    Q_UNUSED(msg);
    Q_UNUSED(args);
};


UA_StatusCode QUaServer::uaConstructor(UA_Server       * server, 
	                                   const UA_NodeId * sessionId, 
	                                   void            * sessionContext, 
	                                   const UA_NodeId * typeNodeId, 
	                                   void            * typeNodeContext, 
	                                   const UA_NodeId * nodeId, 
	                                   void            ** nodeContext)
{
	Q_UNUSED(server);
	Q_UNUSED(sessionContext); 
	// get server from context object
#ifdef QT_DEBUG 
	auto srv = qobject_cast<QUaServer*>(static_cast<QObject*>(typeNodeContext));
	Q_CHECK_PTR(srv);
#else
	auto srv = static_cast<QUaServer*>(typeNodeContext);
#endif // QT_DEBUG 
	if (!srv)
	{
		return (UA_StatusCode)UA_STATUSCODE_BADUNEXPECTEDERROR;
	}
	// check session (objects can be created or destroyed without client connected)
	//Q_ASSERT(srv->_hashSessions.contains(*sessionId));
	srv->_currentSession = srv->_hashSessions.contains(*sessionId) ?
		srv->_hashSessions[*sessionId] : nullptr;
	// get method from type constructors map and call it
	Q_ASSERT(srv->_hashConstructors.contains(*typeNodeId));
	auto st = srv->_hashConstructors[*typeNodeId](nodeId, nodeContext);
	// emit new instance signal if appropriate
	if (srv->_hashSignalers.contains(*typeNodeId))
	{
		auto signaler = srv->_hashSignalers.value(*typeNodeId);
		auto newInstance = QUaNode::fromVoidContext(*nodeContext);
		if (newInstance)
		{
			emit signaler->signalNewInstance(newInstance);
		}
	}
	return st;
}

void QUaServer::uaDestructor(UA_Server       * server, 
	                         const UA_NodeId * sessionId, 
	                         void            * sessionContext, 
	                         const UA_NodeId * typeNodeId, 
	                         void            * typeNodeContext, 
	                         const UA_NodeId * nodeId, 
	                         void            ** nodeContext)
{
	Q_UNUSED(nodeContext);
	Q_UNUSED(typeNodeContext);
	Q_UNUSED(typeNodeId);
	Q_UNUSED(sessionContext);
	// get server
	void* serverContext = nullptr;
	auto st = UA_Server_getNodeContext(server, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER), &serverContext);
	// UA_Server_delete may remove the Server node first; ~QUaServer already deleted the C++ nodes
	if (st != UA_STATUSCODE_GOOD || !serverContext)
	{
		return;
	}
#ifdef QT_DEBUG 
	auto srv = qobject_cast<QUaServer*>(static_cast<QObject*>(serverContext));
	Q_CHECK_PTR(srv);
#else
	auto srv = static_cast<QUaServer*>(serverContext);
#endif // QT_DEBUG 
	// check session (objects can be created or destroyed without client connected)
	//Q_ASSERT(srv->_hashSessions.contains(*sessionId));
	srv->_currentSession = srv->_hashSessions.contains(*sessionId) ?
		srv->_hashSessions[*sessionId] : nullptr;
	// get node
	void * context;
	st = UA_Server_getNodeContext(server, *nodeId, &context);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// try to convert to node (NOTE : nullptr is triggered by ~QUaNode)
	auto node = QUaNode::fromVoidContext(context);
	// early exit if not convertible (this call was triggered by ~QUaNode)
	if (!node)
	{
		return;
	}
	// handle events if enabled
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	// check if event (not in tree)
	auto evt = qobject_cast<QUaBaseEvent*>(node);
	if (evt)
	{
		evt->deleteLater();
		return;
	}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
	// if convertible could mean:
	// 1) this is a child of a node being deleted programatically 
	//    this one does not require to call C++ delete because Qt will take care of it
	// 2) this is a node being deleted from the network
	//    this one requires C++ delete
	// we can differentiate them because parent of 1) would not have a context
	// in which case we set current child context to nullptr to continue pattern and return
	UA_NodeId parentNodeId = QUaNode::getParentNodeId(*nodeId, server);
	Q_ASSERT(!UA_NodeId_equal(&parentNodeId, &UA_NODEID_NULL));
	void * parentContext;
	st = UA_Server_getNodeContext(server, parentNodeId, &parentContext);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	auto parentNode = QUaNode::fromVoidContext(parentContext);
	if (!parentNode)
	{
		st = UA_Server_setNodeContext(server, *nodeId, nullptr);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		UA_NodeId_clear(&parentNodeId);
		return;
	}
	// if we reach here it means case 2), so deleteLater (when nodeId not in store anymore)
	// so we avoid calling UA_Server_deleteNode in ~QUaNode
	node->deleteLater();
	Q_UNUSED(st);
	UA_NodeId_clear(&parentNodeId);
}

// try to make instance declaration nodeIds a little more predictable
UA_StatusCode QUaServer::generateChildNodeId(
	UA_Server       *server, 
	const UA_NodeId *sessionId, 
	void            *sessionContext, 
	const UA_NodeId *sourceNodeId, 
	const UA_NodeId *targetParentNodeId, 
	const UA_NodeId *referenceTypeId, 
	UA_NodeId       *targetNodeId)
{
	Q_UNUSED(sessionId);
	Q_UNUSED(sessionContext);
	Q_UNUSED(referenceTypeId);
	// read browse name
	UA_QualifiedName outBrowseName;
	auto st = UA_Server_readBrowseName(server, *sourceNodeId, &outBrowseName);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	auto qserver = QUaServer::getServerNodeContext(server);
	// if parent has string node id try to add child also as string
	if (qserver->_childNodeIdCallback)
	{
		// get child browse name
		QUaQualifiedName childBrowseName = QUaQualifiedName(outBrowseName);		
		// get parent node id
		QUaNodeId parentNodeId = QUaNodeId(*targetParentNodeId);
		// call callback
		QUaNodeId childNodeId = qserver->_childNodeIdCallback(parentNodeId, childBrowseName);	
		bool isNull = childNodeId.isNull();
		bool isUsed = false;
		if (!isNull)
		{
			isUsed = qserver->isNodeIdUsed(childNodeId);
		}		
		// copy if success
		if (!isNull && !isUsed)
		{
			// copy
			*targetNodeId = childNodeId;
			// cleanup
			UA_QualifiedName_clear(&outBrowseName);
			return UA_STATUSCODE_GOOD;
		}
		// log errors
		if (isNull)
		{
			emit qserver->logMessage({
				tr("Child %1 nodeId for parent %2 is null. Assigning random nodeId.")
				.arg(childBrowseName.name()).arg(parentNodeId),
				QUaLogLevel::Error,
				QUaLogCategory::UserLand
			});
		}
		// log errors
		if (isUsed)
		{
			emit qserver->logMessage({
				tr("Child %1 nodeId for parent %2 is already in use. Assigning random nodeId.")
				.arg(childBrowseName.name()).arg(parentNodeId),
				QUaLogLevel::Error,
				QUaLogCategory::UserLand
			});
		}
	}
	// get parent node id hash
	size_t parentHash = qHash(*targetParentNodeId, targetParentNodeId->namespaceIndex);
	// get child browse name hash
	size_t childNameHash = qHash(
		QByteArray::fromRawData((const char*)outBrowseName.name.data, static_cast<int>(outBrowseName.name.length)),
			outBrowseName.namespaceIndex
		);
	// new node id is combination of both
	targetNodeId->identifierType = UA_NODEIDTYPE_NUMERIC;
	targetNodeId->identifier.numeric = parentHash ^ childNameHash;
	// cleanup
	UA_QualifiedName_clear(&outBrowseName);
	// check 	
	while (qserver->isNodeIdUsed(*targetNodeId))
	{
		targetNodeId->identifier.numeric = targetNodeId->identifier.numeric ^ childNameHash;
	}
	return UA_STATUSCODE_GOOD;
}

UA_StatusCode QUaServer::uaConstructor(
	QUaServer         *server,
	const UA_NodeId   *nodeId, 
	void             **nodeContext,
	const QMetaObject &metaObject
)
{
	// get parent node id
	UA_NodeId topBoundParentNodeId = QUaNode::getParentNodeId(*nodeId, server->_server);
	// find top level node which is bound (bound := NodeId context == QUaNode instance)
	UA_NodeClass outNodeClass;
	QUaNode * parentContext = nullptr;
	// NOTE : not only events can be parent-less (e.g. conditions)
	while (!UA_NodeId_isNull(&topBoundParentNodeId))
	{
		// handle events
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
		// check if event
		if (metaObject.inherits(&QUaBaseEvent::staticMetaObject)
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
			&& !metaObject.inherits(&QUaCondition::staticMetaObject)
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
			)
		{
			break;
		}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
		// ignore if new instance is due to new type registration
		UA_Server_readNodeClass(server->_server, topBoundParentNodeId, &outNodeClass);
		Q_ASSERT_X(outNodeClass != UA_NODECLASS_UNSPECIFIED, "QUaServer::uaConstructor", 
			"Something went wrong while getting parent info.");
		if(outNodeClass != UA_NODECLASS_OBJECT && outNodeClass != UA_NODECLASS_VARIABLE)
		{
			UA_NodeId_clear(&topBoundParentNodeId);
			return UA_STATUSCODE_GOOD;
		}
		parentContext = QUaNode::getNodeContext(topBoundParentNodeId, server->_server);
		// check if parent node is bound
		if (parentContext)
		{
			break;
		}
		// try next parent in hierarchy
		auto tmpParentNodeId = QUaNode::getParentNodeId(topBoundParentNodeId, server->_server);
		UA_NodeId_clear(&topBoundParentNodeId); // clear old
		topBoundParentNodeId = tmpParentNodeId; // shallow copy
	}
	// create new instance (and bind it to UA, in base types happens in constructor, 
	// in derived class is done by QOpcUaServerNodeFactory)
	Q_ASSERT_X(metaObject.constructorCount() > 0, "QUaServer::uaConstructor", 
		"Failed instantiation. No matching Q_INVOKABLE constructor with signature "
		"CONSTRUCTOR(QUaServer *server) found.");
	// NOTE : to simplify user API, we minimize QUaNode arguments to just a QUaServer 
	// reference we temporarily store in the QUaServer reference the UA_NodeId and 
	// QMetaObject values needed to instantiate the new node.
	server->_newNodeNodeId     = nodeId;
	server->_newNodeMetaObject = &metaObject;
	// instantiate new C++ node, _newNodeNodeId and _newNodeMetaObject only meant to be used during this call
	auto * pQObject = metaObject.newInstance(Q_ARG(QUaServer*, server));
	Q_ASSERT_X(pQObject, "QUaServer::uaConstructor", 
		"Failed instantiation. No matching Q_INVOKABLE constructor with signature "
		"CONSTRUCTOR(QUaServer *server) found.");
	auto* newInstance = qobject_cast<QUaNode*>(pQObject);
	Q_CHECK_PTR(newInstance);
	if (!newInstance)
	{
		UA_NodeId_clear(&topBoundParentNodeId);
		return (UA_StatusCode)UA_STATUSCODE_BADUNEXPECTEDERROR;
	}
	// need to bind again using the official (void ** nodeContext) of the UA constructor
	// because we set context on C++ instantiation, but later the UA library overwrites it 
	// after calling the UA constructor
	*nodeContext = static_cast<void*>(newInstance);
	UA_NodeId_clear(&newInstance->_nodeId);
	UA_NodeId_copy(nodeId, &newInstance->_nodeId);
	// need to set parent if direct parent is already bound bacause its constructor has already been called
	UA_NodeId directParentNodeId = QUaNode::getParentNodeId(*nodeId, server->_server);
	if (parentContext && UA_NodeId_equal(&topBoundParentNodeId, &directParentNodeId))
	{
		auto browseName = QUaNode::getBrowseName(*nodeId, server->_server);
		newInstance->setParent(parentContext);
		newInstance->setObjectName(browseName);
		Q_ASSERT(!parentContext->browseChild(browseName));
		size_t key = qHash(browseName);
		parentContext->_browseCache[key] = newInstance;
		QObject::connect(newInstance, &QObject::destroyed, parentContext, [parentContext, key]() {
			parentContext->_browseCache.remove(key);
		});	
		// emit child added to parent
		emit parentContext->childAdded(newInstance);
	}
	// success
	UA_NodeId_clear(&topBoundParentNodeId);
	UA_NodeId_clear(&directParentNodeId);
	return UA_STATUSCODE_GOOD;
}

// [STATIC]
UA_StatusCode QUaServer::methodCallback(
	UA_Server        *server,
	const UA_NodeId  *sessionId, 
	void             *sessionContext, 
	const UA_NodeId  *methodId, 
	void             *methodContext, 
	const UA_NodeId  *objectId, 
	void             *objectContext, 
	size_t            inputSize, 
	const UA_Variant *input, 
	size_t            outputSize, 
	UA_Variant       *output
)
{
	Q_UNUSED(server        );
	Q_UNUSED(sessionContext);
	Q_UNUSED(objectId      );
	Q_UNUSED(inputSize     );
	Q_UNUSED(outputSize    );
	// get node from context object
#ifdef QT_DEBUG 
	auto srv = qobject_cast<QUaServer*>(static_cast<QObject*>(methodContext));
	Q_CHECK_PTR(srv);
#else
	auto srv = static_cast<QUaServer*>(methodContext);
#endif // QT_DEBUG 
	if (!srv)
	{
		return (UA_StatusCode)UA_STATUSCODE_BADUNEXPECTEDERROR;
	}
	// check session
	Q_ASSERT(srv->_hashSessions.contains(*sessionId));
	srv->_currentSession = srv->_hashSessions.contains(*sessionId) ?
		srv->_hashSessions[*sessionId] : nullptr;
	// check method
	Q_ASSERT(srv->_hashMethods.contains(*methodId));
	if (!srv->_hashMethods.contains(*methodId))
	{
		return (UA_StatusCode)UA_STATUSCODE_BADINTERNALERROR;
	}
	// get method from node callbacks map and call it
	return srv->_hashMethods[*methodId](objectContext, input, output);
}

UA_StatusCode QUaServer::callMetaMethod(
	QUaServer         *server, 
	QUaBaseObject     *object, 
	const QMetaMethod &metaMethod, 
	const UA_Variant  *input, 
	UA_Variant        *output)
{
	// convert input arguments to QVariants
	QVariantList varListArgs;
	QList<QGenericArgument> genListArgs;
	auto listTypeNames = metaMethod.parameterTypes();
	for (int k = 0; k < metaMethod.parameterCount(); k++)
	{
		QVariant varArg;
		auto metaType    = (QMetaType::Type)metaMethod.parameterType(k);
		auto strTypeName = QString::fromUtf8(listTypeNames.at(k));
		// NOTE : enums are QMetaType::UnknownType
		Q_ASSERT_X(metaType != QMetaType::UnknownType ||
			server->_hashEnums.contains(strTypeName),
			"QUaServer::callMetaMethod",
			"Argument type is not registered. Try using qRegisterMetaType.");
		if (metaType == QMetaType::UnknownType &&
			!server->_hashEnums.contains(strTypeName))
		{
			return (UA_StatusCode)UA_STATUSCODE_BADINTERNALERROR;
		}
		if (strTypeName.contains(QLatin1String("QList"), Qt::CaseInsensitive))
		{
			varArg = QUaTypesConverter::uaVariantToQVariantArray(input[k],
				QUaTypesConverter::ArrayType::QList);
		}
		else if (strTypeName.contains(QLatin1String("QVector"), Qt::CaseInsensitive))
		{
			varArg = QUaTypesConverter::uaVariantToQVariantArray(input[k],
				QUaTypesConverter::ArrayType::QVector);
		}
		else
		{
			varArg = QUaTypesConverter::uaVariantToQVariant(input[k]);
		}
		// NOTE : need to keep in stack while method is being called
		varListArgs.append(varArg);
		// create generic argument only with reference to data
		genListArgs.append(QGenericArgument(
			QMetaType( varListArgs[k].userType() ).name(),
			const_cast<void*>(varListArgs[k].constData())
		));
	}
	// create return QVariant
	int retType = metaMethod.returnType();
	// NOTE : enums are QMetaType::UnknownType
	Q_ASSERT_X(retType != QMetaType::UnknownType ||
		server->_hashEnums.contains( QString::fromUtf8(metaMethod.typeName()) ),
		"QUaServer::callMetaMethod",
		"Return type is not registered. Try using qRegisterMetaType."
	);
	if (retType == QMetaType::UnknownType)
	{
		return (UA_StatusCode)UA_STATUSCODE_BADINTERNALERROR;
	}
	QVariant returnValue;
	// avoid Qt printing warning to console
	if (retType != QMetaType::Void)
	{
		returnValue = QVariant( QMetaType(retType) );
	}
	QGenericReturnArgument returnArgument(
		metaMethod.typeName(),
		const_cast<void*>(returnValue.constData())
	);
	// reset status code
	server->_methodRetStatusCode = UA_STATUSCODE_GOOD;
	// call metaMethod
	bool ok = metaMethod.invoke(
		object,
		Qt::DirectConnection,
		returnArgument,
		genListArgs.value(0),
		genListArgs.value(1),
		genListArgs.value(2),
		genListArgs.value(3),
		genListArgs.value(4),
		genListArgs.value(5),
		genListArgs.value(6),
		genListArgs.value(7),
		genListArgs.value(8),
		genListArgs.value(9)
	);
	Q_ASSERT(ok);
	Q_UNUSED(ok);
	// set return value if any
	if (retType != QMetaType::Void)
	{
		UA_Variant tmpVar = QUaTypesConverter::uaVariantFromQVariant(returnValue);
		// TODO : have to cleanup? UA_Variant_deleteMembers(&tmpVar)
		*output = tmpVar;
	}
	// copy return status code
	UA_StatusCode retStatusCode = server->_methodRetStatusCode;
	// reset status code
	server->_methodRetStatusCode = UA_STATUSCODE_GOOD;
	// return success status
	return retStatusCode;
}

void QUaServer::bindMethod(
	QUaServer       *server, 
	const UA_NodeId *methodNodeId, 
	const int       &metaMethodIndex)
{
	// register callback in open62541
	auto st = UA_Server_setMethodNodeCallback(server->_server, *methodNodeId, &QUaServer::methodCallback);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// set QUaServer* instance as method context
	st = UA_Server_setNodeContext(
		server->_server,
		*methodNodeId,
		static_cast<void*>(server)
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	Q_ASSERT_X(!server->_hashMethods.contains(*methodNodeId), "QUaServer::registerTypeDefaults",
		"Cannot register same method more than once.");
	if (server->_hashMethods.contains(*methodNodeId))
	{
		return;
	}
	// NOTE : had to cache the method's index in lambda capture because caching 
	//        metaMethod directly was not working in some cases the internal data
	//        of the metaMethod was deleted which resulted in access violation
	server->_hashMethods[*methodNodeId] = [metaMethodIndex, server](
		void* objectContext,
		const UA_Variant* input,
		UA_Variant* output)
	{
		// get object instance that owns method
#ifdef QT_DEBUG 
		QUaBaseObject* object = qobject_cast<QUaBaseObject*>(static_cast<QObject*>(objectContext));
		Q_ASSERT_X(object,
			"QUaServer::bindMethod",
			"Cannot call method on invalid C++ object.");
#else
		QUaBaseObject* object = static_cast<QUaBaseObject*>(objectContext);
#endif // QT_DEBUG 
		if (!object)
		{
			return (UA_StatusCode)UA_STATUSCODE_BADUNEXPECTEDERROR;
		}
		// get meta method
		auto metaMethod = object->metaObject()->method(metaMethodIndex);
		return QUaServer::callMetaMethod(server, object, metaMethod, input, output);
	};
}

QHash<QUaQualifiedName, int> QUaServer::metaMethodIndexes(const QMetaObject& metaObject)
{
	QHash<QUaQualifiedName, int> retHash;
	for (int methIdx = metaObject.methodOffset(); methIdx < metaObject.methodCount(); methIdx++)
	{
		QMetaMethod metaMethod = metaObject.method(methIdx);
		// validate id method (not signal, slot or constructor)
		auto methodType = metaMethod.methodType();
		if (methodType != QMetaMethod::Method)
		{
			continue;
		}
		// add method
		QUaQualifiedName methName = QString::fromUtf8(metaMethod.name());
		retHash[methName] = methIdx;
	}
	return retHash;
}

bool QUaServer::isNodeBound(const UA_NodeId & nodeId, UA_Server *server)
{
	auto ptr = QUaNode::getNodeContext(nodeId, server);
	if (!ptr)
	{
		return false;
	}
	// test if nodeId assigned to context
	if (!UA_NodeId_equal(&nodeId, &ptr->_nodeId))
	{
		return false;
	}
	// success
	return true;
}

QUaServer * QUaServer::getServerNodeContext(UA_Server * server)
{
	auto context = QUaNode::getVoidContext(UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER), server);
	// try to cast to C++ server
#ifdef QT_DEBUG 
	auto srv = qobject_cast<QUaServer*>(static_cast<QObject*>(context));
	Q_CHECK_PTR(srv);
#else
	auto srv = static_cast<QUaServer*>(context);
#endif // QT_DEBUG 
	return srv;
}

QString QUaServer::_anonUser      = QObject::tr("[Anonymous]");
QString QUaServer::_anonUserToken = QObject::tr("[Anonymous:EmptyToken]");
QStringList QUaServer::_anonUsers = QStringList() 
	<< QUaServer::_anonUser
	<< QUaServer::_anonUserToken;

/// Wraps the default open62541 implementation (which validates the identity token
// against the configured policies and calls QUaServer::loginCallback for credentials)
// and keeps track of the session and its user in QUaServer
UA_StatusCode QUaServer::activateSession(UA_Server                    * server,
	                                     UA_AccessControl             * ac,
	                                     const UA_EndpointDescription * endpointDescription,
	                                     const UA_ByteString          * secureChannelRemoteCertificate,
	                                     const UA_NodeId              * sessionId,
	                                     const UA_ExtensionObject     * userIdentityToken,
	                                     void                        ** sessionContext)
{
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	Q_CHECK_PTR(srv);
	Q_CHECK_PTR(srv->_defaultActivateSession);
	UA_StatusCode st = srv->_defaultActivateSession(
		server,
		ac,
		endpointDescription,
		secureChannelRemoteCertificate,
		sessionId,
		userIdentityToken,
		sessionContext
	);
	if (st != UA_STATUSCODE_GOOD)
	{
		return st;
	}
	// get user name
	QString strUserName;
	if (userIdentityToken->encoding == UA_EXTENSIONOBJECT_ENCODED_NOBODY)
	{
		/* The empty token is interpreted as anonymous */
		strUserName = QUaServer::_anonUserToken;
	}
	else if (userIdentityToken->content.decoded.type == &UA_TYPES[UA_TYPES_ANONYMOUSIDENTITYTOKEN])
	{
		strUserName = QUaServer::_anonUser;
	}
	else if (userIdentityToken->content.decoded.type == &UA_TYPES[UA_TYPES_USERNAMEIDENTITYTOKEN])
	{
		const UA_UserNameIdentityToken *userToken =
			static_cast<const UA_UserNameIdentityToken*>(userIdentityToken->content.decoded.data);
		strUserName = QUaTypesConverter::uaStringToQString(userToken->userName);
	}
#ifdef UA_ENABLE_ENCRYPTION
	else if (userIdentityToken->content.decoded.type == &UA_TYPES[UA_TYPES_X509IDENTITYTOKEN] &&
	         srv->_userCertificateCallback)
	{
		// open62541 already checked the token signature and the certificate against the trust lists
		const UA_X509IdentityToken *certToken =
			static_cast<const UA_X509IdentityToken*>(userIdentityToken->content.decoded.data);
		strUserName = srv->_userCertificateCallback(QByteArray(
			reinterpret_cast<const char*>(certToken->certificateData.data),
			static_cast<qsizetype>(certToken->certificateData.length)
		));
		if (strUserName.isEmpty())
		{
			return UA_STATUSCODE_BADIDENTITYTOKENREJECTED;
		}
		srv->_certificateUsers.insert(strUserName);
	}
#endif // UA_ENABLE_ENCRYPTION
	else
	{
		/* Unsupported token type */
		return UA_STATUSCODE_BADIDENTITYTOKENINVALID;
	}
	// NOTE : actually is possible for a current session to change its user while maintaining nodeId
	if (!srv->_hashSessions.contains(*sessionId))
	{
		srv->_hashSessions.insert(*sessionId, new QUaSession(srv));
	}
	srv->_hashSessions[*sessionId]->_strUserName = strUserName;
	// NOTE : QUaServer::newSession is called when the session activated notification arrives
	/* No userdata atm */
	*sessionContext = nullptr;
	return UA_STATUSCODE_GOOD;
}

UA_StatusCode QUaServer::loginCallback(const UA_String               *userName,
	                                   const UA_ByteString           *password,
	                                   size_t                         usernamePasswordLoginSize,
	                                   const UA_UsernamePasswordLogin*usernamePasswordLogin,
	                                   void                         **sessionContext,
	                                   void                          *loginContext)
{
	Q_UNUSED(usernamePasswordLoginSize);
	Q_UNUSED(usernamePasswordLogin);
	Q_UNUSED(sessionContext);
	QUaServer *srv = static_cast<QUaServer*>(loginContext);
	Q_CHECK_PTR(srv);
	// anonymous login (already checked if allowed by open62541)
	if (!userName || userName->length == 0)
	{
		return UA_STATUSCODE_GOOD;
	}
	// check user and password
	const QString strUserName = QUaTypesConverter::uaStringToQString(*userName);
	const QString strPassword = QString::fromUtf8((char*)password->data, (int)password->length);
	// call validation callback
	if (!srv->_validationCallback(strUserName, strPassword))
	{
		return UA_STATUSCODE_BADUSERACCESSDENIED;
	}
	if (!srv->_hashUsers.contains(strUserName))
	{
		// Server must be aware of user name otherwise it will kick user out of session
		// in user access callbacks
		// NOTE : do not keep password in memory for security
		srv->_hashUsers.insert(strUserName, QString());
	}
	return UA_STATUSCODE_GOOD;
}

void QUaServer::newSession(QUaServer* server,
	                       const UA_NodeId* sessionId,
	                       const UA_UInt32 &secureChannelId)
{
	if (!server->_hashSessions.contains(*sessionId))
	{
		return;
	}
	// get client description
	QString strApplicationUri;
	QString strProductUri;
	QString strApplicationName;
	UA_Variant varDescription;
	UA_Variant_init(&varDescription);
	auto st = UA_Server_getSessionAttributeCopy(
		server->_server,
		sessionId,
		UA_QUALIFIEDNAME(0, (char*)"clientDescription"),
		&varDescription
	);
	if (st == UA_STATUSCODE_GOOD &&
		UA_Variant_hasScalarType(&varDescription, &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION]))
	{
		auto clientDescription = static_cast<UA_ApplicationDescription*>(varDescription.data);
		strApplicationUri  = QUaTypesConverter::uaStringToQString(clientDescription->applicationUri);
		strProductUri      = QUaTypesConverter::uaStringToQString(clientDescription->productUri);
		strApplicationName = QUaTypesConverter::uaVariantToQVariantScalar<QUaLocalizedText, UA_LocalizedText>(&clientDescription->applicationName);
	}
	UA_Variant_clear(&varDescription);
	// get connection data
	QString strAddress = QStringLiteral("Unknown");
	quint16 intPort    = 0;
	if (server->_hashChannelAddresses.contains(secureChannelId))
	{
		const auto &address = server->_hashChannelAddresses[secureChannelId];
		strAddress = address.first;
		intPort    = address.second;
	}
	// store session data
	auto session = server->_hashSessions[*sessionId];
	session->_strSessionId       = QUaTypesConverter::nodeIdToQString(*sessionId);
	session->_strApplicationName = strApplicationName;
	session->_strApplicationUri  = strApplicationUri;
	session->_strProductUri      = strProductUri;
	session->_strAddress         = strAddress;
	session->_intPort            = intPort;
	// emit new client connected event
	emit server->clientConnected(session);
}

// Get the peer address and port of a socket
static bool quaGetPeerAddress(const UA_UInt64 &connectionId, QString &strAddress, quint16 &intPort)
{
	sockaddr_storage address;
	memset(&address, 0, sizeof(address));
	socklen_t address_len = sizeof(address);
	if (getpeername(static_cast<quaSocket>(connectionId), reinterpret_cast<sockaddr*>(&address), &address_len) != 0)
	{
		return false;
	}
	char remote_name[NI_MAXHOST];
	if (getnameinfo(reinterpret_cast<sockaddr*>(&address), address_len,
		remote_name, sizeof(remote_name), nullptr, 0, NI_NUMERICHOST) != 0)
	{
		return false;
	}
	strAddress = QString::fromUtf8(remote_name);
	switch (address.ss_family)
	{
	case AF_INET:
		intPort = ntohs(reinterpret_cast<sockaddr_in*>(&address)->sin_port);
		break;
	case AF_INET6:
		intPort = ntohs(reinterpret_cast<sockaddr_in6*>(&address)->sin6_port);
		break;
	default:
		intPort = 0;
		break;
	}
	return true;
}

void QUaServer::secureChannelNotificationCallback(UA_Server                     *server,
	                                              UA_ApplicationNotificationType type,
	                                              const UA_KeyValueMap           payload)
{
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	if (!srv)
	{
		return;
	}
	const UA_UInt32 *channelId = static_cast<const UA_UInt32*>(UA_KeyValueMap_getScalar(
		&payload, UA_QUALIFIEDNAME(0, (char*)"securechannel-id"), &UA_TYPES[UA_TYPES_UINT32]));
	if (!channelId)
	{
		return;
	}
	if (type == UA_APPLICATIONNOTIFICATIONTYPE_SECURECHANNEL_CLOSED)
	{
		srv->_hashChannelAddresses.remove(*channelId);
		return;
	}
	if (type != UA_APPLICATIONNOTIFICATIONTYPE_SECURECHANNEL_OPENED)
	{
		return;
	}
	QString strAddress = QStringLiteral("Unknown");
	quint16 intPort    = 0;
	// NOTE : for tcp connections the connection id is the socket
	const UA_UInt64 *connectionId = static_cast<const UA_UInt64*>(UA_KeyValueMap_getScalar(
		&payload, UA_QUALIFIEDNAME(0, (char*)"connection-id"), &UA_TYPES[UA_TYPES_UINT64]));
	if (!connectionId || !quaGetPeerAddress(*connectionId, strAddress, intPort))
	{
		const UA_String *remoteAddress = static_cast<const UA_String*>(UA_KeyValueMap_getScalar(
			&payload, UA_QUALIFIEDNAME(0, (char*)"remote-address"), &UA_TYPES[UA_TYPES_STRING]));
		if (remoteAddress)
		{
			strAddress = QUaTypesConverter::uaStringToQString(*remoteAddress);
		}
	}
	srv->_hashChannelAddresses[*channelId] = qMakePair(strAddress, intPort);
}

void QUaServer::sessionNotificationCallback(UA_Server                     *server,
	                                        UA_ApplicationNotificationType type,
	                                        const UA_KeyValueMap           payload)
{
	if (type != UA_APPLICATIONNOTIFICATIONTYPE_SESSION_ACTIVATED)
	{
		return;
	}
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	if (!srv)
	{
		return;
	}
	const UA_NodeId *sessionId = static_cast<const UA_NodeId*>(UA_KeyValueMap_getScalar(
		&payload, UA_QUALIFIEDNAME(0, (char*)"session-id"), &UA_TYPES[UA_TYPES_NODEID]));
	const UA_UInt32 *channelId = static_cast<const UA_UInt32*>(UA_KeyValueMap_getScalar(
		&payload, UA_QUALIFIEDNAME(0, (char*)"securechannel-id"), &UA_TYPES[UA_TYPES_UINT32]));
	if (!sessionId || !channelId)
	{
		return;
	}
	QUaServer::newSession(srv, sessionId, *channelId);
}

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
void QUaServer::subscriptionNotificationCallback(UA_Server                     *server,
	                                             UA_ApplicationNotificationType type,
	                                             const UA_KeyValueMap           payload)
{
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	if (!srv)
	{
		return;
	}
	const UA_NodeId *sessionId = static_cast<const UA_NodeId*>(UA_KeyValueMap_getScalar(
		&payload, UA_QUALIFIEDNAME(0, (char*)"session-id"), &UA_TYPES[UA_TYPES_NODEID]));
	const UA_UInt32 *subscriptionId = static_cast<const UA_UInt32*>(UA_KeyValueMap_getScalar(
		&payload, UA_QUALIFIEDNAME(0, (char*)"subscription-id"), &UA_TYPES[UA_TYPES_UINT32]));
	if (!sessionId || !subscriptionId)
	{
		return;
	}
	auto &sessions = srv->_hashEventMonitoredItems;
	switch (type)
	{
	case UA_APPLICATIONNOTIFICATIONTYPE_SUBSCRIPTION_DELETED:
		{
			if (!sessions.contains(*sessionId))
			{
				return;
			}
			sessions[*sessionId].remove(*subscriptionId);
			if (sessions[*sessionId].isEmpty())
			{
				sessions.remove(*sessionId);
			}
		}
		break;
	case UA_APPLICATIONNOTIFICATIONTYPE_MONITOREDITEM_CREATED:
		{
			const UA_UInt32 *monitoredItemId = static_cast<const UA_UInt32*>(UA_KeyValueMap_getScalar(
				&payload, UA_QUALIFIEDNAME(0, (char*)"monitoreditem-id"), &UA_TYPES[UA_TYPES_UINT32]));
			const UA_UInt32 *attributeId = static_cast<const UA_UInt32*>(UA_KeyValueMap_getScalar(
				&payload, UA_QUALIFIEDNAME(0, (char*)"attribute-id"), &UA_TYPES[UA_TYPES_UINT32]));
			const UA_NodeId *targetNode = static_cast<const UA_NodeId*>(UA_KeyValueMap_getScalar(
				&payload, UA_QUALIFIEDNAME(0, (char*)"target-node"), &UA_TYPES[UA_TYPES_NODEID]));
			// only event monitored items are of interest
			if (!monitoredItemId || !attributeId || !targetNode ||
				*attributeId != UA_ATTRIBUTEID_EVENTNOTIFIER)
			{
				return;
			}
			sessions[*sessionId][*subscriptionId][*monitoredItemId] = *targetNode;
		}
		break;
	case UA_APPLICATIONNOTIFICATIONTYPE_MONITOREDITEM_DELETE:
		{
			const UA_UInt32 *monitoredItemId = static_cast<const UA_UInt32*>(UA_KeyValueMap_getScalar(
				&payload, UA_QUALIFIEDNAME(0, (char*)"monitoreditem-id"), &UA_TYPES[UA_TYPES_UINT32]));
			if (!monitoredItemId ||
				!sessions.contains(*sessionId) ||
				!sessions[*sessionId].contains(*subscriptionId))
			{
				return;
			}
			auto &subscriptions = sessions[*sessionId];
			subscriptions[*subscriptionId].remove(*monitoredItemId);
			if (subscriptions[*subscriptionId].isEmpty())
			{
				subscriptions.remove(*subscriptionId);
			}
			if (subscriptions.isEmpty())
			{
				sessions.remove(*sessionId);
			}
		}
		break;
	default:
		break;
	}
}
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

void QUaServer::closeSession(UA_Server        * server, 
	                         UA_AccessControl * ac, 
	                         const UA_NodeId  * sessionId, 
	                         void             * sessionContext)
{
	Q_UNUSED(sessionContext);
	Q_UNUSED(ac);
	// get server
	QUaServer *srv = QUaServer::getServerNodeContext(server);
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	srv->_hashEventMonitoredItems.remove(*sessionId);
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	// remove session from hash
	if (!srv->_hashSessions.contains(*sessionId))
	{
		// TODO : failed connection, bad identity log message
		return;
	}
	auto session = srv->_hashSessions.take(*sessionId);
	emit srv->clientDisconnected(session);
	session->deleteLater();
}

UA_UInt32 QUaServer::getUserRightsMask(UA_Server        *server,
	                                   UA_AccessControl *ac,
	                                   const UA_NodeId  *sessionId,
	                                   void             *sessionContext,
	                                   const UA_NodeId  *nodeId,
	                                   void             *nodeContext) 
{
	Q_UNUSED(nodeContext);
	Q_UNUSED(sessionContext);
	Q_UNUSED(ac);
	// get server
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	Q_ASSERT(srv->_hashSessions.contains(*sessionId));
	// get user
    QString strUserName = srv->_hashSessions[*sessionId]->_strUserName;
	// check if user still exists
	if (!srv->userExists(strUserName))
	{
		// TODO : wait until officially supported
		// https://github.com/open62541/open62541/issues/2617
		//auto st = UA_Server_closeSession(server, sessionId);
		//Q_ASSERT(st == UA_STATUSCODE_GOOD);
		return (UA_UInt32)0;
	}
	// if node from user tree then call user implementation
	QUaNode * node = QUaNode::getNodeContext(*nodeId, server);
	if (node)
	{
		return node->userWriteMaskInternal(strUserName).intValue;
	}
	// else default
	return 0xFFFFFFFF;
}

UA_Byte QUaServer::getUserAccessLevel(UA_Server        *server,
	                                  UA_AccessControl *ac,
	                                  const UA_NodeId  *sessionId,
	                                  void             *sessionContext,
	                                  const UA_NodeId  *nodeId,
	                                  void             *nodeContext)
{
	Q_UNUSED(nodeContext);
	Q_UNUSED(sessionContext);
	Q_UNUSED(ac);
	// get server
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	Q_ASSERT(srv->_hashSessions.contains(*sessionId));
	// get user
    QString strUserName = srv->_hashSessions[*sessionId]->_strUserName;
	// check if user still exists
	if (!srv->userExists(strUserName))
	{
		// TODO : wait until officially supported
		// https://github.com/open62541/open62541/issues/2617
		//auto st = UA_Server_closeSession(server, sessionId);
		//Q_ASSERT(st == UA_STATUSCODE_GOOD);
		return (UA_UInt32)0;
	}
	// if node from user tree then call user implementation
	QUaNode * node = QUaNode::getNodeContext(*nodeId, server);
	QUaBaseVariable * variable = qobject_cast<QUaBaseVariable *>(node);
	if (variable)
	{
		return variable->userAccessLevelInternal(strUserName).intValue;
	}
	// else default
	return 0xFF;
}

// NOTE : called when reading attributes
UA_Boolean QUaServer::getUserExecutable(UA_Server        *server, 
		                                UA_AccessControl *ac,
		                                const UA_NodeId  *sessionId, 
		                                void             *sessionContext,
		                                const UA_NodeId  *methodId, 
		                                void             *methodContext)
{
	Q_UNUSED(methodContext);
	Q_UNUSED(methodId);
	Q_UNUSED(sessionContext);
	Q_UNUSED(ac);
	// overall execution permissions for method regardless of conntext object
	// boils down to whether user exists
	// get server
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	Q_ASSERT(srv->_hashSessions.contains(*sessionId));
	// get user
    QString strUserName = srv->_hashSessions[*sessionId]->_strUserName;
	// check if user still exists
	if (!srv->userExists(strUserName))
	{
		// TODO : wait until officially supported
		// https://github.com/open62541/open62541/issues/2617
		//auto st = UA_Server_closeSession(server, sessionId);
		//Q_ASSERT(st == UA_STATUSCODE_GOOD);
		return false;
	}
	return true;
}

// NOTE : called when actually requesting to execute method
UA_Boolean QUaServer::getUserExecutableOnObject(UA_Server        *server, 
		                                        UA_AccessControl *ac,
		                                        const UA_NodeId  *sessionId, 
		                                        void             *sessionContext,
		                                        const UA_NodeId  *methodId, 
		                                        void             *methodContext,
		                                        const UA_NodeId  *objectId, 
		                                        void             *objectContext)
{
	Q_UNUSED(objectContext);
	Q_UNUSED(methodContext);
	Q_UNUSED(methodId);
	Q_UNUSED(sessionContext);
	Q_UNUSED(ac);
	// get server
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	Q_ASSERT(srv->_hashSessions.contains(*sessionId));
	// get user
    QString strUserName = srv->_hashSessions[*sessionId]->_strUserName;
	// check if user still exists
	if (!srv->userExists(strUserName))
	{
		// TODO : wait until officially supported
		// https://github.com/open62541/open62541/issues/2617
		//auto st = UA_Server_closeSession(server, sessionId);
		//Q_ASSERT(st == UA_STATUSCODE_GOOD);
		return false;
	}
	// if node from user tree then call user implementation
	QUaNode * node = QUaNode::getNodeContext(*objectId, server);
	QUaBaseObject * object = qobject_cast<QUaBaseObject *>(node);
	if (object)
	{
		// NOTE : could not diff by method name because name multiples are possible
		return object->userExecutableInternal(strUserName);
	}
	// else default
	return true;
}

///
/// \brief Returns the client session \a sessionId, or nullptr when it is unknown.
///
const QUaSession* QUaServer::sessionById(const UA_NodeId* sessionId) const
{
	return sessionId ? _hashSessions.value(*sessionId, nullptr) : nullptr;
}

///
/// \brief [STATIC] Asks the add node callback, if any, whether a client may add \a item.
///
UA_Boolean QUaServer::allowAddNode(UA_Server *server, UA_AccessControl *ac,
                                   const UA_NodeId *sessionId, void *sessionContext,
                                   const UA_AddNodesItem *item)
{
	Q_UNUSED(ac);
	Q_UNUSED(sessionContext);
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	if (!srv->_addNodeCallback)
	{
		return true;
	}
	return srv->_addNodeCallback(srv->sessionById(sessionId),
	                              QUaNodeId(item->parentNodeId.nodeId),
	                              QUaQualifiedName(item->browseName),
	                              QUaNodeId(item->typeDefinition.nodeId));
}

///
/// \brief [STATIC] Asks the delete node callback, if any, whether a client may delete \a item.
///
UA_Boolean QUaServer::allowDeleteNode(UA_Server *server, UA_AccessControl *ac,
                                      const UA_NodeId *sessionId, void *sessionContext,
                                      const UA_DeleteNodesItem *item)
{
	Q_UNUSED(ac);
	Q_UNUSED(sessionContext);
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	if (!srv->_deleteNodeCallback)
	{
		return true;
	}
	return srv->_deleteNodeCallback(srv->sessionById(sessionId), QUaNodeId(item->nodeId));
}

///
/// \brief [STATIC] Asks the add reference callback, if any, whether a client may add \a item.
///
UA_Boolean QUaServer::allowAddReference(UA_Server *server, UA_AccessControl *ac,
                                        const UA_NodeId *sessionId, void *sessionContext,
                                        const UA_AddReferencesItem *item)
{
	Q_UNUSED(ac);
	Q_UNUSED(sessionContext);
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	if (!srv->_addReferenceCallback)
	{
		return true;
	}
	return srv->_addReferenceCallback(srv->sessionById(sessionId),
	                                   QUaNodeId(item->sourceNodeId),
	                                   QUaNodeId(item->referenceTypeId),
	                                   QUaNodeId(item->targetNodeId.nodeId),
	                                   item->isForward);
}

///
/// \brief [STATIC] Asks the delete reference callback, if any, whether a client may delete \a item.
///
UA_Boolean QUaServer::allowDeleteReference(UA_Server *server, UA_AccessControl *ac,
                                           const UA_NodeId *sessionId, void *sessionContext,
                                           const UA_DeleteReferencesItem *item)
{
	Q_UNUSED(ac);
	Q_UNUSED(sessionContext);
	QUaServer *srv = QUaServer::getServerNodeContext(server);
	if (!srv->_deleteReferenceCallback)
	{
		return true;
	}
	return srv->_deleteReferenceCallback(srv->sessionById(sessionId),
	                                      QUaNodeId(item->sourceNodeId),
	                                      QUaNodeId(item->referenceTypeId),
	                                      QUaNodeId(item->targetNodeId.nodeId),
	                                      item->isForward);
}

///
/// \brief Sets the callback that allows or denies client AddNodes requests; without one, all are allowed.
///
void QUaServer::setAddNodeCallback(const QUaAddNodeCallback& callback)
{
	_addNodeCallback = callback;
}

///
/// \brief Sets the callback that allows or denies client DeleteNodes requests; without one, all are allowed.
///
void QUaServer::setDeleteNodeCallback(const QUaDeleteNodeCallback& callback)
{
	_deleteNodeCallback = callback;
}

///
/// \brief Sets the callback that allows or denies client AddReferences requests; without one, all are allowed.
///
void QUaServer::setAddReferenceCallback(const QUaReferenceCallback& callback)
{
	_addReferenceCallback = callback;
}

///
/// \brief Sets the callback that allows or denies client DeleteReferences requests; without one, all are allowed.
///
void QUaServer::setDeleteReferenceCallback(const QUaReferenceCallback& callback)
{
	_deleteReferenceCallback = callback;
}

QUaServer::QUaServer(QObject* parent/* = 0*/)
	: QObject(parent)
{
	// defaults
	_beingDestroyed = false;
	_port = 4840;
	_anonymousLoginAllowed = true;
	_byteCertificate = QByteArray();
	_byteCertificateInternal = QByteArray();
	_methodRetStatusCode = UA_STATUSCODE_GOOD;
	_childNodeIdCallback = nullptr;
	_defaultActivateSession = nullptr;
	memset(&_nodeLifecycle, 0, sizeof(UA_GlobalNodeLifecycle));
#ifdef UA_ENABLE_ENCRYPTION
	_bytePrivateKey = QByteArray();
	_bytePrivateKeyInternal = QByteArray();
	_securityPolicies = QUaSecurityPolicy::All;
	_securityModes = QUaMessageSecurityMode::All;
#endif
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	_conditionsRefreshRequired = false;
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
#ifdef UA_ENABLE_HISTORIZING
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	_maxHistoryEventResponseSize = 1000;
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
#endif // UA_ENABLE_HISTORIZING
	// create long-living open62541 server instance with custom logger
	this->setupLogger();
	UA_ServerConfig config;
	memset(&config, 0, sizeof(UA_ServerConfig));
	config.logging = &_logger;
	auto st = UA_ServerConfig_setMinimal(&config, _port, nullptr);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	this->_server = UA_Server_newWithConfig(&config);
	Q_CHECK_PTR(this->_server);
	// register custom types to be used with Qt (QVariant and stuff)
	QUaTypesConverter::registerCustomTypes();
	// setup server (other defaults)
	this->setupServer();
}

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
void QUaServer::addChange(const QUaChangeStructureDataType& change)
{
	// NOTE : do not check if server is running because we might wanna
	//        historize offline events
	if (_listChanges.contains(change))
	{
		return;
	}
	_listChanges.append(change);
	// if trigger already scheduled, then ealry exit
	if (_changeEventSignaler.processing())
	{
		return;
	}
	// exec trigger on next event loop iteration
	_changeEventSignaler.execLater([this]() {
		// trigger
		auto time = QDateTime::currentDateTimeUtc();
		_changeEvent->setChanges(_listChanges);
		_changeEvent->setTime(time);
		_changeEvent->setReceiveTime(time);
		_changeEvent->trigger();
		// clean list of changes buffer
		_listChanges.clear();
		_changeEvent->setChanges(_listChanges);
	});
}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
void QUaServer::requireConditionsRefresh(const QUaLocalizedText& message/* = QUaLocalizedText()*/)
{
	// do net send multiple refresh required events in a single Qt event loop iteration
	if (_conditionsRefreshRequired)
	{
		return;
	}
	// set flag
	_conditionsRefreshRequired = true;
	// schedule refresh required event
	auto time = QDateTime::currentDateTimeUtc();
	_refreshRequiredEvent->setEventId(QUaBaseEvent::generateEventId());
	_refreshRequiredEvent->setTime(time);
	_refreshRequiredEvent->setReceiveTime(time);
	_refreshRequiredEvent->setMessage(message);
	// exec trigger on next event loop iteration
	_changeEventSignaler.execLater([this]() {
		// trigger
		_refreshRequiredEvent->trigger();
		// reset flag
		_conditionsRefreshRequired = false;
	});
}
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

#ifdef UA_ENABLE_HISTORIZING
UA_HistoryDataGathering QUaServer::getGathering() const
{
	return _historGathering;
}
quint8 QUaServer::eventNotifier() const
{
	UA_Byte outByte;
	auto st = UA_Server_readEventNotifier(_server, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER), &outByte);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	return outByte;
}
void QUaServer::setEventNotifier(const quint8& eventNotifier)
{
	// open62541 1.5 feeds EventNotifier writes to event monitored items as data changes
	if (eventNotifier == this->eventNotifier())
	{
		return;
	}
	auto st = UA_Server_writeEventNotifier(_server, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER), eventNotifier);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// TODO : emit signal?	
}
#endif // UA_ENABLE_HISTORIZING

bool QUaServer::resetConfig()
{
	// clean old config and create new
	UA_ServerConfig * config = UA_Server_getConfig(_server);
	// NOTE : cannot call UA_ServerConfig_clean because it cleans node store
	//        and event loop, so we just call the parts that interest us
	/* Security Policy */
	for (size_t i = 0; i < config->securityPoliciesSize; ++i) {
		UA_SecurityPolicy* policy = &config->securityPolicies[i];
		policy->clear(policy);
	}
	UA_free(config->securityPolicies);
	config->securityPolicies = nullptr;
	config->securityPoliciesSize = 0;
	/* Endoints */
	for (size_t i = 0; i < config->endpointsSize; ++i)
		UA_EndpointDescription_clear(&config->endpoints[i]);
	UA_free(config->endpoints);
	config->endpoints = nullptr;
	config->endpointsSize = 0;
	/* Certificate Validation */
	if (config->secureChannelPKI.clear)
		config->secureChannelPKI.clear(&config->secureChannelPKI);
	if (config->sessionPKI.clear)
		config->sessionPKI.clear(&config->sessionPKI);
	/* Access Control */
	if (config->accessControl.clear)
		config->accessControl.clear(&config->accessControl);

	UA_StatusCode st;
#ifndef UA_ENABLE_ENCRYPTION
	// convert cert if valid
	UA_ByteString cert;
	UA_ByteString* ptrCert = QUaServer::parseCertificate(_byteCertificate, cert, _byteCertificateInternal);
	st = UA_ServerConfig_setMinimal(config, _port, ptrCert);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
#else
	// convert cert if valid (should contain public key)
	UA_ByteString cert;
	UA_ByteString* ptrCert = QUaServer::parseCertificate(_byteCertificate, cert, _byteCertificateInternal);
	// convert private key if valid
	UA_ByteString priv;
	UA_ByteString* ptrPriv = QUaServer::parseCertificate(_bytePrivateKey, priv, _bytePrivateKeyInternal);
	// check if valid private key
	if (ptrCert && ptrPriv)
	{
		// create config with port, certificate, private key and trust lists for encryption
		const QVector<UA_ByteString> trusted    = QUaServer::toByteStringArray(_listTrusted);
		const QVector<UA_ByteString> issuers    = QUaServer::toByteStringArray(_listIssuers);
		const QVector<UA_ByteString> revocation = QUaServer::toByteStringArray(_listRevocation);
		st = UA_ServerConfig_setDefaultWithSecurityPolicies(
			config,
			_port,
			ptrCert,
			ptrPriv,
			trusted.constData(),
			static_cast<size_t>(trusted.size()),
			issuers.constData(),
			static_cast<size_t>(issuers.size()),
			revocation.constData(),
			static_cast<size_t>(revocation.size())
		);
		if (st != UA_STATUSCODE_GOOD)
		{
			UA_LOG_ERROR(config->logging, UA_LOGCATEGORY_SERVER,
				"Could not configure encryption with the given certificates : %s", UA_StatusCode_name(st));
		}
	}
	else
	{
		// create config with port and certificate only (no encryption)
		st = UA_ServerConfig_setMinimal(config, _port, ptrCert);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
	}
	if (ptrPriv)
	{
		UA_ByteString_clear(ptrPriv);
	}
#endif
	if (ptrCert)
	{
		UA_ByteString_clear(ptrCert);
	}
	if (st != UA_STATUSCODE_GOOD || !this->applySecurityFilter(config))
	{
		return false;
	}
	this->applyServerUrls(config);

	// NOTE : open62541 >= 1.4 does not allow user and password on unencrypted
	//        channels by default, keep previous QUaServer behaviour if no encryption
#ifndef UA_ENABLE_ENCRYPTION
	config->allowNonePolicyPassword = true;
#else
	config->allowNonePolicyPassword = !(ptrCert && ptrPriv);
#endif
	// setup access control (use default implementation with custom login callback)
	// NOTE : a (dummy) login entry is needed so open62541 offers the username token policy,
	//        the actual validation is done in QUaServer::loginCallback
	UA_UsernamePasswordLogin dummyLogin;
	dummyLogin.username = UA_STRING_NULL;
	dummyLogin.password = UA_BYTESTRING_NULL;
	st = UA_AccessControl_defaultWithLoginCallback(
		config,
		_anonymousLoginAllowed,
		nullptr,
		1,
		&dummyLogin,
		&QUaServer::loginCallback,
		this
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// certificate (x509) user tokens need a callback to map them to users, and a private key to check their signature
#ifdef UA_ENABLE_ENCRYPTION
	const bool userCertificatesAllowed = _userCertificateCallback && ptrCert && ptrPriv;
#else
	const bool userCertificatesAllowed = false;
#endif // UA_ENABLE_ENCRYPTION
	if (!userCertificatesAllowed)
	{
		UA_AccessControl* ac = &config->accessControl;
		size_t count = 0;
		for (size_t i = 0; i < ac->userTokenPoliciesSize; i++)
		{
			if (ac->userTokenPolicies[i].tokenType == UA_USERTOKENTYPE_CERTIFICATE)
			{
				UA_UserTokenPolicy_clear(&ac->userTokenPolicies[i]);
				continue;
			}
			ac->userTokenPolicies[count++] = ac->userTokenPolicies[i];
		}
		ac->userTokenPoliciesSize = count;
	}

	// setup server description
	UA_ApplicationDescription_clear(&config->applicationDescription);
	config->applicationDescription.applicationType = UA_APPLICATIONTYPE_SERVER;
	config->applicationDescription.applicationName = UA_LOCALIZEDTEXT_ALLOC((char*)"", _byteApplicationName.constData());
	config->applicationDescription.applicationUri  = UA_String_fromChars(_byteApplicationUri.constData());
	// NOTE : update application description productUri as well
	config->applicationDescription.productUri      = UA_String_fromChars(_byteProductUri.constData());
	UA_BuildInfo_clear(&config->buildInfo);
	config->buildInfo.productName                  = UA_String_fromChars(_byteProductName.constData());
	config->buildInfo.productUri                   = UA_String_fromChars(_byteProductUri.constData());
	config->buildInfo.manufacturerName             = UA_String_fromChars(_byteManufacturerName.constData());
	config->buildInfo.softwareVersion              = UA_String_fromChars(_byteSoftwareVersion.constData());
	config->buildInfo.buildNumber                  = UA_String_fromChars(_byteBuildNumber.constData());
	config->buildInfo.buildDate                    = UA_DateTime_now();
	// update endpoints (they contain a copy of the application description)
	for (size_t i = 0; i < config->endpointsSize; ++i)
	{
		UA_ApplicationDescription_clear(&config->endpoints[i].server);
		st = UA_ApplicationDescription_copy(&config->applicationDescription, &config->endpoints[i].server);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
	}

	// setup server limits
	config->maxSecureChannels = _maxSecureChannels;
	config->maxSessions       = _maxSessions;
	this->applyLimits(config);

	// custom callbacks
	this->setupConfigCallbacks();

	Q_UNUSED(st);
	return true;
}

///
/// \brief Removes the endpoints whose security policy or mode is not allowed.
///        The security policies themselves are kept, since discovery always runs over SecurityPolicy None.
/// \return False when no endpoint is left to publish.
///
bool QUaServer::applySecurityFilter(UA_ServerConfig* config)
{
#ifdef UA_ENABLE_ENCRYPTION
	size_t count = 0;
	for (size_t i = 0; i < config->endpointsSize; i++)
	{
		UA_EndpointDescription& endpoint = config->endpoints[i];
		if (!(securityPolicyFromUri(endpoint.securityPolicyUri) & _securityPolicies) ||
			!(securityModeFromUa(endpoint.securityMode) & _securityModes))
		{
			UA_EndpointDescription_clear(&endpoint);
			continue;
		}
		config->endpoints[count++] = endpoint;
	}
	config->endpointsSize = count;
#endif // UA_ENABLE_ENCRYPTION
	if (config->endpointsSize == 0)
	{
		UA_LOG_ERROR(config->logging, UA_LOGCATEGORY_SERVER,
			"No endpoint matches the allowed security policies and modes");
		return false;
	}
	return true;
}

///
/// \brief Replaces the default listen-on-all-interfaces server URL with one bound to the configured hostname.
///
void QUaServer::applyServerUrls(UA_ServerConfig* config)
{
	if (_strHostname.isEmpty())
	{
		return;
	}
	UA_Array_delete(config->serverUrls, config->serverUrlsSize, &UA_TYPES[UA_TYPES_STRING]);
	const QByteArray url = QStringLiteral("opc.tcp://%1:%2").arg(_strHostname).arg(_port).toUtf8();
	config->serverUrls     = UA_String_new();
	*config->serverUrls    = UA_String_fromChars(url.constData());
	config->serverUrlsSize = 1;
}

#ifdef UA_ENABLE_ENCRYPTION
///
/// \brief Wraps the non-empty items of \a list as UA_ByteString views, without copying the data.
/// \return Views that are valid as long as \a list is not modified.
///
QVector<UA_ByteString> QUaServer::toByteStringArray(const QList<QByteArray>& list)
{
	QVector<UA_ByteString> array;
	array.reserve(list.size());
	for (const QByteArray& item : list)
	{
		if (item.isEmpty())
		{
			continue;
		}
		UA_ByteString bytes;
		bytes.length = static_cast<size_t>(item.size());
		bytes.data   = reinterpret_cast<UA_Byte*>(const_cast<char*>(item.constData()));
		array.append(bytes);
	}
	return array;
}
#endif // UA_ENABLE_ENCRYPTION

void QUaServer::setupConfigCallbacks()
{
	UA_ServerConfig* config = UA_Server_getConfig(_server);
	// static methods to reimplement custom behaviour
	_defaultActivateSession = config->accessControl.activateSession;
	config->accessControl.activateSession           = &QUaServer::activateSession;
	config->accessControl.closeSession              = &QUaServer::closeSession;
	config->accessControl.getUserRightsMask         = &QUaServer::getUserRightsMask;
	config->accessControl.getUserAccessLevel        = &QUaServer::getUserAccessLevel;
	config->accessControl.getUserExecutable         = &QUaServer::getUserExecutable;
	config->accessControl.getUserExecutableOnObject = &QUaServer::getUserExecutableOnObject;

	config->accessControl.allowAddNode              = &QUaServer::allowAddNode;
	config->accessControl.allowDeleteNode           = &QUaServer::allowDeleteNode;
	config->accessControl.allowAddReference         = &QUaServer::allowAddReference;
	config->accessControl.allowDeleteReference      = &QUaServer::allowDeleteReference;

	// notifications used to track clients and monitored items
	config->secureChannelNotificationCallback = &QUaServer::secureChannelNotificationCallback;
	config->sessionNotificationCallback       = &QUaServer::sessionNotificationCallback;
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	config->subscriptionNotificationCallback  = &QUaServer::subscriptionNotificationCallback;
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

#ifdef UA_ENABLE_HISTORIZING
	config->historyDatabase = _historDatabase;
#endif // UA_ENABLE_HISTORIZING

	// custom instance declaration NodeId mechanism
	config->nodeLifecycle = &_nodeLifecycle;
	_nodeLifecycle.generateChildNodeId = &QUaServer::generateChildNodeId;
}

UA_ByteString * QUaServer::parseCertificate(const QByteArray &inByteCert,
	                                        UA_ByteString    &outUaCert,
	                                        QByteArray       &outByteCertt)
{
	UA_ByteString *ptr = nullptr;
	if (!inByteCert.isEmpty())
	{
		outByteCertt = inByteCert;
		// convert QByteArray to UA_ByteString
		size_t          cert_length = static_cast<size_t>(outByteCertt.length());
		const UA_Byte * cert_data = reinterpret_cast<const UA_Byte *>(outByteCertt.constData());
		outUaCert.length = cert_length;
		UA_StatusCode success = UA_Array_copy(
			cert_data,                                  // src
			cert_length,                                // size
			reinterpret_cast<void **>(&outUaCert.data), // dst
			&UA_TYPES[UA_TYPES_BYTE]                    // type
		);
		// only set pointer if succeeds
		if (success == UA_STATUSCODE_GOOD)
		{
			ptr = &outUaCert;
		}
	}
	return ptr;
}

void QUaServer::setupServer()
{
	// Server stuff
	UA_StatusCode st;
	_running = false;
	// Set default validation callback
	_validationCallback = [this](const QString& strUserName, const QString& strPassword) {
		if (!_hashUsers.contains(strUserName))
		{
			return false;
		}
		return _hashUsers[strUserName].compare(strPassword, Qt::CaseSensitive) == 0;
	};
	// Create "Objects" folder using special constructor
	// Part 5 - 8.2.4 : Objects
	auto objectsNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
	this->_newNodeNodeId = &objectsNodeId;
	this->_newNodeMetaObject = &QUaFolderObject::staticMetaObject;
	_pobjectsFolder = new QUaFolderObject(this);
	_pobjectsFolder->setParent(this);
	_pobjectsFolder->setObjectName( QStringLiteral("Objects") );
	// register base types (for all types)
	this->registerSpecificationType<QUaBaseVariable>    (UA_NODEID_NUMERIC(0, UA_NS0ID_BASEVARIABLETYPE    ), true);
	this->registerSpecificationType<QUaBaseDataVariable>(UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE));
	this->registerSpecificationType<QUaProperty>        (UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE        ));
	this->registerSpecificationType<QUaBaseObject>      (UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE      ));
	this->registerSpecificationType<QUaFolderObject>    (UA_NODEID_NUMERIC(0, UA_NS0ID_FOLDERTYPE          ));
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	this->registerSpecificationType<QUaBaseEvent              >(UA_NODEID_NUMERIC(0, UA_NS0ID_BASEEVENTTYPE), true);
	this->registerSpecificationType<QUaBaseModelChangeEvent   >(UA_NODEID_NUMERIC(0, UA_NS0ID_BASEMODELCHANGEEVENTTYPE));
	this->registerSpecificationType<QUaGeneralModelChangeEvent>(UA_NODEID_NUMERIC(0, UA_NS0ID_GENERALMODELCHANGEEVENTTYPE));
	this->registerSpecificationType<QUaSystemEvent            >(UA_NODEID_NUMERIC(0, UA_NS0ID_SYSTEMEVENTTYPE));
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	this->registerSpecificationType<QUaConditionVariable         >(UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONVARIABLETYPE  ));
	this->registerSpecificationType<QUaStateVariable             >(UA_NODEID_NUMERIC(0, UA_NS0ID_STATEVARIABLETYPE      ));
	this->registerSpecificationType<QUaTwoStateVariable          >(UA_NODEID_NUMERIC(0, UA_NS0ID_TWOSTATEVARIABLETYPE   ));
	this->registerSpecificationType<QUaFiniteStateVariable       >(UA_NODEID_NUMERIC(0, UA_NS0ID_FINITESTATEVARIABLETYPE));
	this->registerSpecificationType<QUaTransitionVariable        >(UA_NODEID_NUMERIC(0, UA_NS0ID_TRANSITIONVARIABLETYPE ));
	this->registerSpecificationType<QUaFiniteTransitionVariable  >(UA_NODEID_NUMERIC(0, UA_NS0ID_FINITETRANSITIONVARIABLETYPE));
	this->registerSpecificationType<QUaCondition                 >(UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE), true);
	this->registerSpecificationType<QUaAcknowledgeableCondition  >(UA_NODEID_NUMERIC(0, UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE));
	this->registerSpecificationType<QUaAlarmCondition            >(UA_NODEID_NUMERIC(0, UA_NS0ID_ALARMCONDITIONTYPE    ));
	this->registerSpecificationType<QUaStateMachine              >(UA_NODEID_NUMERIC(0, UA_NS0ID_STATEMACHINETYPE      ));
	this->registerSpecificationType<QUaFiniteStateMachine        >(UA_NODEID_NUMERIC(0, UA_NS0ID_FINITESTATEMACHINETYPE), true);
	this->registerSpecificationType<QUaState                     >(UA_NODEID_NUMERIC(0, UA_NS0ID_STATETYPE     ));
	this->registerSpecificationType<QUaTransition                >(UA_NODEID_NUMERIC(0, UA_NS0ID_TRANSITIONTYPE));
	this->registerSpecificationType<QUaExclusiveLimitStateMachine>(UA_NODEID_NUMERIC(0, UA_NS0ID_EXCLUSIVELIMITSTATEMACHINETYPE));
	this->registerSpecificationType<QUaDiscreteAlarm             >(UA_NODEID_NUMERIC(0, UA_NS0ID_DISCRETEALARMTYPE       ));
	this->registerSpecificationType<QUaOffNormalAlarm            >(UA_NODEID_NUMERIC(0, UA_NS0ID_OFFNORMALALARMTYPE      ));
	this->registerSpecificationType<QUaLimitAlarm                >(UA_NODEID_NUMERIC(0, UA_NS0ID_LIMITALARMTYPE          ));
	this->registerSpecificationType<QUaExclusiveLimitAlarm       >(UA_NODEID_NUMERIC(0, UA_NS0ID_EXCLUSIVELIMITALARMTYPE ));
	this->registerSpecificationType<QUaExclusiveLevelAlarm       >(UA_NODEID_NUMERIC(0, UA_NS0ID_EXCLUSIVELEVELALARMTYPE ));
	this->registerSpecificationType<QUaRefreshStartEvent         >(UA_NODEID_NUMERIC(0, UA_NS0ID_REFRESHSTARTEVENTTYPE   ));
	this->registerSpecificationType<QUaRefreshEndEvent           >(UA_NODEID_NUMERIC(0, UA_NS0ID_REFRESHENDEVENTTYPE     ));
	this->registerSpecificationType<QUaRefreshRequiredEvent      >(UA_NODEID_NUMERIC(0, UA_NS0ID_REFRESHREQUIREDEVENTTYPE));
	this->registerSpecificationType<QUaTransitionEvent           >(UA_NODEID_NUMERIC(0, UA_NS0ID_TRANSITIONEVENTTYPE     ));
	// register static condition refresh methods
	// Part 9 - 5.57 and 5.5.8
	st = UA_Server_setMethodNodeCallback(
		_server, 
		UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE_CONDITIONREFRESH), 
		&QUaCondition::ConditionRefresh
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	st = UA_Server_setMethodNodeCallback(
		_server,
		UA_NODEID_NUMERIC(0, UA_NS0ID_CONDITIONTYPE_CONDITIONREFRESH2),
		&QUaCondition::ConditionRefresh2
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
	this->registerSpecificationType<QUaOptionSetVariable        >(UA_NODEID_NUMERIC(0, UA_NS0ID_OPTIONSETTYPE));
#endif
	// set context for server
	st = UA_Server_setNodeContext(_server, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER), (void*)this); Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// lets value sources tell local writes from client writes (see QUaBaseVariable::writeValueSource)
	UA_Server_setAdminSessionContext(_server, this);
	// add default supported references
	_hashHierRefTypes.insert({ QStringLiteral("Organizes")          , QStringLiteral("OrganizedBy")        }, UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES          ));
	_hashHierRefTypes.insert({ QStringLiteral("HasOrderedComponent"), QStringLiteral("OrderedComponentOf") }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASORDEREDCOMPONENT));
	_hashHierRefTypes.insert({ QStringLiteral("HasComponent")       , QStringLiteral("ComponentOf")        }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT       ));
	_hashHierRefTypes.insert({ QStringLiteral("HasProperty")        , QStringLiteral("PropertyOf")         }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY        ));
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	_hashHierRefTypes.insert({ QStringLiteral("HasEventSource")     , QStringLiteral("EventSourceOf")      }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASEVENTSOURCE));
	_hashHierRefTypes.insert({ QStringLiteral("HasNotifier")        , QStringLiteral("NotifierOf")         }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASNOTIFIER   ));
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	// hierarchical
	_hashHierRefTypes.insert({ QStringLiteral("HasTrueSubState") , QStringLiteral("IsTrueSubStateOf")  }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASTRUESUBSTATE ));
	_hashHierRefTypes.insert({ QStringLiteral("HasFalseSubState"), QStringLiteral("IsFalseSubStateOf") }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASFALSESUBSTATE));
	// non-hierarchical
	_hashRefTypes.insert({ QStringLiteral("HasCondition")      , QStringLiteral("IsConditionOf")     }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCONDITION));
	_hashRefTypes.insert({ QStringLiteral("FromState")         , QStringLiteral("ToTransition")      }, UA_NODEID_NUMERIC(0, UA_NS0ID_FROMSTATE)); // Part 5 - B.4.11
	_hashRefTypes.insert({ QStringLiteral("ToState")           , QStringLiteral("FromTransition")    }, UA_NODEID_NUMERIC(0, UA_NS0ID_TOSTATE)); // Part 5 - B.4.12
	_hashRefTypes.insert({ QStringLiteral("HasCause")          , QStringLiteral("MayBeCausedBy")     }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCAUSE)); // Part 5 - B.4.13
	_hashRefTypes.insert({ QStringLiteral("HasEffect")         , QStringLiteral("MayBeEffectedBy")   }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASEFFECT)); // Part 5 - B.4.14
	_hashRefTypes.insert({ QStringLiteral("HasSubStateMachine"), QStringLiteral("SubStateMachineOf") }, UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBSTATEMACHINE)); // Part 5 - B.4.15
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	for (auto it = _hashHierRefTypes.constBegin(); it != _hashHierRefTypes.constEnd(); ++it)
		_hashRefTypes.insert(it.key(), it.value());

	// read initial values for server description
	// NOTE : config already initialized with minimal settings in constructor
	UA_ServerConfig* config = UA_Server_getConfig(_server);
	// get server app name
	_byteApplicationName = QByteArray(
		(char*)config->applicationDescription.applicationName.text.data,
		(int)config->applicationDescription.applicationName.text.length
	);
	// get server app uri
	_byteApplicationUri = QByteArray(
		(char*)config->applicationDescription.applicationUri.data,
		(int)config->applicationDescription.applicationUri.length
	);
	// get server product name
	_byteProductName = QByteArray(
		(char*)config->buildInfo.productName.data,
		(int)config->buildInfo.productName.length
	);
	// get server product uri
	_byteProductUri = QByteArray(
		(char*)config->buildInfo.productUri.data,
		(int)config->buildInfo.productUri.length
	);
	// get server manufacturer name
	_byteManufacturerName = QByteArray(
		(char*)config->buildInfo.manufacturerName.data,
		(int)config->buildInfo.manufacturerName.length
	);
	// get server software version
	_byteSoftwareVersion = QByteArray(
		(char*)config->buildInfo.softwareVersion.data,
		(int)config->buildInfo.softwareVersion.length
	);
	// get server software version
	_byteBuildNumber = QByteArray(
		(char*)config->buildInfo.buildNumber.data,
		(int)config->buildInfo.buildNumber.length
	);

	// copy other initial values
	_maxSecureChannels = config->maxSecureChannels;
	_maxSessions = config->maxSessions;
	_limits = QUaServer::limitsFromConfig(config);

	// instantiate change event
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	_changeEvent = this->createEvent<QUaGeneralModelChangeEvent>();
	Q_CHECK_PTR(_changeEvent);
	_changeEvent->setSourceNode(QUaTypesConverter::nodeIdToQString(UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER)));
	_changeEvent->setSourceName(tr("Server"));
	_changeEvent->setMessage(tr("Node added or removed."));
	_changeEvent->setSeverity(1);
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	_refreshStartEvent = this->createEvent<QUaRefreshStartEvent>();
	Q_CHECK_PTR(_refreshStartEvent);
	_refreshStartEvent->setSourceNode(QUaTypesConverter::nodeIdToQString(UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER)));
	_refreshStartEvent->setSourceName(tr("Server"));
	_refreshStartEvent->setMessage("");
	_refreshStartEvent->setSeverity(100);
	_refreshEndEvent = this->createEvent<QUaRefreshEndEvent>();
	Q_CHECK_PTR(_refreshEndEvent);
	_refreshEndEvent->setSourceNode(QUaTypesConverter::nodeIdToQString(UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER)));
	_refreshEndEvent->setSourceName(tr("Server"));
	_refreshEndEvent->setMessage("");
	_refreshEndEvent->setSeverity(100);
	_refreshRequiredEvent = this->createEvent<QUaRefreshRequiredEvent>();
	Q_CHECK_PTR(_refreshRequiredEvent);
	_refreshRequiredEvent->setSourceNode(QUaTypesConverter::nodeIdToQString(UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER)));
	_refreshRequiredEvent->setSourceName(tr("Server"));
	_refreshRequiredEvent->setMessage("");
	_refreshRequiredEvent->setSeverity(100);
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

#ifdef UA_ENABLE_HISTORIZING
	UA_HistoryDataGathering gathering = UA_HistoryDataGathering_Default(1000);
	_historGathering = gathering;
	_historDatabase = UA_HistoryDatabase_default(gathering);
	// add historic event handling is supported
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	// NOTE : changed setEvent for optimized call in QUaServer_Anex::UA_Server_triggerEvent_Modified
	_historDatabase.setEvent  = nullptr; 
	_historDatabase.readEvent = &QUaHistoryBackend::readEvent;
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
	config->historyDatabase = _historDatabase;
#endif // UA_ENABLE_HISTORIZING
}

void QUaServer::setupLogger()
{
	_logger.log = [](void* logContext, UA_LogLevel level, UA_LogCategory category, const char* msg, va_list args)
	{
#ifdef QT_DEBUG
		auto srv = qobject_cast<QUaServer*>(static_cast<QObject*>(logContext));
		Q_CHECK_PTR(srv);
#else
		auto srv = static_cast<QUaServer*>(logContext);
#endif // QT_DEBUG
		// do not process log message if nobody listening
		static const QMetaMethod logSignal = QMetaMethod::fromSignal(&QUaServer::logMessage);
		if (!srv->isSignalConnected(logSignal))
		{
			return;
		}
		// NOTE : open62541 uses custom format specifiers (e.g. %N for NodeId)
		//        so the message must be formatted with UA_String_vformat
		UA_String buffer;
		buffer.data   = reinterpret_cast<UA_Byte*>(srv->_logBuffer);
		buffer.length = QUA_MAX_LOG_MESSAGE_SIZE;
		UA_String_vformat(&buffer, msg, args);
		emit srv->logMessage({
			QByteArray(srv->_logBuffer, static_cast<int>(buffer.length)),
			static_cast<QUaLogLevel>(level),
			static_cast<QUaLogCategory>(category)
		});
	};
	_logger.context = this;
	_logger.clear   = nullptr;
}

QUaServer::~QUaServer()
{
	_beingDestroyed = true;
	// stop if running
	this->stop();
	// [FIX] : QObject children destructors were called after this one
	//         and the ~QUaNode destructor makes use of _server
	//         so we better destroy the children manually before deleting _server
	while (this->children().count() > 0)
	{
		delete this->children().at(0);
	}
	// cleanup open62541
	UA_Server_delete(this->_server);
	// custom data types are not owned by open62541
	for (auto custType : _customDataTypes)
	{
		UA_NodeId_clear(&custType->type.typeId);
		delete custType;
	}
	_customDataTypes.clear();
}

quint16 QUaServer::port() const
{
	return _port;
}

void QUaServer::setPort(const quint16& intPort)
{
	_port = intPort;
	emit this->portChanged(_port);
}

QByteArray QUaServer::certificate() const
{
	return _byteCertificate;
}

void QUaServer::setCertificate(const QByteArray& byteCertificate)
{
	_byteCertificate = byteCertificate;
	emit this->certificateChanged(_byteCertificate);
}

#ifdef UA_ENABLE_ENCRYPTION
QByteArray QUaServer::privateKey() const
{
	return _bytePrivateKey;
}

void QUaServer::setPrivateKey(const QByteArray& bytePrivateKey)
{
	_bytePrivateKey = bytePrivateKey;
	emit this->privateKeyChanged(_bytePrivateKey);
}

///
/// \brief Returns the DER certificates of the clients and CAs that the server trusts.
///
QList<QByteArray> QUaServer::trustedCertificates() const
{
	return _listTrusted;
}

///
/// \brief Sets the DER certificates of the clients and CAs that the server trusts.
///        While the list is empty, the server accepts any client certificate.
///        Applied on the next start().
///
void QUaServer::setTrustedCertificates(const QList<QByteArray>& trustedCertificates)
{
	_listTrusted = trustedCertificates;
	emit this->trustedCertificatesChanged(_listTrusted);
}

///
/// \brief Returns the DER CA certificates used to build the chain of a client certificate.
///
QList<QByteArray> QUaServer::issuerCertificates() const
{
	return _listIssuers;
}

///
/// \brief Sets the DER CA certificates used to build the chain of a client certificate,
///        without trusting them. Ignored while the trusted certificates list is empty.
///        Applied on the next start().
///
void QUaServer::setIssuerCertificates(const QList<QByteArray>& issuerCertificates)
{
	_listIssuers = issuerCertificates;
	emit this->issuerCertificatesChanged(_listIssuers);
}

///
/// \brief Returns the DER certificate revocation lists of the trusted and issuer CAs.
///
QList<QByteArray> QUaServer::revocationLists() const
{
	return _listRevocation;
}

///
/// \brief Sets the DER certificate revocation lists of the trusted and issuer CAs.
///        Ignored while the trusted certificates list is empty. Applied on the next start().
///
void QUaServer::setRevocationLists(const QList<QByteArray>& revocationLists)
{
	_listRevocation = revocationLists;
	emit this->revocationListsChanged(_listRevocation);
}

///
/// \brief Returns the security policies the server publishes endpoints for.
///
QUaSecurityPolicies QUaServer::securityPolicies() const
{
	return _securityPolicies;
}

///
/// \brief Restricts the published endpoints to \a securityPolicies. Applied on the next start().
///
void QUaServer::setSecurityPolicies(const QUaSecurityPolicies& securityPolicies)
{
	_securityPolicies = securityPolicies;
	emit this->securityPoliciesChanged(_securityPolicies);
}

///
/// \brief Returns the message security modes the server publishes endpoints for.
///
QUaMessageSecurityModes QUaServer::securityModes() const
{
	return _securityModes;
}

///
/// \brief Restricts the published endpoints to \a securityModes. Applied on the next start().
///
void QUaServer::setSecurityModes(const QUaMessageSecurityModes& securityModes)
{
	_securityModes = securityModes;
	emit this->securityModesChanged(_securityModes);
}
#endif

///
/// \brief Returns the hostname the server listens on and advertises as discovery URL.
///
QString QUaServer::hostname() const
{
	return _strHostname;
}

///
/// \brief Sets the hostname or IP address the server listens on and advertises as discovery URL.
///        An empty hostname listens on all interfaces. Applied on the next start().
///
void QUaServer::setHostname(const QString& hostname)
{
	_strHostname = hostname;
	emit this->hostnameChanged(_strHostname);
}

QString QUaServer::applicationName() const
{
	return QString::fromUtf8(_byteApplicationName);
}

void QUaServer::setApplicationName(const QString& strApplicationName)
{
	// update config
	_byteApplicationName = strApplicationName.toUtf8();
	// emit event
	emit this->applicationNameChanged(strApplicationName);
}

QString QUaServer::applicationUri() const
{
	return QString::fromUtf8(_byteApplicationUri);
}

void QUaServer::setApplicationUri(const QString& strApplicationUri)
{
	// update config
	_byteApplicationUri = strApplicationUri.toUtf8();
	// emit event
	emit this->applicationUriChanged(strApplicationUri);
}

QString QUaServer::productName() const
{
	return QString::fromUtf8(_byteProductName);
}

void QUaServer::setProductName(const QString& strProductName)
{
	// update config
	_byteProductName = strProductName.toUtf8();
	// emit event
	emit this->productNameChanged(strProductName);
}

QString QUaServer::productUri() const
{
	return QString::fromUtf8(_byteProductUri);
}

void QUaServer::setProductUri(const QString& strProductUri)
{
	// update config
	_byteProductUri = strProductUri.toUtf8();
	// emit event
	emit this->productUriChanged(strProductUri);
}

QString QUaServer::manufacturerName() const
{
	return QString::fromUtf8(_byteManufacturerName);
}

void QUaServer::setManufacturerName(const QString& strManufacturerName)
{
	// update config
	_byteManufacturerName = strManufacturerName.toUtf8();
	// emit event
	emit this->manufacturerNameChanged(strManufacturerName);
}

QString QUaServer::softwareVersion() const
{
	return QString::fromUtf8(_byteSoftwareVersion);
}

void QUaServer::setSoftwareVersion(const QString& strSoftwareVersion)
{
	// update config
	_byteSoftwareVersion = strSoftwareVersion.toUtf8();
	// emit event
	emit this->softwareVersionChanged(strSoftwareVersion);
}

QString QUaServer::buildNumber() const
{
	return QString::fromUtf8(_byteBuildNumber);
}

void QUaServer::setBuildNumber(const QString& strBuildNumber)
{
	// update config
	_byteBuildNumber = strBuildNumber.toUtf8();
	// emit event
	emit this->buildNumberChanged(strBuildNumber);
}

bool QUaServer::start()
{
	// NOTE : we must define port and other server params upon instantiation, 
	//        because rest of API assumes _server is valid
	if (_running)
	{
		return true;
	}
	if (!this->resetConfig())
	{
		return false;
	}
	// start open62541 server
	auto st = UA_Server_run_startup(_server);
	if (st != UA_STATUSCODE_GOOD)
	{
		return false;
	}
	_running = true;
	QObject::connect(&_iterWaitTimer, &QTimer::timeout, this,
	[this]() {
		// do not iterate if asked to stop
		if (!_running) { return; }
		// iterate and restart
		_iterWaitTimer.stop();
		// UA_Server_run_iterate(server, true) blocks up to 500 ms since open62541 1.4, stalling Qt and delaying responses
		UA_EventLoop* eventLoop = UA_Server_getConfig(_server)->eventLoop;
		eventLoop->run(eventLoop, QUA_ITERATE_TIMEOUT_MS);
		_iterWaitTimer.start(0);
	}, Qt::QueuedConnection);
	// start iterations
	_iterWaitTimer.start(0);
	// emit event
	emit this->isRunningChanged(_running);
	return true;
}

void QUaServer::stop()
{
	if (!_running)
	{
		return;
	}
	_running = false;
	_iterWaitTimer.stop();
	_iterWaitTimer.disconnect();
	// [FIX] force remove sessions (calls QUaServer::closeSession for each one)
	const auto sessionIds = _hashSessions.keys();
	for (const auto& sessionId : sessionIds)
	{
		UA_Server_closeSession(_server, &sessionId);
	}
	// NOTE : shutdown closes all secure channels and processes all delayed callbacks
	UA_Server_run_shutdown(_server);
	_hashChannelAddresses.clear();
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	_hashEventMonitoredItems.clear();
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	// emit event
	emit this->isRunningChanged(_running);
}

bool QUaServer::isRunning() const
{
	return _running;
}

void QUaServer::setIsRunning(const bool& running)
{
	if (running)
	{
		this->start();
	}
	else
	{
		this->stop();
	}
}

quint16 QUaServer::maxSecureChannels() const
{
	return _maxSecureChannels;
}

void QUaServer::setMaxSecureChannels(const quint16& maxSecureChannels)
{
	_maxSecureChannels = maxSecureChannels;
	emit this->maxSecureChannelsChanged(_maxSecureChannels);
}

quint16 QUaServer::maxSessions() const
{
	return _maxSessions;
}

void QUaServer::setMaxSessions(const quint16& maxSessions)
{
	_maxSessions = maxSessions;
	emit this->maxSessionsChanged(_maxSessions);
}

///
/// \brief Returns the session, operation, subscription and monitored item limits.
///
QUaServerLimits QUaServer::limits() const
{
	return _limits;
}

///
/// \brief Sets the session, operation, subscription and monitored item limits. Applied on the next start().
/// \return False, leaving the limits unchanged, when \a limits is not valid (see QUaServerLimits::isValid()).
///
bool QUaServer::setLimits(const QUaServerLimits& limits)
{
	if (!limits.isValid())
	{
		return false;
	}
	if (limits != _limits)
	{
		_limits = limits;
		emit this->limitsChanged(_limits);
	}
	return true;
}

///
/// \brief Reads the limits of \a config, used to start from the open62541 defaults.
///
QUaServerLimits QUaServer::limitsFromConfig(const UA_ServerConfig* config)
{
	QUaServerLimits limits;
	limits.maxSessionTimeout                        = config->maxSessionTimeout;
	limits.maxSecurityTokenLifetime                 = config->maxSecurityTokenLifetime;
	limits.maxNodesPerRead                          = config->maxNodesPerRead;
	limits.maxNodesPerWrite                         = config->maxNodesPerWrite;
	limits.maxNodesPerMethodCall                    = config->maxNodesPerMethodCall;
	limits.maxNodesPerBrowse                        = config->maxNodesPerBrowse;
	limits.maxNodesPerTranslateBrowsePathsToNodeIds = config->maxNodesPerTranslateBrowsePathsToNodeIds;
	limits.maxMonitoredItemsPerCall                 = config->maxMonitoredItemsPerCall;
	limits.maxReferencesPerNode                     = config->maxReferencesPerNode;
#ifdef UA_ENABLE_SUBSCRIPTIONS
	limits.maxSubscriptions                         = config->maxSubscriptions;
	limits.maxSubscriptionsPerSession               = config->maxSubscriptionsPerSession;
	limits.minPublishingInterval                    = config->publishingIntervalLimits.min;
	limits.maxPublishingInterval                    = config->publishingIntervalLimits.max;
	limits.maxNotificationsPerPublish               = config->maxNotificationsPerPublish;
	limits.maxPublishRequestsPerSession             = config->maxPublishReqPerSession;
	limits.maxMonitoredItems                        = config->maxMonitoredItems;
	limits.maxMonitoredItemsPerSubscription         = config->maxMonitoredItemsPerSubscription;
	limits.minSamplingInterval                      = config->samplingIntervalLimits.min;
	limits.maxSamplingInterval                      = config->samplingIntervalLimits.max;
	limits.minQueueSize                             = config->queueSizeLimits.min;
	limits.maxQueueSize                             = config->queueSizeLimits.max;
#endif // UA_ENABLE_SUBSCRIPTIONS
	return limits;
}

///
/// \brief Writes the stored limits over the defaults that open62541 sets on every configuration reset.
///
void QUaServer::applyLimits(UA_ServerConfig* config) const
{
	config->maxSessionTimeout                        = _limits.maxSessionTimeout;
	config->maxSecurityTokenLifetime                 = _limits.maxSecurityTokenLifetime;
	config->maxNodesPerRead                          = _limits.maxNodesPerRead;
	config->maxNodesPerWrite                         = _limits.maxNodesPerWrite;
	config->maxNodesPerMethodCall                    = _limits.maxNodesPerMethodCall;
	config->maxNodesPerBrowse                        = _limits.maxNodesPerBrowse;
	config->maxNodesPerTranslateBrowsePathsToNodeIds = _limits.maxNodesPerTranslateBrowsePathsToNodeIds;
	config->maxMonitoredItemsPerCall                 = _limits.maxMonitoredItemsPerCall;
	config->maxReferencesPerNode                     = _limits.maxReferencesPerNode;
#ifdef UA_ENABLE_SUBSCRIPTIONS
	config->maxSubscriptions                         = _limits.maxSubscriptions;
	config->maxSubscriptionsPerSession               = _limits.maxSubscriptionsPerSession;
	config->publishingIntervalLimits.min             = _limits.minPublishingInterval;
	config->publishingIntervalLimits.max             = _limits.maxPublishingInterval;
	config->maxNotificationsPerPublish               = _limits.maxNotificationsPerPublish;
	config->maxPublishReqPerSession                  = _limits.maxPublishRequestsPerSession;
	config->maxMonitoredItems                        = _limits.maxMonitoredItems;
	config->maxMonitoredItemsPerSubscription         = _limits.maxMonitoredItemsPerSubscription;
	config->samplingIntervalLimits.min               = _limits.minSamplingInterval;
	config->samplingIntervalLimits.max               = _limits.maxSamplingInterval;
	config->queueSizeLimits.min                      = _limits.minQueueSize;
	config->queueSizeLimits.max                      = _limits.maxQueueSize;
#endif // UA_ENABLE_SUBSCRIPTIONS
}

QUaServerLimits::QUaServerLimits()
	: maxSessionTimeout(0.0)
	, maxSecurityTokenLifetime(0)
	, maxNodesPerRead(0)
	, maxNodesPerWrite(0)
	, maxNodesPerMethodCall(0)
	, maxNodesPerBrowse(0)
	, maxNodesPerTranslateBrowsePathsToNodeIds(0)
	, maxMonitoredItemsPerCall(0)
	, maxReferencesPerNode(0)
	, maxSubscriptions(0)
	, maxSubscriptionsPerSession(0)
	, minPublishingInterval(0.0)
	, maxPublishingInterval(0.0)
	, maxNotificationsPerPublish(0)
	, maxPublishRequestsPerSession(0)
	, maxMonitoredItems(0)
	, maxMonitoredItemsPerSubscription(0)
	, minSamplingInterval(0.0)
	, maxSamplingInterval(0.0)
	, minQueueSize(0)
	, maxQueueSize(0)
{
}

///
/// \brief Tells whether every range has its minimum below its maximum and no interval is below 5 ms.
///
bool QUaServerLimits::isValid() const
{
	// open62541 rejects publishing and sampling intervals below 5 ms
	static constexpr double minimumInterval = 5.0;
	return minPublishingInterval >= minimumInterval && minPublishingInterval <= maxPublishingInterval &&
	       minSamplingInterval   >= minimumInterval && minSamplingInterval   <= maxSamplingInterval   &&
	       minQueueSize <= maxQueueSize;
}

bool QUaServerLimits::operator==(const QUaServerLimits& other) const
{
	return maxSessionTimeout                        == other.maxSessionTimeout                        &&
	       maxSecurityTokenLifetime                 == other.maxSecurityTokenLifetime                 &&
	       maxNodesPerRead                          == other.maxNodesPerRead                          &&
	       maxNodesPerWrite                         == other.maxNodesPerWrite                         &&
	       maxNodesPerMethodCall                    == other.maxNodesPerMethodCall                    &&
	       maxNodesPerBrowse                        == other.maxNodesPerBrowse                        &&
	       maxNodesPerTranslateBrowsePathsToNodeIds == other.maxNodesPerTranslateBrowsePathsToNodeIds &&
	       maxMonitoredItemsPerCall                 == other.maxMonitoredItemsPerCall                 &&
	       maxReferencesPerNode                     == other.maxReferencesPerNode                     &&
	       maxSubscriptions                         == other.maxSubscriptions                         &&
	       maxSubscriptionsPerSession               == other.maxSubscriptionsPerSession               &&
	       minPublishingInterval                    == other.minPublishingInterval                    &&
	       maxPublishingInterval                    == other.maxPublishingInterval                    &&
	       maxNotificationsPerPublish               == other.maxNotificationsPerPublish               &&
	       maxPublishRequestsPerSession             == other.maxPublishRequestsPerSession             &&
	       maxMonitoredItems                        == other.maxMonitoredItems                        &&
	       maxMonitoredItemsPerSubscription         == other.maxMonitoredItemsPerSubscription         &&
	       minSamplingInterval                      == other.minSamplingInterval                      &&
	       maxSamplingInterval                      == other.maxSamplingInterval                      &&
	       minQueueSize                             == other.minQueueSize                             &&
	       maxQueueSize                             == other.maxQueueSize;
}

bool QUaServerLimits::operator!=(const QUaServerLimits& other) const
{
	return !(*this == other);
}

void QUaServer::setChildNodeIdCallback(const QUaChildNodeIdCallback& callback)
{
	_childNodeIdCallback = callback;
}

void QUaServer::registerTypeInternal(
	const QMetaObject& metaObject, 
	const QUaNodeId& nodeId/* = ""*/
)
{
	// check if OPC UA relevant
	if (!metaObject.inherits(&QUaNode::staticMetaObject))
	{
		Q_ASSERT_X(false, "QUaServer::registerType", "Unsupported base class");
		return;
	}
	// check if already registered
	QString   strClassName = QString::fromUtf8(metaObject.className());
	UA_NodeId newTypeNodeId = _mapTypes.value(strClassName, UA_NODEID_NULL);
	if (!UA_NodeId_isNull(&newTypeNodeId))
	{
		Q_ASSERT(_mapTypes.contains(strClassName));
		return;
	}
	// create new type browse name
	UA_QualifiedName browseName;
	browseName.namespaceIndex = 0;
	browseName.name = QUaTypesConverter::uaStringFromQString(strClassName);
	// check if base class is registered
	QString strBaseClassName = QString::fromUtf8(metaObject.superClass()->className());
	if (!_mapTypes.contains(strBaseClassName))
	{
		// recursive
		this->registerTypeInternal(*metaObject.superClass());
	}
	Q_ASSERT_X(_mapTypes.contains(strBaseClassName), "QUaServer::registerType", "Base object type not registered.");
	// check if requested node id defined
	if (!nodeId.isNull())
	{
		// check if requested node id exists
		bool isUsed = this->isNodeIdUsed(nodeId);
		Q_ASSERT_X(!isUsed, "QUaServer::registerType", "Requested NodeId already exists");
		if (isUsed)
		{
			UA_QualifiedName_clear(&browseName);
			return;
		}
	}
	UA_NodeId reqNodeId = nodeId;
	// check if variable or object
	if (metaObject.inherits(&QUaBaseDataVariable::staticMetaObject))
	{
		// create variable type attributes
		UA_VariableTypeAttributes vtAttr = UA_VariableTypeAttributes_default;
		// set node attributes		  
		QByteArray byteDisplayName = strClassName.toUtf8();
		vtAttr.displayName = UA_LOCALIZEDTEXT((char*)"", byteDisplayName.data());
		QByteArray byteDescription;
		vtAttr.description = UA_LOCALIZEDTEXT((char*)"", byteDescription.data());
		// add new variable type
		auto st = UA_Server_addVariableTypeNode(_server,
			reqNodeId,                                 // requested nodeId
			_mapTypes.value(strBaseClassName, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE)), // parent (variable type)
			UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE), // parent relation with child
			browseName,
			UA_NODEID_NULL,                            // typeDefinition ??
			vtAttr,
			(void*)this,                               // context : server instance where type was registered
			&newTypeNodeId);                           // new variable type id
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
	}
	else
	{
		Q_ASSERT(metaObject.inherits(&QUaBaseObject::staticMetaObject));
		// create object type attributes
		UA_ObjectTypeAttributes otAttr = UA_ObjectTypeAttributes_default;
		// set node attributes		  
		QByteArray byteDisplayName = strClassName.toUtf8();
		otAttr.displayName = UA_LOCALIZEDTEXT((char*)"", byteDisplayName.data());
		QByteArray byteDescription;
		otAttr.description = UA_LOCALIZEDTEXT((char*)"", byteDescription.data());
		// add new object type
		auto st = UA_Server_addObjectTypeNode(_server,
			reqNodeId,                                 // requested nodeId
			_mapTypes.value(strBaseClassName, UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE)), // parent (object type)
			UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE), // parent relation with child
			browseName,
			otAttr,
			(void*)this,                               // context : server instance where type was registered
			&newTypeNodeId);                           // new object type id
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
	}
	// clean up
	UA_QualifiedName_clear(&browseName);
	UA_NodeId_clear(&reqNodeId);
	// add to registered types map
	_mapTypes.insert(strClassName, newTypeNodeId);
	_hashMetaObjects.insert(strClassName, metaObject);
	// register for default mandatory children and so on
	// in case our custom type inherits from a spec type
	// which contains mandatory children
	this->registerTypeDefaults(newTypeNodeId, metaObject);
	// register constructor/destructor
	this->registerTypeLifeCycle(newTypeNodeId, metaObject);
	// register meta-enums
	this->registerMetaEnums(metaObject);
	// register meta-properties
	// NOTE : this can be recursive if property type has not been yet registered
	this->addMetaProperties(metaObject);
	// register meta-methods (only if object class, or NOT variable class)
	if (!metaObject.inherits(&QUaBaseDataVariable::staticMetaObject))
	{
		this->addMetaMethods(metaObject);
	}
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	// needed to store historic events in a consistent way
	if (metaObject.inherits(&QUaBaseEvent::staticMetaObject))
	{
		Q_ASSERT(!_hashTypeVars.contains(newTypeNodeId));
		_hashTypeVars[newTypeNodeId] =
			QUaNode::getTypeVars(
				newTypeNodeId,
				this->_server
			);
	}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
}

QList<QUaNode*> QUaServer::typeInstances(const QMetaObject& metaObject)
{
	QList<QUaNode*> retList;
	// check if OPC UA relevant
	if (!metaObject.inherits(&QUaNode::staticMetaObject))
	{
		Q_ASSERT_X(false, "QUaServer::typeInstances", "Unsupported base class. It must derive from QUaNode");
		return retList;
	}
	// try to get typeNodeId, if null, then register it
	UA_NodeId typeNodeId = this->typeIdByMetaObject(metaObject);
	Q_ASSERT(!UA_NodeId_isNull(&typeNodeId));
	// make ua browse
	auto refTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASTYPEDEFINITION);
	QList<UA_NodeId> retRefSet;
	UA_BrowseDescription* bDesc = UA_BrowseDescription_new();
	UA_NodeId_copy(&typeNodeId, &bDesc->nodeId);
	bDesc->browseDirection = UA_BROWSEDIRECTION_INVERSE; //TypeDefinitionOf
	bDesc->includeSubtypes = true;
	bDesc->referenceTypeId = refTypeId;
	bDesc->resultMask = UA_BROWSERESULTMASK_REFERENCETYPEID;
	// browse
	UA_BrowseResult bRes = UA_Server_browse(_server, 0, bDesc);
	Q_ASSERT(bRes.statusCode == UA_STATUSCODE_GOOD);
	while (bRes.referencesSize > 0)
	{
		for (size_t i = 0; i < bRes.referencesSize; i++)
		{
			UA_ReferenceDescription rDesc = bRes.references[i];
			Q_ASSERT(UA_NodeId_equal(&rDesc.referenceTypeId, &refTypeId));
			UA_NodeId nodeId/* = rDesc.nodeId.nodeId*/;
			UA_NodeId_copy(&rDesc.nodeId.nodeId, &nodeId);
			retRefSet << nodeId;
		}
        UA_BrowseResult_clear(&bRes);
		bRes = UA_Server_browseNext(_server, true, &bRes.continuationPoint);
	}
	// cleanup
    UA_BrowseDescription_clear(bDesc);
	UA_BrowseDescription_delete(bDesc);
    UA_BrowseResult_clear(&bRes);
	// get QUaNode references
	for (int i = 0; i < retRefSet.count(); i++)
	{
		// when browsing ObjectsFolder there are children with null context (Server object and children)
		QUaNode* node = QUaNode::getNodeContext(retRefSet[i], _server);
		if (node)
		{
			retList << node;
		}
		UA_NodeId_clear(&retRefSet[i]);
	}
	return retList;
}

void QUaServer::registerTypeLifeCycle(const UA_NodeId& typeNodeId, const QMetaObject& metaObject)
{
	Q_ASSERT(!UA_NodeId_isNull(&typeNodeId));
	if (UA_NodeId_isNull(&typeNodeId))
	{
		return;
	}
	// add custom constructor
	Q_ASSERT_X(!_hashConstructors.contains(typeNodeId), "QUaServer::registerType", "Constructor for type already exists.");
	// NOTE : we need constructors to be lambdas in order to cache metaobject in capture
	//        because type context is already the server instance where type was registered
	//        so we can differentiate the server instance in the static ::uaConstructor callback
	//        TLDR; to support multiple server instances in an application
	_hashConstructors[typeNodeId] = [metaObject, this](const UA_NodeId* instanceNodeId, void** nodeContext) {
		// call static method
		return QUaServer::uaConstructor(this, instanceNodeId, nodeContext, metaObject);
	};
	// set generic constructor (that calls custom one internally)
	UA_NodeTypeLifecycle lifecycle;
	lifecycle.constructor = &QUaServer::uaConstructor;
	lifecycle.destructor = &QUaServer::uaDestructor;
	auto st = UA_Server_setNodeTypeLifecycle(_server, typeNodeId, lifecycle);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st)
}

void QUaServer::registerTypeDefaults(const UA_NodeId& typeNodeId, const QMetaObject& metaObject)
{
	// cache mandatory children if not done before
	Q_ASSERT(!_hashMandatoryChildren.contains(typeNodeId));
	if (_hashMandatoryChildren.contains(typeNodeId))
	{
		return;
	}
	// copy mandatory from parent type, unless base type
    static UA_NodeId baseObjType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE);
    static UA_NodeId baseVarType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEVARIABLETYPE);
    if (UA_NodeId_equal(&typeNodeId, &baseVarType) ||
        UA_NodeId_equal(&typeNodeId, &baseObjType))
	{
		_hashMandatoryChildren[typeNodeId] = QSet<QUaQualifiedName>();
		return;
	}
	QUaNodeId superTypeNodeId = QUaNode::superTypeDefinitionNodeId(typeNodeId, this->_server);
	Q_ASSERT_X(
		_hashMandatoryChildren.contains(superTypeNodeId) ||
		metaObject.superClass() == &QUaNode::staticMetaObject,
		"QUaServer::registerTypeDefaults", "Parent type must already be registered.");
	_hashMandatoryChildren[typeNodeId] =
		_hashMandatoryChildren.value(superTypeNodeId, QSet<QUaQualifiedName>());
	// get mandatory children browse names
	const auto chidrenNodeIds = QUaNode::getChildrenNodeIds(typeNodeId, _server);
	for (const auto & childNodeId : chidrenNodeIds)
	{
		// ignore if not mandatory
		if (!QUaNode::hasMandatoryModellingRule(childNodeId, _server))
		{
			continue;
		}
		// sometimes children repeat parent's mandatory, no need to add twice
		QUaQualifiedName mandatoryBrowseName = QUaNode::getBrowseName(childNodeId, _server);
		if (_hashMandatoryChildren[typeNodeId].contains(mandatoryBrowseName))
		{
			continue;
		}
		_hashMandatoryChildren[typeNodeId]
			<< mandatoryBrowseName;
	}
	// get qt type methods by name
	// loop meta methods and find out which ones inherit from
	QHash<QUaQualifiedName, int> hashQtMethods = QUaServer::metaMethodIndexes(metaObject);
	// get all ua methods
	auto methodsNodeIds = QUaNode::getMethodsNodeIds(typeNodeId, _server);
	// try to match ua methods with qt meta methods (by browse name)
	for (const auto & methodNodeId : std::as_const(methodsNodeIds))
	{
		// ignore if not mandatory or optional
		if (!QUaNode::hasMandatoryModellingRule(methodNodeId, _server) &&
			!QUaNode::hasOptionalModellingRule(methodNodeId, _server))
		{
			continue;
		}
		// check mandatory ua method exists on qt type
		QUaQualifiedName methBrowseName = QUaNode::getBrowseName(methodNodeId, _server);
		if (!hashQtMethods.contains(methBrowseName))
		{
			// NOTE : newer versions of the specification add optional methods to
			//        existing types, those are ignored if not implemented by the Qt type
			Q_ASSERT_X(!QUaNode::hasMandatoryModellingRule(methodNodeId, _server), "QUaServer::registerTypeDefaults",
				"Qt type does not implement mandatory method.");
			QString strClassName = QString::fromUtf8(metaObject.className());
			qDebug() << "QUaServer : ignoring not implemented method" << methBrowseName << "for" << strClassName;
			continue;
		}

		// TODO : check arguments and return types

		int methIdx = hashQtMethods[methBrowseName];
		QUaServer::bindMethod(this, &methodNodeId, methIdx);
	}
	// cleanup for all ua methods
	for (auto & methNodeId : methodsNodeIds)
	{
		UA_NodeId_clear(&methNodeId);
	}
}

void QUaServer::registerMetaEnums(const QMetaObject& metaObject)
{
	int enumCount = metaObject.enumeratorCount();
	for (int i = metaObject.enumeratorOffset(); i < enumCount; i++)
	{
		QMetaEnum metaEnum = metaObject.enumerator(i);
		this->registerEnum(metaEnum);
	}
}

void QUaServer::addMetaProperties(const QMetaObject& metaObject)
{
	UA_NodeId parentTypeNodeId = this->typeIdByMetaObject(metaObject);
	Q_ASSERT(!UA_NodeId_isNull(&parentTypeNodeId));
	// loop meta properties and find out which ones inherit from
	int propCount = metaObject.propertyCount();
	for (int i = metaObject.propertyOffset(); i < propCount; i++)
	{
		QMetaProperty metaProperty = metaObject.property(i);
		// check if is meta enum
		bool      isVariable = false;
		bool      isEnum = false;
		UA_NodeId enumTypeNodeId = UA_NODEID_NULL;
		if (metaProperty.isEnumType())
		{
			QMetaEnum metaEnum = metaProperty.enumerator();
			// compose enum name
			QString strEnumName = QStringLiteral("%1::%2").arg(
						QString::fromLatin1(metaEnum.scope()),
						QString::fromLatin1(metaEnum.enumName()));
			// must already be registered by now
			Q_ASSERT(_hashEnums.contains(strEnumName));
			// get enum data type
			enumTypeNodeId = _hashEnums.value(strEnumName);
			// allow to continue
			isEnum = true;
		}
		// parent relation with child
		UA_NodeId referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
		// get property name
		QByteArray bytePropName = metaProperty.name();
		// get type node id
		UA_NodeId propTypeNodeId;
		if (!isEnum)
		{
			// check if available in meta-system
			const QMetaObject *propMetaObject = QMetaType(metaProperty.userType()).metaObject();
			if (!propMetaObject)
			{
				continue;
			}
			// check if OPC UA relevant type
			if (!propMetaObject->inherits(&QUaNode::staticMetaObject))
			{
				continue;
			}
			// check if prop inherits from parent
			Q_ASSERT_X(!propMetaObject->inherits(&metaObject),
				"QUaServer::addMetaProperties",
				"Qt MetaProperty type cannot inherit from Class.");
			if (propMetaObject->inherits(&metaObject) && !isEnum)
			{
				continue;
			}
			// check if prop type registered, register of not
			propTypeNodeId = this->typeIdByMetaObject(*propMetaObject);
			// set is variable
			isVariable = propMetaObject->inherits(&QUaBaseVariable::staticMetaObject);
			// check if ua property, then set correct reference
			if (propMetaObject->inherits(&QUaProperty::staticMetaObject))
			{
				referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
			}
		}
		else
		{
			propTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
		}
		Q_ASSERT(!UA_NodeId_isNull(&propTypeNodeId));
		// set qualified name, default is class name
		UA_QualifiedName browseName;
		// NOTE : use namespace 0 as default to allow QUaNode::browseChild 
		//        to work with simple strings on custom types
		browseName.namespaceIndex = 0;
		browseName.name = QUaTypesConverter::uaStringFromQString( QString::fromLatin1( bytePropName ) );
		// display name
		UA_LocalizedText displayName = UA_LOCALIZEDTEXT((char*)"", bytePropName.data());
		// check if variable or object
		// NOTE : a type is considered to inherit itself sometimes does not work 
		// (http://doc.qt.io/qt-5/qmetaobject.html#inherits)
		UA_NodeId tempNodeId;
		if (isVariable || isEnum)
		{

			// some types require the attrs to match because open62541 checks them
			UA_VariableAttributes vAttr = UA_VariableAttributes_default;
			auto st = UA_Server_readDataType(_server, propTypeNodeId, &vAttr.dataType);
			Q_ASSERT(st == UA_STATUSCODE_GOOD);
			Q_UNUSED(st);
			st = UA_Server_readValueRank(_server, propTypeNodeId, &vAttr.valueRank);
			Q_ASSERT(st == UA_STATUSCODE_GOOD);
			Q_UNUSED(st);
			UA_Variant outArrayDimensions;
			st = UA_Server_readArrayDimensions(_server, propTypeNodeId, &outArrayDimensions);
			Q_ASSERT(st == UA_STATUSCODE_GOOD);
			Q_UNUSED(st);
			vAttr.arrayDimensionsSize = outArrayDimensions.arrayLength;
			vAttr.arrayDimensions = static_cast<quint32*>(outArrayDimensions.data);
			// browse name
			vAttr.displayName = displayName;
			// if enum, set data type
			if (isEnum)
			{
				Q_ASSERT(!UA_NodeId_isNull(&enumTypeNodeId));
				vAttr.dataType = enumTypeNodeId;
			}
			// add variable
			st = UA_Server_addVariableNode(
				_server,
				UA_NODEID_NULL,   // requested nodeId
				parentTypeNodeId, // parent
				referenceTypeId,  // parent relation with child
				browseName,
				propTypeNodeId,
				vAttr,
				nullptr,          // context
				&tempNodeId       // output nodeId to make mandatory
			);
			Q_ASSERT(st == UA_STATUSCODE_GOOD);
			Q_UNUSED(st);
			UA_Variant_clear(&outArrayDimensions);
		}
		else
		{
			// NOTE : not working ! 
			// Q_ASSERT(propMetaObject.inherits(&QUaBaseObject::staticMetaObject));
			UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
			oAttr.displayName = displayName;
			// add object
			auto st = UA_Server_addObjectNode(
				_server,
				UA_NODEID_NULL,   // requested nodeId
				parentTypeNodeId, // parent
				referenceTypeId,  // parent relation with child
				browseName,
				propTypeNodeId,
				oAttr,
				nullptr,          // context
				&tempNodeId);     // output nodeId to make mandatory
			Q_ASSERT(st == UA_STATUSCODE_GOOD);
			Q_UNUSED(st);
		}
		// clean up
		UA_QualifiedName_clear(&browseName);
		// make mandatory
		auto st = UA_Server_addReference(_server,
			tempNodeId,
			UA_NODEID_NUMERIC(0, UA_NS0ID_HASMODELLINGRULE),
			UA_EXPANDEDNODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY),
			true);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
	}
}

void QUaServer::addMetaMethods(const QMetaObject& parentMetaObject)
{
	UA_NodeId parentTypeNodeId = this->typeIdByMetaObject(parentMetaObject);
	Q_ASSERT(!UA_NodeId_isNull(&parentTypeNodeId));
	// loop meta methods and find out which ones inherit from
	for (int methIdx = parentMetaObject.methodOffset(); methIdx < parentMetaObject.methodCount(); methIdx++)
	{
		QMetaMethod metaMethod = parentMetaObject.method(methIdx);
		// validate id method (not signal, slot or constructor)
		auto methodType = metaMethod.methodType();
		if (methodType != QMetaMethod::Method)
		{
			continue;
		}
		// validate return type
		auto returnType = (QMetaType::Type)metaMethod.returnType();
		bool isSupported = QUaTypesConverter::isSupportedQType(returnType);
		bool isEnumType = this->_hashEnums.contains( QString::fromUtf8(metaMethod.typeName()) );
		bool isArrayType = QUaTypesConverter::isQTypeArray(returnType);
		bool isValidType = isSupported || isEnumType || isArrayType;
		// NOTE : enums are QMetaType::UnknownType
		Q_ASSERT_X(isValidType,
			"QUaServer::addMetaMethods",
			"Return type not supported in MetaMethod.");
		if (!isValidType)
		{
			continue;
		}
		// if array
		if (isArrayType)
		{
			returnType = QUaTypesConverter::getQArrayType(returnType);
		}
		// create return type
		UA_Argument  outputArgumentInstance;
		UA_Argument* outputArgument = nullptr;
		if (returnType != QMetaType::Void)
		{
			UA_Argument_init(&outputArgumentInstance);
			outputArgumentInstance.description = UA_LOCALIZEDTEXT((char*)"",
				(char*)"Result Value");
			outputArgumentInstance.name = QUaTypesConverter::uaStringFromQString( QStringLiteral("Result") );
			outputArgumentInstance.dataType = isEnumType ?
				this->_hashEnums.value(QString::fromUtf8(metaMethod.typeName())) :
				QUaTypesConverter::uaTypeNodeIdFromQType(returnType);
			outputArgumentInstance.valueRank = isArrayType ?
				UA_VALUERANK_ONE_DIMENSION :
				UA_VALUERANK_SCALAR;
			outputArgument = &outputArgumentInstance;
		}
		// validate argument types and create them
		Q_ASSERT_X(metaMethod.parameterCount() <= 10,
			"QUaServer::addMetaMethods",
			"No more than 10 arguments supported in MetaMethod.");
		if (metaMethod.parameterCount() > 10)
		{
			continue;
		}
		QVector<UA_Argument> vectArgs;
		auto listArgNames = metaMethod.parameterNames();
		Q_ASSERT(listArgNames.count() == metaMethod.parameterCount());
		auto listTypeNames = metaMethod.parameterTypes();
		for (int k = 0; k < metaMethod.parameterCount(); k++)
		{
			auto argType = (QMetaType::Type)metaMethod.parameterType(k);
			isSupported = QUaTypesConverter::isSupportedQType(argType);
			isEnumType  = this->_hashEnums.contains( QString::fromUtf8(listTypeNames[k]) );
			isArrayType = QUaTypesConverter::isQTypeArray(argType);
			isValidType = isSupported || isEnumType || isArrayType;
			// NOTE : enums are QMetaType::UnknownType
			Q_ASSERT_X(isValidType,
				"QUaServer::addMetaMethods",
				"Argument type not supported in MetaMethod.");
			if (!isValidType)
			{
				break;
			}
			// check if array
			if (isArrayType)
			{
				argType = QUaTypesConverter::getQArrayType(argType);
			}
			// get ua type
			UA_Argument inputArgument;
			UA_Argument_init(&inputArgument);
			// check if type is registered enum
			UA_NodeId uaType = isEnumType ?
				this->_hashEnums.value( QString::fromUtf8(listTypeNames[k]) ) :
				QUaTypesConverter::uaTypeNodeIdFromQType(argType);
			// create n-th argument
			inputArgument.description = UA_LOCALIZEDTEXT((char*)"", (char*)"Method Argument");
			inputArgument.name = QUaTypesConverter::uaStringFromQString( QString::fromUtf8(listArgNames[k]) );
			inputArgument.dataType = uaType;
			inputArgument.valueRank = UA_VALUERANK_SCALAR;
			if (isArrayType)
			{
				inputArgument.valueRank = UA_VALUERANK_ONE_DIMENSION;
			}
			vectArgs.append(inputArgument);
		}
		// skip if any arg is not supported
		if (!isValidType)
		{
			continue;
		}
		// add method
		auto strMethName = metaMethod.name();
		// add method node
		UA_MethodAttributes methAttr = UA_MethodAttributes_default;
		methAttr.executable = true;
		methAttr.userExecutable = true;
		methAttr.description = UA_LOCALIZEDTEXT((char*)"",
			strMethName.data());
		methAttr.displayName = UA_LOCALIZEDTEXT((char*)"",
			strMethName.data());
		// create callback
		UA_NodeId methNodeId;
		auto st = UA_Server_addMethodNode(
			this->_server,
			UA_NODEID_NULL,
			parentTypeNodeId,
			UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
			UA_QUALIFIEDNAME(1, strMethName.data()),
			methAttr,
			&QUaServer::methodCallback,
			metaMethod.parameterCount(),
			vectArgs.data(),
			outputArgument ? 1 : 0,
			outputArgument,
			this, // context is server instance that has _hashMethods
			&methNodeId
		);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
		// Define "StartPump" method mandatory
		st = UA_Server_addReference(
			this->_server,
			methNodeId,
			UA_NODEID_NUMERIC(0, UA_NS0ID_HASMODELLINGRULE),
			UA_EXPANDEDNODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY),
			true
		);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
		// store method with node id hash as key
		Q_ASSERT_X(!_hashMethods.contains(methNodeId),
			"QUaServer::addMetaMethods",
			"Method already exists, callback will be overwritten.");
		// NOTE : had to cache the method's index in lambda capture because caching 
		//        metaMethod directly was not working in some cases the internal data
		//        of the metaMethod was deleted which resulted in access violation
		_hashMethods[methNodeId] = [methIdx, this](
			void* objectContext,
			const UA_Variant* input,
			UA_Variant* output)
		{
			// get object instance that owns method
#ifdef QT_DEBUG 
			QUaBaseObject* object = qobject_cast<QUaBaseObject*>(static_cast<QObject*>(objectContext));
			Q_ASSERT_X(object,
				"QUaServer::addMetaMethods",
				"Cannot call method on invalid C++ object.");
#else
			QUaBaseObject* object = static_cast<QUaBaseObject*>(objectContext);
#endif // QT_DEBUG 
			if (!object)
			{
				return (UA_StatusCode)UA_STATUSCODE_BADUNEXPECTEDERROR;
			}
			// get meta method
			auto metaMethod = object->metaObject()->method(methIdx);
			return QUaServer::callMetaMethod(this, object, metaMethod, input, output);
		};
	}
}

// NOTE : do not clean
UA_NodeId QUaServer::typeIdByMetaObject(const QMetaObject& metaObject)
{
	QString   strClassName = QString::fromUtf8(metaObject.className());
	UA_NodeId typeNodeId   = _mapTypes.value(strClassName, UA_NODEID_NULL);
	if (UA_NodeId_isNull(&typeNodeId))
	{
		this->registerTypeInternal(metaObject);
		typeNodeId = _mapTypes.value(strClassName, UA_NODEID_NULL);
	}
	return typeNodeId;
}

UA_NodeId QUaServer::createInstanceInternal(
	const QMetaObject& metaObject,
	QUaNode* parentNode,
	const QUaQualifiedName& browseName,
	const QUaNodeId& nodeId
)
{
	// check if OPC UA relevant
	if (!metaObject.inherits(&QUaNode::staticMetaObject))
	{
		Q_ASSERT_X(false, "QUaServer::createInstance",
			"Unsupported base class. It must derive from QUaNode");
		return UA_NODEID_NULL;
	}
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	// check if inherits BaseEventType, in which case this method cannot be used
	if (metaObject.inherits(&QUaBaseEvent::staticMetaObject) 
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
		&& !metaObject.inherits(&QUaCondition::staticMetaObject)
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
		)
	{
		Q_ASSERT_X(false, "QUaServer::createInstanceInternal",
			"Cannot use createInstance to create Non-Condition Events. Use createEvent method instead");
		return UA_NODEID_NULL;
	}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
	// try to get typeNodeId, if null, then register it
	UA_NodeId typeNodeId = this->typeIdByMetaObject(metaObject);
	Q_ASSERT(!UA_NodeId_isNull(&typeNodeId));
	// adapt parent relation with child according to parent type
	// NOTE : parent can be null (no address space representation, e.g. events, conditions)
	UA_NodeId referenceTypeId = parentNode ?
		QUaServer::getReferenceTypeId(*parentNode->metaObject(), metaObject) :
		UA_NODEID_NULL;
	// check if browse name already used with parent (if any parent)
	if (parentNode && parentNode->hasChild(browseName))
	{
		Q_ASSERT_X(false, "QUaServer::createInstance", "Requested BrowseName already exists in parent");
		return UA_NODEID_NULL;
	}
	UA_QualifiedName uaBrowseName = browseName;
	// check if requested node id defined
	if (!nodeId.isNull())
	{
		// check if requested node id exists
		bool isUsed = this->isNodeIdUsed(nodeId);
		Q_ASSERT_X(!isUsed, "QUaServer::createInstance", "Requested NodeId already exists");
		if (isUsed)
		{
			return UA_NODEID_NULL;
		}
	}
	// NOTE : calling UA_Server_addXXX below will trigger QUaServer::uaConstructor
	// which will instantiate the respective Qt instance and binding
	UA_NodeId reqNodeId = nodeId;
	UA_NodeId nodeIdNewInstance;
	// check if variable or object 
	// NOTE : a type is considered to inherit itself 
	// (http://doc.qt.io/qt-5/qmetaobject.html#inherits)
	Q_ASSERT(parentNode ? !UA_NodeId_isNull(&parentNode->_nodeId) : true);
	if (metaObject.inherits(&QUaBaseVariable::staticMetaObject))
	{
		// some types require the attrs to match because open62541 checks them
		UA_VariableAttributes vAttr = UA_VariableAttributes_default;
		auto st = UA_Server_readDataType(_server, typeNodeId, &vAttr.dataType);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
		st = UA_Server_readValueRank(_server, typeNodeId, &vAttr.valueRank);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
		UA_Variant outArrayDimensions;
		st = UA_Server_readArrayDimensions(_server, typeNodeId, &outArrayDimensions);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
		vAttr.arrayDimensionsSize = outArrayDimensions.arrayLength;
		vAttr.arrayDimensions = static_cast<quint32*>(outArrayDimensions.data);
		// default displayName is browseName
		QByteArray byteDisplayName = browseName.name().toUtf8();
		vAttr.displayName = UA_LOCALIZEDTEXT((char*)"", byteDisplayName.data());
		// add variable
        st = UA_Server_addVariableNode(_server,
			reqNodeId,            // requested nodeId
			parentNode ? parentNode->_nodeId : UA_NODEID_NULL, // parent (can be null)
			referenceTypeId,      // parent relation with child
			uaBrowseName,
			typeNodeId,
			vAttr,
			nullptr,             // context
			&nodeIdNewInstance); // set new nodeId to new instance
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
		UA_Variant_clear(&outArrayDimensions);
	}
	else
	{
		Q_ASSERT(metaObject.inherits(&QUaBaseObject::staticMetaObject) ||
			metaObject.className() == QUaBaseObject::staticMetaObject.className());
		UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
		// default displayName is browseName
		QByteArray byteDisplayName = browseName.name().toUtf8();
		oAttr.displayName = UA_LOCALIZEDTEXT((char*)"", byteDisplayName.data());
		// add object
		auto st = UA_Server_addObjectNode(_server,
			reqNodeId,            // requested nodeId
			parentNode ? parentNode->_nodeId : UA_NODEID_NULL, // parent
			referenceTypeId,      // parent relation with child
			uaBrowseName,
			typeNodeId,
			oAttr,
			nullptr,             // context
			&nodeIdNewInstance); // set new nodeId to new instance
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
	}
	Q_ASSERT_X(!UA_NodeId_isNull(&nodeIdNewInstance), "QUaServer::createInstanceInternal", "Something went wrong");
	// clean up
	UA_NodeId_clear(&reqNodeId);
	// NOTE : do not UA_NodeId_clear(&typeNodeId); or value in _mapTypes gets corrupted
	UA_NodeId_clear(&referenceTypeId);
	UA_QualifiedName_clear(&uaBrowseName);

	// if child is condition, add non-hierarchical reference and default props
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	if (parentNode && metaObject.inherits(&QUaCondition::staticMetaObject))
	{
		// add HasCondition reference
		auto node = QUaNode::getNodeContext(nodeIdNewInstance, this->_server);
		auto condition = qobject_cast<QUaCondition*>(node);
		Q_CHECK_PTR(condition);
		// set default originator
		condition->QUaBaseEvent::setSourceNode(parentNode);
	}
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

	// trigger reference added, model change event, so client (UaExpert) auto refreshes tree
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	if (parentNode && parentNode->inAddressSpace())
	{
		Q_CHECK_PTR(_changeEvent);
		// add reference added change to buffer
		this->addChange({
			parentNode->nodeId(),
			parentNode->typeDefinitionNodeId(),
			QUaChangeVerb::ReferenceAdded // UaExpert does not recognize QUaChangeVerb::NodeAdded
		});
	}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

	// return new instance node id
	return nodeIdNewInstance;
}

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS

UA_NodeId QUaServer::createEventInternal(
	const QMetaObject& metaObject
)
{
	// check if derives from event
	if (!metaObject.inherits(&QUaBaseEvent::staticMetaObject))
	{
		Q_ASSERT_X(false, "QUaServer::createEvent",
			"Unsupported event class. It must derive from QUaBaseEvent");
		return UA_NODEID_NULL;
	}
	// try to get typeEvtId, if null, then register it
	UA_NodeId typeEvtId = this->typeIdByMetaObject(metaObject);
	Q_ASSERT(!UA_NodeId_isNull(&typeEvtId));
	// create event instance
	// NOTE : event instances have no parent (same as UA_Server_createEvent in open62541 v1.2)
	UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
	UA_NodeId nodeIdNewEvent = UA_NODEID_NULL;
	auto st = UA_Server_addObjectNode(
		_server,
		UA_NODEID_NULL,
		UA_NODEID_NULL,
		UA_NODEID_NULL,
		UA_QUALIFIEDNAME(0, (char*)"E"),
		typeEvtId,
		oAttr,
		nullptr,
		&nodeIdNewEvent
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// set the EventType property
	UA_QualifiedName eventTypeName = UA_QUALIFIEDNAME(0, (char*)"EventType");
	UA_BrowsePathResult bpr = UA_Server_browseSimplifiedBrowsePath(_server, nodeIdNewEvent, 1, &eventTypeName);
	if (bpr.statusCode == UA_STATUSCODE_GOOD && bpr.targetsSize > 0)
	{
		UA_Variant value;
		UA_Variant_setScalar(&value, &typeEvtId, &UA_TYPES[UA_TYPES_NODEID]);
		st = UA_Server_writeValue(_server, bpr.targets[0].targetId.nodeId, value);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
	}
	UA_BrowsePathResult_clear(&bpr);
	Q_UNUSED(st);
	// return new instance node id
	return nodeIdNewEvent;
}

#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

void QUaServer::bindCppInstanceWithUaNode(QUaNode* nodeInstance, UA_NodeId& nodeId)
{
	Q_CHECK_PTR(nodeInstance);
	Q_ASSERT(!UA_NodeId_isNull(&nodeId));
	// set c++ instance as context
	UA_Server_setNodeContext(_server, nodeId, (void**)(&nodeInstance));
	// set node id to c++ instance
	if (!UA_NodeId_equal(&nodeInstance->_nodeId, &nodeId))
	{
		UA_NodeId_clear(&nodeInstance->_nodeId);
		UA_NodeId_copy(&nodeId, &nodeInstance->_nodeId);
	}
}

bool QUaServer::isMetaObjectRegistered(const QString& strClassName) const
{
	return _hashMetaObjects.contains(strClassName);
}

QMetaObject QUaServer::getRegisteredMetaObject(const QString& strClassName) const
{
	Q_ASSERT_X(_hashMetaObjects.contains(strClassName), "QUaServer::getRegisteredMetaObject", "Class is not registered.");
	return _hashMetaObjects.value(strClassName);
}

bool QUaServer::registerReferenceType(const QUaReferenceType& refType, const QUaNodeId& nodeId/* = ""*/)
{
	// first check if already registered
	if (_hashRefTypes.contains(refType))
	{
		return true;
	}
	// check if requested node id defined
	if (!nodeId.isNull())
	{
		// check if requested node id exists
		bool isUsed = this->isNodeIdUsed(nodeId);
		Q_ASSERT_X(!isUsed, "QUaServer::registerReferenceType", "Requested NodeId already exists");
		if (isUsed)
		{
			return false;
		}
	}
	// get namea and stuff
	QByteArray byteForwardName = refType.strForwardName.toUtf8();
	QByteArray byteInverseName = refType.strInverseName.toUtf8();
	// TODO : Use QUaQualifiedName, but then need to change how refsare serialized
	UA_QualifiedName browseName;
	browseName.namespaceIndex = 0;
	browseName.name = QUaTypesConverter::uaStringFromQString(refType.strForwardName);
	// setup new ref type attributes
	UA_ReferenceTypeAttributes refattr = UA_ReferenceTypeAttributes_default;
	refattr.displayName = UA_LOCALIZEDTEXT((char*)(""), byteForwardName.data());
	refattr.inverseName = UA_LOCALIZEDTEXT((char*)(""), byteInverseName.data());
	UA_NodeId reqNodeId = nodeId;
	UA_NodeId outNewNodeId;
	auto st = UA_Server_addReferenceTypeNode(
		_server,
		reqNodeId,
		UA_NODEID_NUMERIC(0, UA_NS0ID_NONHIERARCHICALREFERENCES),
		UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE),
		browseName,
		refattr,
		nullptr,
		&outNewNodeId
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// clean up
	UA_NodeId_clear(&reqNodeId);
	UA_QualifiedName_clear(&browseName);
	// add to hash to complete registration
	_hashRefTypes.insert(refType, outNewNodeId);
	return true;
}

const QList<QUaReferenceType> QUaServer::referenceTypes() const
{
	return _hashRefTypes.keys();
}

bool QUaServer::referenceTypeRegistered(const QUaReferenceType& refType) const
{
	return _hashRefTypes.contains(refType);
}

bool QUaServer::isNodeIdUsed(const QUaNodeId& nodeId) const
{
	// check if requested node id exists
	UA_NodeId reqNodeId  = nodeId;
	UA_NodeId testNodeId = nodeId;
	auto st = UA_Server_readNodeId(_server, reqNodeId, &testNodeId);
	UA_NodeId_clear(&reqNodeId);
	UA_NodeId_clear(&testNodeId);
	return st == UA_STATUSCODE_GOOD;
}

QUaFolderObject* QUaServer::objectsFolder() const
{
	return _pobjectsFolder;
}

QUaNode* QUaServer::nodeById(const QUaNodeId& nodeIdIn)
{
	UA_NodeId nodeId = nodeIdIn;
	QUaNode* node = QUaNode::getNodeContext(nodeId, _server);
	UA_NodeId_clear(&nodeId);
	return node;
}

bool QUaServer::isTypeNameRegistered(const QString& strTypeName) const
{
	return _mapTypes.contains(strTypeName);
}

QUaNode * QUaServer::browsePath(const QUaBrowsePath& browsePath) const
{
	if (browsePath.count() <= 0)
	{
		return nullptr;
	}
	QUaQualifiedName first = browsePath.first();
	// check if first is ObjectsFolder
	if (first == this->objectsFolder()->browseName())
	{
		return this->objectsFolder()->browsePath(browsePath.mid(1));
	}
	// then check if first is a child of ObjectsFolder
	const auto listChildren = this->objectsFolder()->browseChildren();
	for (auto child : listChildren)
	{
		if (first == child->browseName())
		{
			return child->browsePath(browsePath.mid(1));
		}
	}
	// if not, then not supported
	return nullptr;
}

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
#ifdef UA_ENABLE_HISTORIZING

bool QUaServer::eventHistoryRead() const
{
	QUaEventNotifier eventNotifier;
	eventNotifier.intValue = this->eventNotifier();
	return eventNotifier.bits.bHistoryRead;
}

void QUaServer::setEventHistoryRead(const bool& eventHistoryRead)
{
	QUaEventNotifier eventNotifier;
	eventNotifier.intValue = this->eventNotifier();
	eventNotifier.bits.bHistoryRead = eventHistoryRead;
	this->setEventNotifier(eventNotifier.intValue);
}

quint64 QUaServer::maxHistoryEventResponseSize() const
{
	return _maxHistoryEventResponseSize;
}

void QUaServer::setMaxHistoryEventResponseSize(const quint64& maxHistoryEventResponseSize)
{
	_maxHistoryEventResponseSize = maxHistoryEventResponseSize;
}

#endif // UA_ENABLE_HISTORIZING
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

bool QUaServer::anonymousLoginAllowed() const
{
	return _anonymousLoginAllowed;
}

void QUaServer::setAnonymousLoginAllowed(const bool & anonymousLoginAllowed)
{
	_anonymousLoginAllowed = anonymousLoginAllowed;
	emit this->anonymousLoginAllowedChanged(_anonymousLoginAllowed);
}

void QUaServer::addUser(const QString & strUserName, const QString & strKey)
{
	if (strUserName.isEmpty())
	{
		return;
	}
	_hashUsers[strUserName] = strKey;
}

void QUaServer::removeUser(const QString & strUserName)
{
	if (strUserName.isEmpty())
	{
		return;
	}
	_hashUsers.remove(strUserName);
}

QString QUaServer::userKey(const QString & strUserName) const
{
	return _hashUsers.value(strUserName, QString());
}

int QUaServer::userCount()
{
	return _hashUsers.count();
}

QStringList QUaServer::userNames() const
{
	return _hashUsers.keys();
}

#ifdef UA_ENABLE_ENCRYPTION
///
/// \brief Lets clients authenticate users with X.509 certificates, mapped to user names by \a callback.
///        The certificate must also pass the trusted certificates lists. Only offered while the server
///        has a private key; call without arguments to disable it. Applied on the next start().
///
void QUaServer::setUserCertificateCallback(const QUaUserCertificateCallback& callback)
{
	_userCertificateCallback = callback;
}
#endif // UA_ENABLE_ENCRYPTION

bool QUaServer::userExists(const QString & strUserName) const
{
	Q_ASSERT(!strUserName.isEmpty());
	if (strUserName.isEmpty())
	{
		return false;
	}
#ifdef UA_ENABLE_ENCRYPTION
	if (_certificateUsers.contains(strUserName))
	{
		return true;
	}
#endif // UA_ENABLE_ENCRYPTION
	return _anonUsers.contains(strUserName) || _hashUsers.contains(strUserName);
}

QList<const QUaSession*> QUaServer::sessions() const
{
    QList<const QUaSession*> listConstSessions;
    const auto hashSessions = _hashSessions.values();
    for (auto session : hashSessions)
    {
        listConstSessions << session;
    }
    return listConstSessions;
}

UA_NodeId QUaServer::getReferenceTypeId(const QMetaObject & parentMetaObject, const QMetaObject & childMetaObject)
{
	UA_NodeId referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
	// adapt parent relation with child according to parent type
	if (parentMetaObject.inherits(&QUaFolderObject::staticMetaObject))
	{
		referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
	}
	else if (parentMetaObject.inherits(&QUaBaseObject::staticMetaObject) ||
		     parentMetaObject.inherits(&QUaBaseDataVariable::staticMetaObject))
	{
		if (childMetaObject.inherits(&QUaBaseObject::staticMetaObject) || 
			childMetaObject.inherits(&QUaBaseDataVariable::staticMetaObject))
		{
			referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);
		}
		else if (childMetaObject.inherits(&QUaProperty::staticMetaObject))
		{
			referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
		}
	}
	else
	{
		Q_ASSERT_X(false, "QUaServer::getReferenceTypeId", "Invalid parent type.");
	}
	return referenceTypeId;
}
