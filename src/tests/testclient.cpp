#include "testclient.h"

#include <atomic>
#include <thread>

#include <QCoreApplication>
#include <QDeadlineTimer>

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/client_subscriptions.h>
#include <open62541/plugin/log_stdout.h>

namespace {

///
/// \brief Converts \a nodeId into an open62541 NodeId owned by the returned guard.
///
class UaNodeIdGuard
{
public:
    explicit UaNodeIdGuard(const QUaNodeId &nodeId) : m_nodeId(nodeId.toUaNodeId()) {}
    ~UaNodeIdGuard() { UA_NodeId_clear(&m_nodeId); }
    UaNodeIdGuard(const UaNodeIdGuard &) = delete;
    UaNodeIdGuard &operator=(const UaNodeIdGuard &) = delete;
    const UA_NodeId &get() const { return m_nodeId; }

private:
    UA_NodeId m_nodeId;
};

#ifdef UA_ENABLE_HISTORIZING
struct HistoryRawContext
{
    QVariantList *values;
};

struct HistoryEventsContext
{
    QList<QVariantList> *events;
};

///
/// \brief Returns a time range wide enough to contain every sample written by a test.
///
QPair<UA_DateTime, UA_DateTime> historyTimeRange()
{
    const UA_DateTime now = UA_DateTime_now();
    return { now - UA_DATETIME_SEC * 3600, now + UA_DATETIME_SEC * 3600 };
}
#endif // UA_ENABLE_HISTORIZING

} // namespace

///
/// \brief Creates a client that logs only warnings and errors.
///
TestClient::TestClient()
    : m_client(nullptr)
    , m_subscriptionId(0)
{
    UA_ClientConfig config;
    memset(&config, 0, sizeof(UA_ClientConfig));
    config.logging = UA_Log_Stdout_new(UA_LOGLEVEL_WARNING);
    UA_ClientConfig_setDefault(&config);
    config.timeout = 5000;
    // the test server has no certificate, so user names travel over SecurityPolicy None
    config.allowNonePolicyPassword = true;
    m_client = UA_Client_newWithConfig(&config);
}

TestClient::~TestClient()
{
    disconnect();
    UA_Client_delete(m_client);
}

///
/// \brief Opens an anonymous session on \a url.
///
UA_StatusCode TestClient::connect(const QString &url)
{
    const QByteArray endpoint = url.toUtf8();
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] { status = UA_Client_connect(m_client, endpoint.constData()); });
    return status;
}

///
/// \brief Opens a session on \a url authenticated with a user name and password.
///
UA_StatusCode TestClient::connectUsername(const QString &url, const QString &userName, const QString &password)
{
    const QByteArray endpoint = url.toUtf8();
    const QByteArray user = userName.toUtf8();
    const QByteArray pass = password.toUtf8();
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] {
        status = UA_Client_connectUsername(m_client, endpoint.constData(), user.constData(), pass.constData());
    });
    return status;
}

///
/// \brief Closes the session and the secure channel, if any.
///
UA_StatusCode TestClient::disconnect()
{
    m_subscriptionId = 0;
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] { status = UA_Client_disconnect(m_client); });
    return status;
}

///
/// \brief Returns the user token types offered by the endpoints of \a url.
///
QList<UA_UserTokenType> TestClient::endpointUserTokenTypes(const QString &url)
{
    const QByteArray endpoint = url.toUtf8();
    size_t endpointsSize = 0;
    UA_EndpointDescription *endpoints = nullptr;
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] {
        status = UA_Client_getEndpoints(m_client, endpoint.constData(), &endpointsSize, &endpoints);
    });
    QList<UA_UserTokenType> tokenTypes;
    if (status != UA_STATUSCODE_GOOD)
    {
        return tokenTypes;
    }
    for (size_t e = 0; e < endpointsSize; ++e)
    {
        for (size_t t = 0; t < endpoints[e].userIdentityTokensSize; ++t)
        {
            tokenTypes << endpoints[e].userIdentityTokens[t].tokenType;
        }
    }
    UA_Array_delete(endpoints, endpointsSize, &UA_TYPES[UA_TYPES_ENDPOINTDESCRIPTION]);
    return tokenTypes;
}

