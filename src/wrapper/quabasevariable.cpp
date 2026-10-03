#include "quabasevariable.h"

#include "quaserver_anex.h"
#include <QUaBaseDataVariable>

#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
#ifndef OPEN62541_ISSUE3934_RESOLVED
// NOTE : added this ugly workaround that passes the "same datatypes" condition in
// server/ua_services_attribute.c::compatibleDataType so we can actually set the value
// of an optionset satatype
QHash<UA_NodeId, UA_DataType*> mapOptionSetDatatypes;
UA_DataType* getDataTypeFromNodeId(const UA_NodeId& optNodeId)
{
	if (mapOptionSetDatatypes.contains(optNodeId))
	{
		return mapOptionSetDatatypes[optNodeId];
	}
	// copy of the builtin OptionSet type with the custom type id
	UA_DataType* tmpType = new UA_DataType(UA_TYPES[UA_TYPES_OPTIONSET]);
	tmpType->typeId = optNodeId;
	mapOptionSetDatatypes[optNodeId] = tmpType;
	return tmpType;
}
#endif // !OPEN62541_ISSUE3934_RESOLVED
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL

// [STATIC] : always called by open62541 library after a write, used by QUaServer 
// to emit signals and to make differentiation between network or programmatic value change
void QUaBaseVariable::onWrite(UA_Server             *server, 
		                      const UA_NodeId       *sessionId,
		                      void                  *sessionContext, 
		                      const UA_NodeId       *nodeId,
		                      void                  *nodeContext, 
		                      const UA_NumericRange *range,
		                      const UA_DataValue    *data)
{
	Q_UNUSED(sessionContext);
	Q_UNUSED(nodeId);
	Q_UNUSED(range);
	// get variable from context
#ifdef QT_DEBUG 
	auto var = qobject_cast<QUaBaseVariable*>(static_cast<QObject*>(nodeContext));
	Q_CHECK_PTR(var);
#else
	auto var = static_cast<QUaBaseVariable*>(nodeContext);
#endif // QT_DEBUG 
	if (!var)
	{
		return;
	}
	// get server
	void* serverContext = nullptr;
	auto st = UA_Server_getNodeContext(server, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER), &serverContext);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
#ifdef QT_DEBUG 
	auto srv = qobject_cast<QUaServer*>(static_cast<QObject*>(serverContext));
	Q_CHECK_PTR(srv);
#else
	auto srv = static_cast<QUaServer*>(serverContext);
#endif // QT_DEBUG 
	// check session (triggering events create internal writes with no session)
	// NOTE : sometimes happens that !srv->m_hashSessions.contains(*sessionId)
	srv->m_currentSession = srv->m_hashSessions.contains(*sessionId) ?
		srv->m_hashSessions[*sessionId] : nullptr;
	var->emitWriteSignals(*data);
}

///
/// \brief Emits the change signals of a written value, flagged as network change unless setValue() wrote it.
///
void QUaBaseVariable::emitWriteSignals(const UA_DataValue& data)
{
	// do not process if nobody listening
	static const QMetaMethod valueSignal = QMetaMethod::fromSignal(&QUaBaseVariable::valueChanged);
	if (this->isSignalConnected(valueSignal))
	{
		emit this->valueChanged(this->value(), !m_bInternalWrite);
	}
	static const QMetaMethod statusSignal = QMetaMethod::fromSignal(&QUaBaseVariable::statusCodeChanged);
	if (data.hasStatus && this->isSignalConnected(statusSignal))
	{
		emit this->statusCodeChanged(QUaStatusCode(data.status), !m_bInternalWrite);
	}
	static const QMetaMethod sourceSignal = QMetaMethod::fromSignal(&QUaBaseVariable::sourceTimestampChanged);
	if (data.hasSourceTimestamp && this->isSignalConnected(sourceSignal))
	{
		emit this->sourceTimestampChanged(
			QUaTypesConverter::uaVariantToQVariantScalar
				<QDateTime, UA_DateTime>(&data.sourceTimestamp),
			!m_bInternalWrite
		);
	}
	static const QMetaMethod serverSignal = QMetaMethod::fromSignal(&QUaBaseVariable::serverTimestampChanged);
	if (data.hasServerTimestamp && this->isSignalConnected(serverSignal))
	{
		emit this->serverTimestampChanged(
			QUaTypesConverter::uaVariantToQVariantScalar
				<QDateTime, UA_DateTime>(&data.serverTimestamp),
			!m_bInternalWrite
		);
	}
	m_bInternalWrite = false;
}

// [STATIC] : Optionally set be the user (m_readCallback). Called before a value is requested by open62541.
// Use case is when the value is computed base don other values and it makes sense to only
// compute it when requested
void QUaBaseVariable::onRead(
	UA_Server             *server, 
	const UA_NodeId       *sessionId,
	void                  *sessionContext, 
	const UA_NodeId       *nodeId,
	void                  *nodeContext, 
	const UA_NumericRange *range,
	const UA_DataValue    *data
)
{
	Q_UNUSED(sessionContext);
	Q_UNUSED(nodeId);
	Q_UNUSED(range);
	Q_UNUSED(data);
	// get server
	void* serverContext = nullptr;
	auto st = UA_Server_getNodeContext(server, UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER), &serverContext);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
#ifdef QT_DEBUG 
	auto srv = qobject_cast<QUaServer*>(static_cast<QObject*>(serverContext));
	Q_CHECK_PTR(srv);
#else
	auto srv = static_cast<QUaServer*>(serverContext);
#endif // QT_DEBUG 
	// local reads (UA_Server_read) come from the admin session, which is not a client session
	srv->m_currentSession = srv->m_hashSessions.contains(*sessionId) ?
		srv->m_hashSessions[*sessionId] : nullptr;
	// get variable from context
