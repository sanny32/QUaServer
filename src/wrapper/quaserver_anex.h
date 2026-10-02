#ifndef QUASERVER_ANEX_H
#define QUASERVER_ANEX_H

#include "quaserver.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS

/*********************************************************************************************
Event helpers implemented on top of the public open62541 event API (UA_Server_createEventEx).

Event fields are resolved from the event instance node, unless a resolveSAOCallback is given
(condition branches) in which case all the event fields are resolved using the callback.
*/

class QUaServer_Anex
{
    friend class QUaServer;
    friend class QUaBaseEvent;
#ifdef UA_ENABLE_HISTORIZING
    friend class QUaHistoryBackend;
#endif // UA_ENABLE_HISTORIZING
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
    friend class QUaCondition;
    friend class QUaConditionBranch;
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

    typedef std::function<QVariant(const QUaBrowsePath&)> QUaSaoCallback;

    // Trigger the event instance eventNodeId with origin as source node.
    // Also stores the event in the history backend if historizing is enabled.
    static UA_StatusCode UA_Server_triggerEvent_Modified(
        UA_Server* server,
        const UA_NodeId eventNodeId,
        const UA_NodeId origin,
        const QUaSaoCallback& resolveSAOCallback = nullptr
    );

    // Send the event instance only to the given monitored item.
    // Not stored in history (used for ConditionRefresh).
    static UA_StatusCode UA_Event_addEventToMonitoredItem(
        UA_Server* server,
        const UA_NodeId* eventNodeId,
        const UA_NodeId* sessionId,
        const UA_UInt32  subscriptionId,
        const UA_UInt32  monitoredItemId,
        const QUaSaoCallback& resolveSAOCallback
    );

    // Create and send event through open62541
    static UA_StatusCode createEvent(
        UA_Server* server,
        const UA_NodeId& eventNodeId,
        const UA_NodeId& origin,
        const QUaSaoCallback& resolveSAOCallback,
        const UA_NodeId* sessionId,
        const UA_UInt32* subscriptionId,
        const UA_UInt32* monitoredItemId
    );

    // Get key used by open62541 to resolve event fields from UA_KeyValueMap
    static UA_QualifiedName eventFieldKey(const QUaBrowsePath& browsePath);
};

#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

#endif // QUASERVER_ANEX_H