///
/// \brief Reads the Value attribute of \a nodeId into \a value.
///
UA_StatusCode TestClient::readValue(const QUaNodeId &nodeId, QVariant &value)
{
    const UaNodeIdGuard id(nodeId);
    UA_Variant uaValue;
    UA_Variant_init(&uaValue);
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] { status = UA_Client_readValueAttribute(m_client, id.get(), &uaValue); });
    if (status == UA_STATUSCODE_GOOD)
    {
        value = QUaTypesConverter::uaVariantToQVariant(uaValue);
    }
    UA_Variant_clear(&uaValue);
    return status;
}

///
/// \brief Reads the DataType attribute of the variable \a nodeId into \a dataTypeId.
///
UA_StatusCode TestClient::readValueDataType(const QUaNodeId &nodeId, QUaNodeId &dataTypeId)
{
    const UaNodeIdGuard id(nodeId);
    UA_NodeId uaDataTypeId;
    UA_NodeId_init(&uaDataTypeId);
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] { status = UA_Client_readDataTypeAttribute(m_client, id.get(), &uaDataTypeId); });
    if (status == UA_STATUSCODE_GOOD)
    {
        dataTypeId = uaDataTypeId;
    }
    UA_NodeId_clear(&uaDataTypeId);
    return status;
}

///
/// \brief Writes \a value to the Value attribute of \a nodeId.
///
UA_StatusCode TestClient::writeValue(const QUaNodeId &nodeId, const QVariant &value)
{
    const UaNodeIdGuard id(nodeId);
    UA_Variant uaValue = QUaTypesConverter::uaVariantFromQVariant(value);
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] { status = UA_Client_writeValueAttribute(m_client, id.get(), &uaValue); });
    UA_Variant_clear(&uaValue);
    return status;
}

///
/// \brief Calls the method \a methodId of \a objectId with \a inputs and stores its results in \a outputs.
///
UA_StatusCode TestClient::call(const QUaNodeId &objectId,
                               const QUaNodeId &methodId,
                               const QVariantList &inputs,
                               QVariantList &outputs)
{
    const UaNodeIdGuard object(objectId);
    const UaNodeIdGuard method(methodId);
    QList<UA_Variant> uaInputs;
    for (const QVariant &input : inputs)
    {
        uaInputs << QUaTypesConverter::uaVariantFromQVariant(input);
    }
    size_t outputSize = 0;
    UA_Variant *uaOutputs = nullptr;
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] {
        status = UA_Client_call(m_client, object.get(), method.get(),
                                uaInputs.size(), uaInputs.data(), &outputSize, &uaOutputs);
    });
    for (UA_Variant &input : uaInputs)
    {
        UA_Variant_clear(&input);
    }
    outputs.clear();
    for (size_t i = 0; i < outputSize; ++i)
    {
        outputs << QUaTypesConverter::uaVariantToQVariant(uaOutputs[i]);
    }
    UA_Array_delete(uaOutputs, outputSize, &UA_TYPES[UA_TYPES_VARIANT]);
    return status;
}

///
/// \brief Creates a subscription with a data change monitored item on the Value of \a nodeId.
///
UA_StatusCode TestClient::monitorValue(const QUaNodeId &nodeId)
{
    UA_StatusCode status = ensureSubscription();
    if (status != UA_STATUSCODE_GOOD)
    {
        return status;
    }
    const UaNodeIdGuard id(nodeId);
    const UA_MonitoredItemCreateRequest item = UA_MonitoredItemCreateRequest_default(id.get());
    UA_MonitoredItemCreateResult result;
    runInWorker([&] {
        result = UA_Client_MonitoredItems_createDataChange(m_client, m_subscriptionId, UA_TIMESTAMPSTORETURN_BOTH,
                                                           item, nullptr, nullptr, nullptr);
    });
    status = result.statusCode;
    UA_MonitoredItemCreateResult_clear(&result);
    return status;
}

///
/// \brief Creates a subscription with an event monitored item on \a emitterId.
/// \param selectClauses Browse paths, relative to BaseEventType, of the event fields to receive.
///
UA_StatusCode TestClient::subscribeEvents(const QUaNodeId &emitterId, const QList<QUaBrowsePath> &selectClauses)
{
    UA_StatusCode status = ensureSubscription();
    if (status != UA_STATUSCODE_GOOD)
    {
        return status;
    }

    const UaNodeIdGuard emitter(emitterId);
    UA_EventFilter filter = eventFilter(selectClauses);
    UA_MonitoredItemCreateRequest item;
    UA_MonitoredItemCreateRequest_init(&item);
    item.itemToMonitor.nodeId = emitter.get();
    item.itemToMonitor.attributeId = UA_ATTRIBUTEID_EVENTNOTIFIER;
    item.monitoringMode = UA_MONITORINGMODE_REPORTING;
    item.requestedParameters.queueSize = 100;
    UA_ExtensionObject_setValue(&item.requestedParameters.filter, &filter, &UA_TYPES[UA_TYPES_EVENTFILTER]);
    UA_MonitoredItemCreateResult result;
    runInWorker([&] {
        result = UA_Client_MonitoredItems_createEvent(m_client, m_subscriptionId, UA_TIMESTAMPSTORETURN_BOTH,
                                                      item, this, &TestClient::eventNotification, nullptr);
    });
    status = result.statusCode;
    UA_MonitoredItemCreateResult_clear(&result);
    UA_EventFilter_clear(&filter);
    return status;
}