#ifdef QT_DEBUG 
	auto var = qobject_cast<QUaBaseVariable*>(static_cast<QObject*>(nodeContext));
	Q_CHECK_PTR(var);
#else
	auto var = static_cast<QUaBaseVariable*>(nodeContext);
#endif // QT_DEBUG 
	if (!var)
	{
		return;
	}
	var->runReadCallback();
}

///
/// \brief Stores the value returned by the user read callback, if any.
///
void QUaBaseVariable::runReadCallback()
{
	if (!m_readCallback || m_readCallbackRunning) return;
	// setValue (somehow) triggers read callback again; this avoids recursion
	QVariant newValue = m_readCallback();
	if (!newValue.isNull())
	{
		m_readCallbackRunning = true;
		this->setValue(newValue);
		m_readCallbackRunning = false;
	}
}

///
/// \brief [STATIC] Serves reads from m_callbackSourceValue while a write validator is set.
///
UA_StatusCode QUaBaseVariable::readValueSource(
	UA_Server             *server,
	const UA_NodeId       *sessionId,
	void                  *sessionContext,
	const UA_NodeId       *nodeId,
	void                  *nodeContext,
	UA_Boolean             includeSourceTimeStamp,
	const UA_NumericRange *range,
	UA_DataValue          *value)
{
	Q_UNUSED(server);
	Q_UNUSED(sessionContext);
	Q_UNUSED(nodeId);
	Q_UNUSED(includeSourceTimeStamp);
	auto var = static_cast<QUaBaseVariable*>(nodeContext);
	Q_CHECK_PTR(var);
	QUaServer* srv = var->m_qUaServer;
	srv->m_currentSession = sessionId && srv->m_hashSessions.contains(*sessionId) ?
		srv->m_hashSessions[*sessionId] : nullptr;
	var->runReadCallback();
	return range ?
		UA_DataValue_copyRange(&var->m_callbackSourceValue, value, *range) :
		UA_DataValue_copy(&var->m_callbackSourceValue, value);
}

///
/// \brief [STATIC] Validates a client write before storing it in m_callbackSourceValue.
/// \return The validator result when it rejects the write, which open62541 returns to the client.
///
UA_StatusCode QUaBaseVariable::writeValueSource(
	UA_Server             *server,
	const UA_NodeId       *sessionId,
	void                  *sessionContext,
	const UA_NodeId       *nodeId,
	void                  *nodeContext,
	const UA_NumericRange *range,
	const UA_DataValue    *value)
{
	Q_UNUSED(server);
	Q_UNUSED(nodeId);
	auto var = static_cast<QUaBaseVariable*>(nodeContext);
	Q_CHECK_PTR(var);
	QUaServer* srv = var->m_qUaServer;
	srv->m_currentSession = srv->m_hashSessions.contains(*sessionId) ?
		srv->m_hashSessions[*sessionId] : nullptr;
	UA_DataValue newValue;
	UA_StatusCode st = UA_DataValue_copy(range ? &var->m_callbackSourceValue : value, &newValue);
	if (st == UA_STATUSCODE_GOOD && range)
	{
		// same partial write semantics as the open62541 internal value source
		st = UA_Variant_setRangeCopy(&newValue.value, value->value.data, value->value.arrayLength, *range);
		newValue.hasStatus            = value->hasStatus;
		newValue.status               = value->status;
		newValue.hasSourceTimestamp   = value->hasSourceTimestamp;
		newValue.sourceTimestamp      = value->sourceTimestamp;
		newValue.hasSourcePicoseconds = value->hasSourcePicoseconds;
		newValue.sourcePicoseconds    = value->sourcePicoseconds;
	}
	// the admin session (local API) is marked with the server as context, any other writer is a client
	if (st == UA_STATUSCODE_GOOD && sessionContext != srv && var->m_writeValidator)
	{
		st = var->m_writeValidator(
			QUaTypesConverter::uaVariantToQVariant(newValue.value),
			srv->m_currentSession
		);
		if (!UA_StatusCode_isBad(st))
		{
			st = UA_STATUSCODE_GOOD;
		}
	}
	if (st != UA_STATUSCODE_GOOD)
	{
		UA_DataValue_clear(&newValue);
		return st;
	}
	UA_DataValue_clear(&var->m_callbackSourceValue);
	var->m_callbackSourceValue = newValue;
	var->emitWriteSignals(var->m_callbackSourceValue);
	return UA_STATUSCODE_GOOD;
}

QUaBaseVariable::QUaBaseVariable(
	QUaServer* server
) : QUaNode(server)
{
	UA_DataValue_init(&m_callbackSourceValue);
	// [NOTE] : constructor of any QUaNode-derived class is not meant to be called by the user
	//          the constructor is called automagically by this library, and m_newNodeNodeId and
	//          m_newNodeMetaObject must be set in QUaServer before calling the constructor, as
	//          is used in QUaServer::uaConstructor
	Q_CHECK_PTR(server);
	Q_CHECK_PTR(server->m_newNodeNodeId);
	m_dataType = this->dataTypeInternal();
	// this should not be needed since stored in m_nodeId already??:
	//    const UA_NodeId &nodeId = *server->m_newNodeNodeId;
	// sets also write callback to emit onWrite signal
	setReadCallback();
#ifdef UA_ENABLE_HISTORIZING
	m_maxHistoryDataResponseSize = 1000;
#endif // UA_ENABLE_HISTORIZING
}

QUaBaseVariable::~QUaBaseVariable()
{
	UA_DataValue_clear(&m_callbackSourceValue);
}

void QUaBaseVariable::setReadCallback(const std::function<QVariant()>& readCallback){
	m_readCallback = readCallback;
	m_readCallbackRunning = false;
	this->applyValueSource();
}

///
/// \brief Sets a callback that validates every client write before it is applied; local writes are not validated.
///        Call with the default argument to remove it. The validator receives the complete resulting value,
///        also when the client writes only an index range.
///
void QUaBaseVariable::setWriteValidator(const QUaWriteValidator& validator)
{
	m_writeValidator = validator;
	this->applyValueSource();
}

