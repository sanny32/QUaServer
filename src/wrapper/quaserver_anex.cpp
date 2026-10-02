#include "quaserver_anex.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
#include <QUaCondition>
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

UA_QualifiedName QUaServer_Anex::eventFieldKey(const QUaBrowsePath& browsePath)
{
    // NOTE : open62541 looks up event fields using the printed (normal form)
    //        SimpleAttributeOperand relative to the BaseEventType, e.g. "/EnabledState/Id"
    UA_SimpleAttributeOperand sao;
    UA_SimpleAttributeOperand_init(&sao);
    sao.typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEEVENTTYPE);
    sao.attributeId      = UA_ATTRIBUTEID_VALUE;
    QVector<UA_QualifiedName> path;
    path.reserve(browsePath.count());
    for (const auto& name : browsePath)
    {
        path << name.toUaQualifiedName();
    }
    sao.browsePathSize = static_cast<size_t>(path.count());
    sao.browsePath     = path.data();
    UA_QualifiedName key;
    UA_QualifiedName_init(&key);
    auto st = UA_SimpleAttributeOperand_print(&sao, &key.name);
    Q_ASSERT(st == UA_STATUSCODE_GOOD);
    Q_UNUSED(st);
    for (auto& name : path)
    {
        UA_QualifiedName_clear(&name);
    }
    return key;
}

UA_StatusCode QUaServer_Anex::createEvent(
    UA_Server* server,
    const UA_NodeId& eventNodeId,
    const UA_NodeId& origin,
    const QUaSaoCallback& resolveSAOCallback,
    const UA_NodeId* sessionId,
    const UA_UInt32* subscriptionId,
    const UA_UInt32* monitoredItemId
)
{
    // get event type
    UA_NodeId eventType = QUaNode::typeDefinitionNodeId(eventNodeId, server);
    if (UA_NodeId_isNull(&eventType))
    {
        return UA_STATUSCODE_BADNOTFOUND;
    }
    // event fields from callback (for condition branches)
    UA_KeyValueMap eventFields = UA_KEYVALUEMAP_NULL;
    if (resolveSAOCallback)
    {
        auto srv = QUaServer::getServerNodeContext(server);
        Q_ASSERT(srv);
        const QUaNodeId eventTypeNodeId(eventType);
        Q_ASSERT(srv->m_hashTypeVars.contains(eventTypeNodeId));
        const auto typeVars = srv->m_hashTypeVars.value(eventTypeNodeId);
        for (auto it = typeVars.constBegin(); it != typeVars.constEnd(); ++it)
        {
            UA_QualifiedName key = QUaServer_Anex::eventFieldKey(it.key());
            UA_Variant value = QUaTypesConverter::uaVariantFromQVariant(resolveSAOCallback(it.key()));
            auto st = UA_KeyValueMap_set(&eventFields, key, &value);
            Q_ASSERT(st == UA_STATUSCODE_GOOD);
            Q_UNUSED(st);
            UA_Variant_clear(&value);
            UA_QualifiedName_clear(&key);
        }
        // NOTE : open62541 requires the EventId to be a scalar ByteString
        static const UA_QualifiedName eventIdKey = UA_QUALIFIEDNAME(0, (char*)"/EventId");
        const UA_Variant* eventId = UA_KeyValueMap_get(&eventFields, eventIdKey);
        if (eventId && UA_Variant_hasScalarType(eventId, &UA_TYPES[UA_TYPES_STRING]))
        {
            // same memory layout
            const_cast<UA_Variant*>(eventId)->type = &UA_TYPES[UA_TYPES_BYTESTRING];
        }
        else if (eventId && UA_Variant_hasArrayType(eventId, &UA_TYPES[UA_TYPES_BYTE]))
        {
            // QByteArray might be converted to an array of bytes
            UA_ByteString byteString;
            byteString.length = eventId->arrayLength;
            byteString.data   = static_cast<UA_Byte*>(eventId->data);
            UA_Variant value;
            UA_Variant_setScalar(&value, &byteString, &UA_TYPES[UA_TYPES_BYTESTRING]);
            auto st = UA_KeyValueMap_set(&eventFields, eventIdKey, &value); // makes a copy
            Q_ASSERT(st == UA_STATUSCODE_GOOD);
            Q_UNUSED(st);
        }
        else if (eventId && !UA_Variant_hasScalarType(eventId, &UA_TYPES[UA_TYPES_BYTESTRING]))
        {
            UA_KeyValueMap_remove(&eventFields, eventIdKey);
        }
    }
    // create event
    UA_EventDescription ed;
    memset(&ed, 0, sizeof(UA_EventDescription));
    ed.sourceNode      = origin;
    ed.eventType       = eventType;
    ed.eventFields     = resolveSAOCallback ? &eventFields : nullptr;
    // NOTE : fields not defined in the map are resolved from the event instance
    ed.eventInstance   = &eventNodeId;
    ed.sessionId       = sessionId;
    ed.subscriptionId  = subscriptionId;
    ed.monitoredItemId = monitoredItemId;
    auto st = UA_Server_createEventEx(server, &ed, nullptr);
    // cleanup
    UA_KeyValueMap_clear(&eventFields);
    UA_NodeId_clear(&eventType);
    return st;
}