///
/// \brief Returns the id of the subscription holding the monitored items, 0 when there is none.
///
UA_UInt32 TestClient::subscriptionId() const
{
    return m_subscriptionId;
}

///
/// \brief Processes publish responses until \a count events arrived or \a timeoutMs elapsed.
/// \return The received events, oldest first, each as the list of its selected fields.
///
QList<QVariantList> TestClient::waitForEvents(int count, int timeoutMs)
{
    m_events.clear();
    runInWorker([&] {
        const QDeadlineTimer deadline(timeoutMs);
        while (m_events.size() < count && !deadline.hasExpired())
        {
            UA_Client_run_iterate(m_client, 20);
        }
    });
    return m_events;
}

#ifdef UA_ENABLE_HISTORIZING
///
/// \brief Reads the first response page of raw history of \a nodeId.
/// \param numValuesPerNode Requested page size; 0 lets the server decide.
///
UA_StatusCode TestClient::readHistoryRaw(const QUaNodeId &nodeId, quint32 numValuesPerNode, QVariantList &values)
{
    const UaNodeIdGuard id(nodeId);
    const auto range = historyTimeRange();
    HistoryRawContext context{ &values };
    values.clear();
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] {
        status = UA_Client_HistoryRead_raw(m_client, &id.get(), &TestClient::historyRawPage,
                                           range.first, range.second, UA_STRING_NULL, false,
                                           numValuesPerNode, UA_TIMESTAMPSTORETURN_BOTH, &context);
    });
    return status;
}

///
/// \brief Reads the first response page of event history of \a nodeId.
/// \param numValuesPerNode Requested page size; 0 lets the server decide.
///
UA_StatusCode TestClient::readHistoryEvents(const QUaNodeId &nodeId,
                                            const QList<QUaBrowsePath> &selectClauses,
                                            quint32 numValuesPerNode,
                                            QList<QVariantList> &events)
{
    const UaNodeIdGuard id(nodeId);
    const auto range = historyTimeRange();
    UA_EventFilter filter = eventFilter(selectClauses);
    HistoryEventsContext context{ &events };
    events.clear();
    UA_StatusCode status = UA_STATUSCODE_GOOD;
    runInWorker([&] {
        status = UA_Client_HistoryRead_events(m_client, &id.get(), &TestClient::historyEventsPage,
                                              range.first, range.second, UA_STRING_NULL, filter,
                                              numValuesPerNode, UA_TIMESTAMPSTORETURN_BOTH, &context);
    });
    UA_EventFilter_clear(&filter);
    return status;
}
#endif // UA_ENABLE_HISTORIZING

///
/// \brief Creates the subscription shared by all monitored items, unless it already exists.
///
UA_StatusCode TestClient::ensureSubscription()
{
    if (m_subscriptionId != 0)
    {
        return UA_STATUSCODE_GOOD;
    }
    UA_CreateSubscriptionRequest request = UA_CreateSubscriptionRequest_default();
    request.requestedPublishingInterval = 50.0;
    UA_CreateSubscriptionResponse response;
    runInWorker([&] {
        response = UA_Client_Subscriptions_create(m_client, request, nullptr, nullptr, nullptr);
    });
    const UA_StatusCode status = response.responseHeader.serviceResult;
    if (status == UA_STATUSCODE_GOOD)
    {
        m_subscriptionId = response.subscriptionId;
    }
    UA_CreateSubscriptionResponse_clear(&response);
    return status;
}

///
/// \brief Runs a blocking client request on a worker thread while the server keeps iterating.
///
void TestClient::runInWorker(const std::function<void()> &job)
{
    std::atomic_bool done{ false };
    std::thread worker([&] {
        job();
        done = true;
    });
    while (!done)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    worker.join();
}