///
/// \brief Moves the value to a callback value source while a write validator is set, since only its write
///        callback can reject a write; otherwise keeps it in the node with read and write notifications.
///
void QUaBaseVariable::applyValueSource()
{
	UA_Server* server = m_qUaServer->m_server;
	UA_StatusCode st = UA_STATUSCODE_GOOD;
	if (m_writeValidator)
	{
		if (m_bValueInCallbackSource)
		{
			return;
		}
		UA_ReadValueId rv;
		UA_ReadValueId_init(&rv);
		rv.nodeId      = m_nodeId;
		rv.attributeId = UA_ATTRIBUTEID_VALUE;
		UA_DataValue_clear(&m_callbackSourceValue);
		m_callbackSourceValue = UA_Server_read(server, &rv, UA_TIMESTAMPSTORETURN_SOURCE);
		// the Read service flags an unset value as present, which would block later data type changes
		m_callbackSourceValue.hasValue = !UA_Variant_isEmpty(&m_callbackSourceValue.value);
		UA_CallbackValueSource source;
		source.read  = &QUaBaseVariable::readValueSource;
		source.write = &QUaBaseVariable::writeValueSource;
		st = UA_Server_setVariableNode_callbackValueSource(server, m_nodeId, source);
		m_bValueInCallbackSource = true;
	}
	else
	{
		UA_ValueSourceNotifications notifications;
		notifications.onRead  = m_readCallback ? &QUaBaseVariable::onRead : nullptr;
		notifications.onWrite = &QUaBaseVariable::onWrite;
		// a null value keeps the current internal value
		st = UA_Server_setVariableNode_internalValueSource(server, m_nodeId,
			m_bValueInCallbackSource ? &m_callbackSourceValue : nullptr, &notifications);
		UA_DataValue_clear(&m_callbackSourceValue);
		m_bValueInCallbackSource = false;
	}
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
}

QVariant QUaBaseVariable::value() const
{
	return this->getValueInternal();
}

QVariant QUaBaseVariable::getValueInternal(
	const QUaTypesConverter::ArrayType& arrType
	 /* = QUaTypesConverter::ArrayType::QList*/
) const
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	if (UA_NodeId_isNull(&m_nodeId))
	{
		return QVariant();
	}
	// get value
	UA_ReadValueId rv;
	UA_ReadValueId_init(&rv);
	rv.nodeId      = m_nodeId;
	rv.attributeId = UA_ATTRIBUTEID_VALUE;
	UA_DataValue value = UA_Server_read(
		m_qUaServer->m_server,
		&rv,
		UA_TIMESTAMPSTORETURN_NEITHER
	);
	// convert
	QVariant outVar = QUaTypesConverter::uaVariantToQVariant(value.value, arrType);
	// clenaup
	UA_DataValue_clear(&value);
	return outVar;
}

void QUaBaseVariable::setValue(
	const QVariant        &value, 
	const QUaStatusCode   &statusCode      /*QUaStatus::Good*/,
	const QDateTime       &sourceTimestamp /*= QDateTime()*/,
	const QDateTime       &serverTimestamp /*= QDateTime()*/,
	const QMetaType::Type &newTypeConst    /*= QMetaType::UnknownType*/
)
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	// get types
#define oldType m_dataType
	// get modifiable copies
	auto newValue = value;
	auto newType  = newTypeConst;

	// if new type not forced, then figure out new type from input
	if (newType == QMetaType::UnknownType)
	{
		bool isArray = QUaTypesConverter::canConvertQVariantList(value);
		if (isArray)
		{
			auto iter = value.value<QSequentialIterable>();
			if (iter.size() > 0)
			{
				QVariant innerVar = iter.at(0);
				newType = static_cast<QMetaType::Type>( innerVar.typeId() );
				if (newType == QMetaType::User) newType = static_cast<QMetaType::Type>( innerVar.userType() );
			}
			else newType = QUaTypesConverter::getQArrayType( value.typeName() );
		}
		// if scalar
		else
		{
			newType = static_cast<QMetaType::Type>( value.typeId() );
			if (newType == QMetaType::User) newType = static_cast<QMetaType::Type>( value.userType() );
		}

		// if new type different from old type, try to keep old type
		if (newType != oldType)
		{
			QMetaType oldMetaType(oldType);
			if (isArray)
			{
				// can convert to old type
				QVariant innerVar;
				auto iter = value.value<QSequentialIterable>();
				if (iter.size()>0) innerVar = iter.at(0);
				else { innerVar = QVariant( QMetaType(newType) ); }

				if (innerVar.canConvert(oldMetaType))
				{
					// convert to old type
					QVariantList listOldType;
					for (auto it = iter.begin(), itEnd = iter.end(); it != itEnd; ++it)
					{
						QVariant val = *it;
						val.convert(oldMetaType);
						listOldType.append(val);
					}

					newValue = listOldType;
					// preserve old type
					newType = oldType;
				}
			}
			// if scalar and can convert to old type
			else if (newValue.canConvert(oldMetaType))
			{
				// convert to old type
				newValue.convert(oldMetaType);
				// preserve old type
				newType = oldType;
			}
			else if (!newValue.isValid())
			{
				// preserve old type
				newType = oldType;
			}
		}
	}
	// these values are maped to the same (see QUaDataType::m_custTypesByNodeId in quacustomdatatypes.cpp)
	else if (newType == QMetaType::SChar    ) { newType = QMetaType::Char; }
	else if (newType == QMetaType::LongLong ) { newType = QMetaType::Long; }
	else if (newType == QMetaType::ULongLong) { newType = QMetaType::ULong;}

	// wether new type is forced or could not be converted to old type, we need type convertion
	if (newType != oldType)
	{
		auto st = UA_Server_writeDataType(m_qUaServer->m_server,
			m_nodeId,
			QUaTypesConverter::uaTypeNodeIdFromQType(newType));
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
	}

	// convert to UA_Variant and set new value