UA_StatusCode QUaServer_Anex::UA_Event_addEventToMonitoredItem(
    UA_Server* server,
    const UA_NodeId* eventNodeId,
    const UA_NodeId* sessionId,
    const UA_UInt32  subscriptionId,
    const UA_UInt32  monitoredItemId,
    const QUaSaoCallback& resolveSAOCallback
)
{
    // NOTE : the source node is the event instance itself, the monitored item
    //        filter only gets the event if the monitored node is in the emitting hierarchy,
    //        so the source must be set accordingly by the caller in the event instance
    auto node = QUaNode::getNodeContext(*eventNodeId, server);
    auto event = qobject_cast<QUaBaseEvent*>(node);
    Q_ASSERT(event);
    if (!event)
    {
        return UA_STATUSCODE_BADNOTFOUND;
    }
    UA_NodeId origin = event->m_sourceNodeId;
    return QUaServer_Anex::createEvent(
        server,
        *eventNodeId,
        origin,
        resolveSAOCallback,
        sessionId,
        &subscriptionId,
        &monitoredItemId
    );
}

UA_StatusCode
QUaServer_Anex::UA_Server_triggerEvent_Modified(
    UA_Server* server,
    const UA_NodeId eventNodeId,
    const UA_NodeId origin,
    const QUaSaoCallback& resolveSAOCallback/* = nullptr*/
) {
    // send event to clients
    UA_StatusCode retval = QUaServer_Anex::createEvent(
        server,
        eventNodeId,
        origin,
        resolveSAOCallback,
        nullptr,
        nullptr,
        nullptr
    );
    if (retval != UA_STATUSCODE_GOOD)
    {
        UA_LOG_WARNING(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_SERVER,
            "Events: Could not trigger event with StatusCode %s", UA_StatusCode_name(retval));
        return retval;
    }

    // [MODIFIED] : custom history code
#ifdef UA_ENABLE_HISTORIZING
    // get event instance
    auto event = qobject_cast<QUaBaseEvent*>(QUaNode::getNodeContext(eventNodeId, server));
    Q_ASSERT(event);
    bool historize = event->historizing();
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
    if (resolveSAOCallback)
    {
        auto condition = qobject_cast<QUaCondition*>(event);
        Q_ASSERT(condition);
        historize = historize && condition->historizingBranches();
    }
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
    if (!historize)
    {
        return UA_STATUSCODE_GOOD;
    }
    auto srv = QUaServer::getServerNodeContext(server);
    Q_ASSERT(srv);
    QList<QUaNodeId> emittersNodeIds;
    if (srv->eventHistoryRead())
    {
        emittersNodeIds << UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER);
    }
    /* Get the list of nodes in the hierarchy that emits the event. Events
     * propagate upwards (bubble up) in the node hierarchy. */
    UA_BrowseDescription bd;
    UA_BrowseDescription_init(&bd);
    bd.nodeId          = origin;
    bd.browseDirection = UA_BROWSEDIRECTION_INVERSE;
    bd.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
    bd.includeSubtypes = true;
    bd.nodeClassMask   = UA_NODECLASS_OBJECT;
    bd.resultMask      = UA_BROWSERESULTMASK_NONE;
    UA_ExpandedNodeId* emitNodes = NULL;
    size_t emitNodesSize = 0;
    retval = UA_Server_browseRecursive(server, &bd, &emitNodesSize, &emitNodes);
    if (retval != UA_STATUSCODE_GOOD) {
        UA_LOG_WARNING(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_SERVER,
            "Events: Could not create the list of nodes emitting the "
            "event with StatusCode %s", UA_StatusCode_name(retval));
        emitNodesSize = 0;
        retval = UA_STATUSCODE_GOOD;
    }
    // add origin
    {
        auto emitter = qobject_cast<QUaBaseObject*>(QUaNode::getNodeContext(origin, server));
        if (emitter && emitter->eventHistoryRead())
        {
            emittersNodeIds << origin;
        }
    }
    for (size_t i = 0; i < emitNodesSize; i++)
    {
        //do not support filter "HistoricalEventFilter"
        // NOTE : delete a bunch of stuff of the original history plugin
        // get emitter instance
        auto emitter = qobject_cast<QUaBaseObject*>(QUaNode::getNodeContext(emitNodes[i].nodeId, server));
        // NOTE : emitter can be null for default objects (open62541?) like UA_NS0ID_ROOTFOLDER
        if (emitter && emitter->eventHistoryRead() && !emittersNodeIds.contains(emitNodes[i].nodeId))
        {
            emittersNodeIds << emitNodes[i].nodeId;
        }
    }
    UA_Array_delete(emitNodes, emitNodesSize, &UA_TYPES[UA_TYPES_EXPANDEDNODEID]);
    if (emittersNodeIds.count() <= 0)
    {
        return retval;
    }
    // get event type node id
    QUaNodeId eventTypeNodeId = event->typeDefinitionNodeId();
    Q_ASSERT(srv->m_hashTypeVars.contains(eventTypeNodeId));
    // populate history point
    QUaHistoryEventPoint eventPoint;
    auto& typeData = srv->m_hashTypeVars[eventTypeNodeId];
    auto i = typeData.begin();
    while (i != typeData.end())
    {
        auto &name = i.key();
        ++i;
        QVariant value;
        if (resolveSAOCallback)
        {
            value = resolveSAOCallback(
                QUaBrowsePath() << name
            );
        }
        else
        {
            auto var = event->browsePath<QUaBaseVariable>(name);
            value = var ? var->value() : QVariant();
        }
        // NOTE : do not if (!value.isValid()), else branchId column will not be created
        eventPoint.fields[name] = value;
    }
    const static auto eventNodeIdPath      = QUaBrowsePath() << QUaQualifiedName(0, "EventNodeId");
    const static auto originatorNodeIdPath = QUaBrowsePath() << QUaQualifiedName(0, "OriginNodeId");
    Q_ASSERT(!eventPoint.fields.contains(eventNodeIdPath));
    Q_ASSERT(!eventPoint.fields.contains(originatorNodeIdPath));
    // add event node id and origin node id
    eventPoint.fields[eventNodeIdPath] = QVariant::fromValue(QUaNodeId(eventNodeId));
    eventPoint.fields[originatorNodeIdPath] = QVariant::fromValue(QUaNodeId(origin));
    // add timestamp
    eventPoint.timestamp = event->time();
    // store
    /*bool ok = */QUaHistoryBackend::setEvent(
        srv,
        eventTypeNodeId,
        emittersNodeIds,
        eventPoint
    );
    //NOTE : can fail due to historizer not set, which is acceptable Q_ASSERT(ok);
#endif // UA_ENABLE_HISTORIZING
    return retval;
}

#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