///
/// \brief Builds an event filter that selects the Value of each of \a selectClauses on BaseEventType.
///
UA_EventFilter TestClient::eventFilter(const QList<QUaBrowsePath> &selectClauses)
{
    UA_EventFilter filter;
    UA_EventFilter_init(&filter);
    filter.selectClausesSize = selectClauses.size();
    filter.selectClauses = static_cast<UA_SimpleAttributeOperand *>(
        UA_Array_new(filter.selectClausesSize, &UA_TYPES[UA_TYPES_SIMPLEATTRIBUTEOPERAND]));
    for (qsizetype c = 0; c < selectClauses.size(); ++c)
    {
        UA_SimpleAttributeOperand &operand = filter.selectClauses[c];
        operand.typeDefinitionId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEEVENTTYPE);
        operand.attributeId = UA_ATTRIBUTEID_VALUE;
        const QUaBrowsePath &path = selectClauses.at(c);
        operand.browsePathSize = path.size();
        operand.browsePath = static_cast<UA_QualifiedName *>(
            UA_Array_new(operand.browsePathSize, &UA_TYPES[UA_TYPES_QUALIFIEDNAME]));
        for (qsizetype p = 0; p < path.size(); ++p)
        {
            operand.browsePath[p] = path.at(p).toUaQualifiedName();
        }
    }
    return filter;
}

///
/// \brief Converts the fields of one event notification, keeping the select clause order.
///
QVariantList TestClient::eventFieldsToList(size_t nEventFields, UA_Variant *eventFields)
{
    QVariantList fields;
    for (size_t i = 0; i < nEventFields; ++i)
    {
        fields << QUaTypesConverter::uaVariantToQVariant(eventFields[i]);
    }
    return fields;
}

///
/// \brief Stores an event received on the monitored item created by subscribeEvents().
///
void TestClient::eventNotification(UA_Client *client,
                                   UA_UInt32 subId,
                                   void *subContext,
                                   UA_UInt32 monId,
                                   void *monContext,
                                   const UA_KeyValueMap eventFields)
{
    Q_UNUSED(client);
    Q_UNUSED(subId);
    Q_UNUSED(subContext);
    Q_UNUSED(monId);
    QVariantList fields;
    for (size_t i = 0; i < eventFields.mapSize; ++i)
    {
        fields << QUaTypesConverter::uaVariantToQVariant(eventFields.map[i].value);
    }
    static_cast<TestClient *>(monContext)->m_events << fields;
}

#ifdef UA_ENABLE_HISTORIZING
///
/// \brief Collects the values of one raw history page and stops paging.
///
UA_Boolean TestClient::historyRawPage(UA_Client *client,
                                      const UA_NodeId *nodeId,
                                      UA_Boolean moreDataAvailable,
                                      const UA_ExtensionObject *data,
                                      void *callbackContext)
{
    Q_UNUSED(client);
    Q_UNUSED(nodeId);
    Q_UNUSED(moreDataAvailable);
    auto context = static_cast<HistoryRawContext *>(callbackContext);
    if (data->content.decoded.type != &UA_TYPES[UA_TYPES_HISTORYDATA])
    {
        return false;
    }
    auto history = static_cast<const UA_HistoryData *>(data->content.decoded.data);
    for (size_t i = 0; i < history->dataValuesSize; ++i)
    {
        *context->values << QUaTypesConverter::uaVariantToQVariant(history->dataValues[i].value);
    }
    return false;
}

///
/// \brief Collects the events of one event history page and stops paging.
///
UA_Boolean TestClient::historyEventsPage(UA_Client *client,
                                         const UA_NodeId *nodeId,
                                         UA_Boolean moreDataAvailable,
                                         const UA_ExtensionObject *data,
                                         void *callbackContext)
{
    Q_UNUSED(client);
    Q_UNUSED(nodeId);
    Q_UNUSED(moreDataAvailable);
    auto context = static_cast<HistoryEventsContext *>(callbackContext);
    if (data->content.decoded.type != &UA_TYPES[UA_TYPES_HISTORYEVENT])
    {
        return false;
    }
    auto history = static_cast<const UA_HistoryEvent *>(data->content.decoded.data);
    for (size_t i = 0; i < history->eventsSize; ++i)
    {
        *context->events << eventFieldsToList(history->events[i].eventFieldsSize, history->events[i].eventFields);
    }
    return false;
}
#endif // UA_ENABLE_HISTORIZING