#if defined(UA_GENERATED_NAMESPACE_ZERO_FULL) && !defined(OPEN62541_ISSUE3934_RESOLVED)
	UA_DataType* optDataType = nullptr;
	if (oldType == QMetaType_OptionSet)
	{
		// read type
		UA_NodeId optionSetTypeNodeId;
		auto st = UA_Server_readDataType(m_qUaServer->m_server, m_nodeId, &optionSetTypeNodeId);
		Q_ASSERT(st == UA_STATUSCODE_GOOD);
		Q_UNUSED(st);
		optDataType = getDataTypeFromNodeId(optionSetTypeNodeId);
		UA_NodeId_clear(&optionSetTypeNodeId);
	}
	auto uaVar = QUaTypesConverter::uaVariantFromQVariant(newValue, optDataType);
#else
	auto uaVar = QUaTypesConverter::uaVariantFromQVariant(newValue);
#endif

	// mask as internal write to avoid emitting valueChange signal on QUaBaseVariable::onWrite
	m_bInternalWrite = true;
	auto st = this->setValueInternal(
		uaVar,
		statusCode,
		sourceTimestamp, 
		serverTimestamp
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// clean up
	UA_Variant_clear(&uaVar);

	// [NOTE] do not set rank or arrayDimensions because they are permanent
	//        is better to just set array dimensions on Variant value and leave rank as ANY
	// update cache
	m_dataType = newType;
	Q_ASSERT(this->dataTypeInternal() == m_dataType);
}

QDateTime QUaBaseVariable::sourceTimestamp() const
{
	UA_ReadValueId rv;
	UA_ReadValueId_init(&rv);
	rv.nodeId      = m_nodeId;
	rv.attributeId = UA_ATTRIBUTEID_VALUE;
	UA_DataValue value = UA_Server_read(
		m_qUaServer->m_server,
		&rv,
		UA_TIMESTAMPSTORETURN_SOURCE
	);
	QDateTime time = QUaTypesConverter::uaVariantToQVariantScalar<QDateTime, UA_DateTime>(&value.sourceTimestamp);
	// clean up
	UA_DataValue_clear(&value);
	return time;
}

void QUaBaseVariable::setSourceTimestamp(const QDateTime& sourceTimestamp)
{
	// get value
	UA_ReadValueId rv;
	UA_ReadValueId_init(&rv);
	rv.nodeId      = m_nodeId;
	rv.attributeId = UA_ATTRIBUTEID_VALUE;
	UA_DataValue value = UA_Server_read(
		m_qUaServer->m_server,
		&rv,
		UA_TIMESTAMPSTORETURN_BOTH
	);
	// set value
	UA_WriteValue wv;
	UA_WriteValue_init(&wv);
	wv.nodeId         = m_nodeId;
	wv.attributeId    = UA_ATTRIBUTEID_VALUE;
	wv.value.value    = value.value;
	wv.value.hasValue = value.hasValue;
	if (sourceTimestamp.isValid())
	{
		QUaTypesConverter::uaVariantFromQVariantScalar(sourceTimestamp, &wv.value.sourceTimestamp);
		wv.value.hasSourceTimestamp = true;
	}
	wv.value.serverTimestamp      = value.serverTimestamp     ;
	wv.value.hasServerTimestamp   = value.hasServerTimestamp  ;
	wv.value.serverPicoseconds    = value.serverPicoseconds   ;
	wv.value.sourcePicoseconds    = value.sourcePicoseconds   ;
	wv.value.hasServerPicoseconds = value.hasServerPicoseconds;
	wv.value.hasSourcePicoseconds = value.hasSourcePicoseconds;
	wv.value.status               = value.status;
	// NOTE : alternate hasStatus value to force notifying timestamp change
	// otherwise change is not sent to clients through subscription
	wv.value.hasStatus            = !value.hasStatus;
	auto st = UA_Server_write(m_qUaServer->m_server, &wv);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// clean up
	UA_Variant_clear(&value.value);
}

QDateTime QUaBaseVariable::serverTimestamp() const
{
	UA_ReadValueId rv;
	UA_ReadValueId_init(&rv);
	rv.nodeId      = m_nodeId;
	rv.attributeId = UA_ATTRIBUTEID_VALUE;
	UA_DataValue value = UA_Server_read(
		m_qUaServer->m_server,
		&rv,
		UA_TIMESTAMPSTORETURN_SERVER
	);
	QDateTime time = QUaTypesConverter::uaVariantToQVariantScalar<QDateTime, UA_DateTime>(&value.serverTimestamp);
	// clean up
	UA_DataValue_clear(&value);
	return time;
}

void QUaBaseVariable::setServerTimestamp(const QDateTime& serverTimestamp)
{
	// get value
	UA_ReadValueId rv;
	UA_ReadValueId_init(&rv);
	rv.nodeId      = m_nodeId;
	rv.attributeId = UA_ATTRIBUTEID_VALUE;
	UA_DataValue value = UA_Server_read(
		m_qUaServer->m_server,
		&rv,
		UA_TIMESTAMPSTORETURN_BOTH
	);
	// set value
	UA_WriteValue wv;
	UA_WriteValue_init(&wv);
	wv.nodeId         = m_nodeId;
	wv.attributeId    = UA_ATTRIBUTEID_VALUE;
	wv.value.value    = value.value;
	wv.value.hasValue = value.hasValue;
	wv.value.sourceTimestamp    = value.sourceTimestamp;
	wv.value.hasSourceTimestamp = value.hasSourceTimestamp;
	if (serverTimestamp.isValid())
	{
		QUaTypesConverter::uaVariantFromQVariantScalar(serverTimestamp, &wv.value.serverTimestamp);
		wv.value.hasServerTimestamp = true;
	}
	wv.value.serverPicoseconds    = value.serverPicoseconds   ;
	wv.value.sourcePicoseconds    = value.sourcePicoseconds   ;
	wv.value.hasServerPicoseconds = value.hasServerPicoseconds;
	wv.value.hasSourcePicoseconds = value.hasSourcePicoseconds;
	wv.value.status               = value.status;
	// NOTE : alternate hasStatus value to force notifying timestamp change
	// otherwise change is not sent to clients through subscription
	wv.value.hasStatus            = !value.hasStatus;
	auto st = UA_Server_write(m_qUaServer->m_server, &wv);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// clean up
	UA_Variant_clear(&value.value);
}

QUaStatusCode QUaBaseVariable::statusCode() const
{
	UA_ReadValueId rv;
	UA_ReadValueId_init(&rv);
	rv.nodeId      = m_nodeId;
	rv.attributeId = UA_ATTRIBUTEID_VALUE;
	UA_DataValue value = UA_Server_read(
		m_qUaServer->m_server,
		&rv,
		UA_TIMESTAMPSTORETURN_SERVER
	);
	QUaStatusCode statusCode = value.status;
	// clean up
	UA_DataValue_clear(&value);
	return statusCode;
}

void QUaBaseVariable::setStatusCode(const QUaStatusCode& statusCode)
{
	// get value
	UA_ReadValueId rv;
	UA_ReadValueId_init(&rv);
	rv.nodeId      = m_nodeId;
	rv.attributeId = UA_ATTRIBUTEID_VALUE;
	UA_DataValue value = UA_Server_read(
		m_qUaServer->m_server,
		&rv,
		UA_TIMESTAMPSTORETURN_BOTH
	);
	// set value
	UA_WriteValue wv;
	UA_WriteValue_init(&wv);
	wv.nodeId         = m_nodeId;
	wv.attributeId    = UA_ATTRIBUTEID_VALUE;
	wv.value.value    = value.value;
	wv.value.hasValue = value.hasValue;
	wv.value.sourceTimestamp      = value.sourceTimestamp     ;
	wv.value.hasSourceTimestamp   = value.hasSourceTimestamp  ;
	wv.value.serverTimestamp      = value.serverTimestamp     ;
	wv.value.hasServerTimestamp   = value.hasServerTimestamp  ;
	wv.value.serverPicoseconds    = value.serverPicoseconds   ;
	wv.value.sourcePicoseconds    = value.sourcePicoseconds   ;
	wv.value.hasServerPicoseconds = value.hasServerPicoseconds;
	wv.value.hasSourcePicoseconds = value.hasSourcePicoseconds;
	wv.value.status               = statusCode;
	wv.value.hasStatus            = true;
	auto st = UA_Server_write(m_qUaServer->m_server, &wv);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// clean up
	UA_Variant_clear(&value.value);
}

QMetaType::Type QUaBaseVariable::dataType() const
{
	return m_dataType;
}

QString QUaBaseVariable::dataTypeNodeId() const
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	if (UA_NodeId_isNull(&m_nodeId))
	{
		return  QUaTypesConverter::nodeIdToQString(UA_NODEID_NULL);
	}
	// read type
	UA_NodeId outDataType;
	auto st = UA_Server_readDataType(m_qUaServer->m_server, m_nodeId, &outDataType);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// check if type is enum, if so, return type int 32
	if (!m_qUaServer->m_hashEnums.key(outDataType).isEmpty())
	{
		UA_NodeId_clear(&outDataType);
		return QUaTypesConverter::nodeIdToQString(UA_NODEID_NUMERIC(0, UA_NS0ID_INT32));
	}
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
	// check if type is option set, if so, return type
	if (!m_qUaServer->m_hashOptionSets.key(outDataType, "").isEmpty())
	{
		UA_NodeId_clear(&outDataType);
		return QUaTypesConverter::nodeIdToQString(UA_NODEID_NUMERIC(0, UA_NS0ID_OPTIONSET));
	}
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
	// else return converted type
	QString retNodeId = QUaTypesConverter::nodeIdToQString(outDataType);
	UA_NodeId_clear(&outDataType);
	return retNodeId;
}

void QUaBaseVariable::setDataType(const QMetaType::Type & newTypeConst)
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	auto dataType = newTypeConst;
	// these values are maped to the same (see QUaDataType::m_custTypesByNodeId in quacustomdatatypes.cpp)
	if      (dataType == QMetaType::SChar    ) { dataType = QMetaType::Char; }
	else if (dataType == QMetaType::LongLong ) { dataType = QMetaType::Long; }
	else if (dataType == QMetaType::ULongLong) { dataType = QMetaType::ULong;}
	// early exit if already same
	if (dataType == m_dataType)
	{
		return;
	}
	// need to "reset" dataType before setting a new value
	auto st = UA_Server_writeDataType(m_qUaServer->m_server,
		m_nodeId,
		UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATATYPE));
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// get old value
	QVariant oldValue = this->value();
	QMetaType dataMetaType(dataType);
	// handle array
	if (QUaTypesConverter::canConvertQVariantList(oldValue))
	{
		QVariantList listConvValues;
		auto iter = oldValue.value<QSequentialIterable>();
		// loop scalar or array
		for (auto it = iter.begin(), itEnd = iter.end(); it != itEnd; ++it)
		{
			QVariant varCurr = *it;
			if (varCurr.canConvert(dataMetaType)) { varCurr.convert(dataMetaType); }
			else
			{
				// else set default value for type
				varCurr = QVariant(dataMetaType);
			}
			// append to list of converted values
			listConvValues.append(varCurr);
		}
		// overwrite old value
		oldValue = listConvValues;
	}
	// handle scalar
	else if (oldValue.canConvert(dataMetaType))
	{
		// convert in place
		oldValue.convert(dataMetaType);
	}
	else
	{
		// else set default value for type
		oldValue = QVariant(dataMetaType);
	}
	// set converted or default value
	auto tmpVar = QUaTypesConverter::uaVariantFromQVariant(oldValue);
	m_bInternalWrite = true;
	st = this->setValueInternal(tmpVar);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// clean up
	UA_Variant_clear(&tmpVar);
	// set new type
	st = UA_Server_writeDataType(m_qUaServer->m_server,
		m_nodeId,
		QUaTypesConverter::uaTypeNodeIdFromQType(dataType));
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// update cache
	m_dataType = dataType;
	Q_ASSERT(this->dataTypeInternal() == m_dataType);
}

void QUaBaseVariable::setDataTypeEnum(const QMetaEnum & metaEnum)
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	// compose enum name
    QString strEnumName = QStringLiteral("%1::%2").arg(
				QString::fromLatin1(metaEnum.scope()),
				QString::fromLatin1(metaEnum.enumName()));
	// register if not exists
	if (!m_qUaServer->m_hashEnums.contains(strEnumName))
	{
		m_qUaServer->registerEnum(metaEnum);
	}
	Q_ASSERT(m_qUaServer->m_hashEnums.contains(strEnumName));
	// get enum nodeId
	UA_NodeId enumTypeNodeId = m_qUaServer->m_hashEnums.value(strEnumName);
	// call internal method
	this->setDataTypeEnum(enumTypeNodeId);
}

bool QUaBaseVariable::setDataTypeEnum(const QString & strEnumName)
{
	// check if exists in server's hash
	if (!m_qUaServer->m_hashEnums.contains(strEnumName))
	{
		return false;
	}
	// get enum nodeId
	UA_NodeId enumTypeNodeId = m_qUaServer->m_hashEnums.value(strEnumName);
	// call internal method
	this->setDataTypeEnum(enumTypeNodeId);
	// success
	return true;
}

void QUaBaseVariable::setDataTypeEnum(const UA_NodeId & enumTypeNodeId)
{
	// need to "reset" dataType before setting a new value
	auto st = UA_Server_writeDataType(m_qUaServer->m_server,
		m_nodeId,
		UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATATYPE));
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// get old value
	QVariant oldValue = this->value();
	// handle array
	QMetaType metaTypeInt = QMetaType::fromType<int>();
	if (QUaTypesConverter::canConvertQVariantList(oldValue))
	{
		auto iter = oldValue.value<QSequentialIterable>();
		// get first value if any
		QVariant varFirst = iter.size() > 0 ? iter.at(0) : QVariant(metaTypeInt);
		// overwrite old value
		oldValue = varFirst;
	}
	// handle scalar
	if (oldValue.canConvert(metaTypeInt))
	{
		// convert in place
		oldValue.convert(metaTypeInt);
	}
	else
	{
		// else set default value for type
		oldValue = QVariant(metaTypeInt);
	}
	// set converted or default value
	auto tmpVar = QUaTypesConverter::uaVariantFromQVariant(oldValue);
	// NOTE : value must be of the enum type, else it is not compatible with the new data type
	const UA_DataType* enumType = UA_Server_findDataType(m_qUaServer->m_server, &enumTypeNodeId);
	if (enumType && enumType->typeKind == UA_DATATYPEKIND_ENUM && tmpVar.type == &UA_TYPES[UA_TYPES_INT32])
	{
		tmpVar.type = enumType;
	}
	m_bInternalWrite = true;
	st = this->setValueInternal(tmpVar);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// clean up
	UA_Variant_clear(&tmpVar);
	// change data type
	st = UA_Server_writeDataType(m_qUaServer->m_server,
		m_nodeId,
		enumTypeNodeId);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// update cache
	m_dataType = QMetaType::Int;
	Q_ASSERT(this->dataTypeInternal() == m_dataType);
}

#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
bool QUaBaseVariable::setDataTypeOptionSet(const QString& strOptionSetName)
{
	// check if exists in server's hash
	if (!m_qUaServer->m_hashOptionSets.contains(strOptionSetName))
	{
		return false;
	}
	// get option set nodeId
	UA_NodeId optionSetTypeNodeId = m_qUaServer->m_hashOptionSets.value(strOptionSetName);
	// call internal method
	this->setDataTypeOptionSet(optionSetTypeNodeId);
	// success
	return true;
}

// TODO : update to specific type and handle conversions there
void QUaBaseVariable::setDataTypeOptionSet(const UA_NodeId& optionSetTypeNodeId)
{
	// need to "reset" dataType before setting a new value
	auto st = UA_Server_writeDataType(m_qUaServer->m_server,
		m_nodeId,
		UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATATYPE));
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// get old value
	QVariant oldValue = this->value();
	// handle array
	if (QUaTypesConverter::canConvertQVariantList(oldValue))
	{
		auto iter = oldValue.value<QSequentialIterable>();
		// get first value if any
		QVariant varFirst = iter.size() > 0 ? iter.at(0) : QVariant(QMetaType(QMetaType::ULongLong));
		// overwrite old value
		oldValue = varFirst;
	}
	// handle scalar
	if (oldValue.canConvert(QMetaType(QMetaType::ULongLong)))
	{
		// convert in place
		oldValue = QVariant::fromValue(QUaOptionSet(oldValue.toULongLong()));
	}
	else
	{
		// else set default value for type
		oldValue = QVariant(QMetaType(QMetaType_OptionSet));
	}
	// set converted or default value
#ifndef OPEN62541_ISSUE3934_RESOLVED
	UA_DataType* optDataType = getDataTypeFromNodeId(optionSetTypeNodeId);
	auto tmpVar = QUaTypesConverter::uaVariantFromQVariant(oldValue, optDataType);
#else
	auto tmpVar = QUaTypesConverter::uaVariantFromQVariant(oldValue);
#endif // !OPEN62541_ISSUE3934_RESOLVED
	m_bInternalWrite = true;
	st = this->setValueInternal(tmpVar);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// clean up
	UA_Variant_clear(&tmpVar);
	// change data type
	st = UA_Server_writeDataType(m_qUaServer->m_server,
		m_nodeId,
		optionSetTypeNodeId
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// update cache
	m_dataType = QMetaType_OptionSet;
	Q_ASSERT(this->dataTypeInternal() == m_dataType);
}
#endif

QMetaType::Type QUaBaseVariable::dataTypeInternal() const
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	if (UA_NodeId_isNull(&m_nodeId))
	{
		return QMetaType::UnknownType;
	}
	// read type
	UA_NodeId outDataType;
	auto st = UA_Server_readDataType(m_qUaServer->m_server, m_nodeId, &outDataType);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// check if type is enum, if so, return type int 32
	if (!m_qUaServer->m_hashEnums.key(outDataType).isEmpty())
	{
		UA_NodeId_clear(&outDataType);
		return QMetaType::Int;
	}
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
	// check if type is option set, if so, return type QMetaType_OptionSet
	if (!m_qUaServer->m_hashOptionSets.key(outDataType, "").isEmpty())
	{
		UA_NodeId_clear(&outDataType);
		return QMetaType_OptionSet;
	}
#endif // UA_GENERATED_NAMESPACE_ZERO_FULL
	// else return converted type
	QMetaType::Type type = QUaTypesConverter::uaTypeNodeIdToQType(&outDataType);
	UA_NodeId_clear(&outDataType);
	return type;
}

UA_StatusCode QUaBaseVariable::setValueInternal(
	const UA_Variant    &value,
	const UA_StatusCode &status,
	const QDateTime     &sourceTimestamp, 
	const QDateTime     &serverTimestamp)
{
	// set value
	UA_WriteValue wv;
	UA_WriteValue_init(&wv);
	wv.nodeId         = m_nodeId;
	wv.attributeId    = UA_ATTRIBUTEID_VALUE;
	wv.value.value    = value;
	wv.value.hasValue = 1;
	if (sourceTimestamp.isValid())
	{
		QUaTypesConverter::uaVariantFromQVariantScalar(sourceTimestamp, &wv.value.sourceTimestamp);
		wv.value.hasSourceTimestamp = true;
	}
	if (serverTimestamp.isValid())
	{
		QUaTypesConverter::uaVariantFromQVariantScalar(serverTimestamp, &wv.value.serverTimestamp);
		wv.value.hasServerTimestamp = true;
	}
	wv.value.serverPicoseconds    = 0;
	wv.value.sourcePicoseconds    = 0;
	wv.value.hasServerPicoseconds = false;
	wv.value.hasSourcePicoseconds = false;
	wv.value.status               = status;
	wv.value.hasStatus            = true;
	auto st = UA_Server_write(m_qUaServer->m_server, &wv);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	return st;
}

qint32 QUaBaseVariable::valueRank() const
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	if (UA_NodeId_isNull(&m_nodeId))
	{
		return -1;
	}
	// read valueRank
	qint32 outValueRank;
	auto st = UA_Server_readValueRank(m_qUaServer->m_server, m_nodeId, &outValueRank);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	return outValueRank;
}

void QUaBaseVariable::setValueRank(const qint32& valueRank){
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	auto st = UA_Server_writeValueRank(m_qUaServer->m_server, m_nodeId, valueRank);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
}

QVector<quint32> QUaBaseVariable::arrayDimensions() const
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	if (UA_NodeId_isNull(&m_nodeId))
	{
		return QVector<quint32>();
	}
	// read arrayDimensionsSize
	UA_Variant outArrayDimensions;
	auto st = UA_Server_readArrayDimensions(m_qUaServer->m_server, m_nodeId, &outArrayDimensions);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// convert UA_Variant to QList<quint32>
	QVector<quint32> retArr;
	Q_ASSERT(outArrayDimensions.type == &UA_TYPES[UA_TYPES_UINT32]);
	auto data = static_cast<quint32*>(outArrayDimensions.data);
	for (int i = 0; i < (int)outArrayDimensions.arrayLength; i++)
	{
		retArr.append(data[i]);
	}
	return retArr;
}

/*
void QUaBaseVariable::setArrayDimensions(const quint32 &size) // const QVector<quint32> &arrayDimenstions
{
	UA_Variant uaArrayDimensions;
	UA_UInt32 arrayDims[1] = { size };
	UA_Variant_setArray(&uaArrayDimensions, arrayDims, 1, &UA_TYPES[UA_TYPES_UINT32]);
	UA_Server_writeArrayDimensions(m_qUaServer->m_server, m_nodeId, uaArrayDimensions);
}
*/

quint8 QUaBaseVariable::accessLevel() const
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	if (UA_NodeId_isNull(&m_nodeId))
	{
		return 0;
	}
	// read accessLevel
	UA_Byte outAccessLevel;
	auto st = UA_Server_readAccessLevel(m_qUaServer->m_server, m_nodeId, &outAccessLevel);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	return outAccessLevel;
}

void QUaBaseVariable::setAccessLevel(const quint8 & accessLevel)
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	// set accessLevel
	auto st = UA_Server_writeAccessLevel(m_qUaServer->m_server, m_nodeId, accessLevel);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
}

double QUaBaseVariable::minimumSamplingInterval() const
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	if (UA_NodeId_isNull(&m_nodeId))
	{
		return 0.0;
	}
	// read minimumSamplingInterval
	UA_Double outMinimumSamplingInterval;
	auto st = UA_Server_readMinimumSamplingInterval(m_qUaServer->m_server, m_nodeId, &outMinimumSamplingInterval);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// return
	return outMinimumSamplingInterval;
}

void QUaBaseVariable::setMinimumSamplingInterval(const double & minimumSamplingInterval)
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	// set minimumSamplingInterval
	auto st = UA_Server_writeMinimumSamplingInterval(m_qUaServer->m_server, m_nodeId, minimumSamplingInterval);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
}

bool QUaBaseVariable::historizing() const
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	if (UA_NodeId_isNull(&m_nodeId))
	{
		return false;
	}
	// read historizing
	UA_Boolean outHistorizing;
	auto st = UA_Server_readHistorizing(m_qUaServer->m_server, m_nodeId, &outHistorizing);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	return outHistorizing;
}

#ifdef UA_ENABLE_HISTORIZING
void QUaBaseVariable::setHistorizing(const bool& historizing)
{
	Q_CHECK_PTR(m_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&m_nodeId));
	// set historizing
	auto st = UA_Server_writeHistorizing(m_qUaServer->m_server, m_nodeId, historizing);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	if (!historizing)
	{
		return;
	}
	// check if historizing already set
	auto gathering = m_qUaServer->getGathering();
	auto psetting  = gathering.getHistorizingSetting(
		m_qUaServer->m_server,
		gathering.context,
		&m_nodeId
	);
	if (psetting)
	{
		return;
	}
	// setup historizing 
	UA_HistorizingNodeIdSettings setting;
	setting.historizingBackend         = QUaHistoryBackend::m_historUaBackend;
	setting.maxHistoryDataResponseSize = m_maxHistoryDataResponseSize; // max size client can ask for
	setting.historizingUpdateStrategy  = UA_HISTORIZINGUPDATESTRATEGY_VALUESET; // when value updated or polling
	st = gathering.registerNodeId(m_qUaServer->m_server, gathering.context, &m_nodeId, setting);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
}
quint64 QUaBaseVariable::maxHistoryDataResponseSize() const
{
	return m_maxHistoryDataResponseSize;
}
void QUaBaseVariable::setMaxHistoryDataResponseSize(const quint64& maxHistoryDataResponseSize)
{
	// set internal value (put a minimum of 50 just in case)
	m_maxHistoryDataResponseSize = (std::max)(static_cast<quint64>(50), maxHistoryDataResponseSize);
	// check if historizing already set
	auto gathering = m_qUaServer->getGathering();
	// NOTE : the default gathering returns a pointer to the setting stored internally
	auto psetting = const_cast<UA_HistorizingNodeIdSettings*>(gathering.getHistorizingSetting(
		m_qUaServer->m_server,
		gathering.context,
		&m_nodeId
	));
	if (!psetting) {
		return;
	}
	psetting->maxHistoryDataResponseSize = m_maxHistoryDataResponseSize; // max size client can ask for
}
#endif // UA_ENABLE_HISTORIZING

bool QUaBaseVariable::readAccess() const
{
	QUaAccessLevel accessLevel;
	accessLevel.intValue = this->accessLevel();
	return accessLevel.bits.bRead;
}

void QUaBaseVariable::setReadAccess(const bool & readAccess)
{
	QUaAccessLevel accessLevel;
	accessLevel.intValue   = this->accessLevel();
	accessLevel.bits.bRead = readAccess;
	this->setAccessLevel(accessLevel.intValue);
}

bool QUaBaseVariable::writeAccess() const
{
	QUaAccessLevel accessLevel;
	accessLevel.intValue = this->accessLevel();
	return accessLevel.bits.bWrite;
}

void QUaBaseVariable::setWriteAccess(const bool & writeAccess)
{
	QUaAccessLevel accessLevel;
	accessLevel.intValue    = this->accessLevel();
	accessLevel.bits.bWrite = writeAccess;
	this->setAccessLevel(accessLevel.intValue);
}

#ifdef UA_ENABLE_HISTORIZING
bool QUaBaseVariable::readHistoryAccess() const
{
	QUaAccessLevel accessLevel;
	accessLevel.intValue = this->accessLevel();
	return accessLevel.bits.bHistoryRead;
}

void QUaBaseVariable::setReadHistoryAccess(const bool& readHistoryAccess)
{
	QUaAccessLevel accessLevel;
	accessLevel.intValue = this->accessLevel();
	accessLevel.bits.bHistoryRead = readHistoryAccess;
	this->setAccessLevel(accessLevel.intValue);
}

bool QUaBaseVariable::writeHistoryAccess() const
{
	QUaAccessLevel accessLevel;
	accessLevel.intValue = this->accessLevel();
	return accessLevel.bits.bHistoryWrite;
}

void QUaBaseVariable::setWriteHistoryAccess(const bool& bHistoryWrite)
{
	QUaAccessLevel accessLevel;
	accessLevel.intValue = this->accessLevel();
	accessLevel.bits.bHistoryWrite = bHistoryWrite;
	this->setAccessLevel(accessLevel.intValue);
}
#endif // UA_ENABLE_HISTORIZING

// [STATIC]
qint32 QUaBaseVariable::GetValueRankFromQVariant(const QVariant & varValue)
{
	auto originalType = static_cast<QMetaType::Type>( varValue.typeId() );
	if (originalType == QMetaType::UnknownType)
	{
		return UA_VALUERANK_ANY;
	}
	else if (QUaTypesConverter::canConvertQVariantList(varValue))
	{
		return UA_VALUERANK_ONE_DIMENSION;
	}
	// scalar is default
	return UA_VALUERANK_SCALAR;
}

// [STATIC]
QVector<quint32> QUaBaseVariable::GetArrayDimensionsFromQVariant(const QVariant & varValue)
{
	if (QUaTypesConverter::canConvertQVariantList(varValue))
	{
		auto iter = varValue.value<QSequentialIterable>();
		auto size = (quint32)iter.size();
		return QVector<quint32>() << size;
	}
	// default arrayDimensionsSize == 0
	return QVector<quint32>();
}
