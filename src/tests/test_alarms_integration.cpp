#include <QSignalSpy>
#include <QTest>

#include <QUaServer>
#include <QUaOffNormalAlarm>

#include "testclient.h"
#include "testserver.h"

class TestAlarmsIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void abnormalInputActivatesAlarm();
    void activationIsNotifiedToSubscribers();
    void clientAcknowledgesAlarm();
    void secondAcknowledgeIsRejected();
    void disabledAlarmRejectsAcknowledge();
    void acknowledgedAlarmReturningToNormalIsNotRetained();
    void conditionRefreshResendsRetainedAlarm();
    void conditionRefreshRejectsMissingSubscriptionId();
    void conditionRefresh2ValidatesIds();

private:
    QUaServer *_server = nullptr;
    TestClient *_client = nullptr;
    QUaBaseObject *_sensor = nullptr;
    QUaBaseDataVariable *_moving = nullptr;
    QUaOffNormalAlarm *_alarm = nullptr;

    static QList<QUaBrowsePath> selectClauses();
    QVariantList lastAlarmEvent(const QList<QVariantList> &events) const;
};

namespace {

enum Field
{
    EventIdField,
    EventTypeField,
    SourceNameField,
    RetainField
};

const QUaNodeId AcknowledgeMethod(0, quint32(UA_NS0ID_ACKNOWLEDGEABLECONDITIONTYPE_ACKNOWLEDGE));
const QUaNodeId ConditionType(0, quint32(UA_NS0ID_CONDITIONTYPE));
const QUaNodeId ConditionRefreshMethod(0, quint32(UA_NS0ID_CONDITIONTYPE_CONDITIONREFRESH));
const QUaNodeId ConditionRefresh2Method(0, quint32(UA_NS0ID_CONDITIONTYPE_CONDITIONREFRESH2));
const QUaNodeId OffNormalAlarmType(0, quint32(UA_NS0ID_OFFNORMALALARMTYPE));
const QUaNodeId RefreshStartEventType(0, quint32(UA_NS0ID_REFRESHSTARTEVENTTYPE));
const QUaNodeId RefreshEndEventType(0, quint32(UA_NS0ID_REFRESHENDEVENTTYPE));

} // namespace

///
/// \brief Starts one server for the whole suite; every test adds its own alarm.
///
void TestAlarmsIntegration::initTestCase()
{
    _server = new QUaServer;
    QVERIFY(TestServer::start(*_server));
}

///
/// \brief Stops and deletes the suite server.
///
void TestAlarmsIntegration::cleanupTestCase()
{
    delete _server;
    _server = nullptr;
}

///
/// \brief Creates a motion sensor with an enabled off-normal alarm on its bool input, and a connected client.
///
void TestAlarmsIntegration::init()
{
    _sensor = _server->objectsFolder()->addBaseObject(QStringLiteral("sensor"));
    _sensor->setSubscribeToEvents(true);
    _moving = _sensor->addBaseDataVariable(QStringLiteral("moving"));
    _moving->setDataType(QMetaType::Bool);
    _moving->setValue(false);
    _alarm = _sensor->addChild<QUaOffNormalAlarm>(QStringLiteral("alarm"));
    _alarm->setConditionName(QStringLiteral("Motion"));
    _alarm->setInputNode(_moving);
    _alarm->setNormalValue(false);
    _alarm->setEnabled(true);
    _client = new TestClient;
    QCOMPARE(_client->connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
}

///
/// \brief Disconnects the client and deletes the sensor together with its alarm.
///
void TestAlarmsIntegration::cleanup()
{
    delete _client;
    _client = nullptr;
    delete _sensor;
    _sensor = nullptr;
}

///
/// \brief Event fields requested by every subscription, in the order of the Field enum.
///
QList<QUaBrowsePath> TestAlarmsIntegration::selectClauses()
{
    return { { QUaQualifiedName(0, QStringLiteral("EventId")) },
             { QUaQualifiedName(0, QStringLiteral("EventType")) },
             { QUaQualifiedName(0, QStringLiteral("SourceName")) },
             { QUaQualifiedName(0, QStringLiteral("Retain")) } };
}

///
/// \brief Returns the newest notification of the test alarm in \a events, or an empty list.
///
QVariantList TestAlarmsIntegration::lastAlarmEvent(const QList<QVariantList> &events) const
{
    for (auto it = events.crbegin(); it != events.crend(); ++it)
    {
        if (it->at(EventTypeField).value<QUaNodeId>() == OffNormalAlarmType)
        {
            return *it;
        }
    }
    return {};
}

///
/// \brief Leaving the normal value activates the alarm, which then needs attention.
///
void TestAlarmsIntegration::abnormalInputActivatesAlarm()
{
    QSignalSpy activatedSpy(_alarm, &QUaAlarmCondition::activated);

    _moving->setValue(true);

    QCOMPARE(activatedSpy.count(), 1);
    QVERIFY(_alarm->active());
    QVERIFY(_alarm->retain());
    QVERIFY(!_alarm->acknowledged());
}

///
/// \brief Subscribers on the source object receive the activation as an OffNormalAlarmType event.
///
void TestAlarmsIntegration::activationIsNotifiedToSubscribers()
{
    QCOMPARE(_client->subscribeEvents(_sensor->nodeId(), selectClauses()), UA_STATUSCODE_GOOD);

    _moving->setValue(true);
    const QVariantList event = lastAlarmEvent(_client->waitForEvents(1));

    QVERIFY(!event.isEmpty());
    QCOMPARE(event.at(EventIdField).toByteArray(), _alarm->eventId());
    QCOMPARE(event.at(SourceNameField).toString(), QStringLiteral("sensor"));
    QCOMPARE(event.at(RetainField).toBool(), true);
}

///
/// \brief The Acknowledge method acknowledges the alarm and records the comment.
///
void TestAlarmsIntegration::clientAcknowledgesAlarm()
{
    _moving->setValue(true);
    QSignalSpy ackSpy(_alarm, &QUaAcknowledgeableCondition::conditionAcknowledged);
    QVariantList outputs;

    const UA_StatusCode status = _client->call(
        _alarm->nodeId(), AcknowledgeMethod,
        { _alarm->eventId(), QVariant::fromValue(QUaLocalizedText(QStringLiteral("seen"))) }, outputs);

    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QVERIFY(_alarm->acknowledged());
    QCOMPARE(ackSpy.count(), 1);
    QCOMPARE(_alarm->comment().text(), QStringLiteral("seen"));
}

///
/// \brief Acknowledging an already acknowledged alarm fails.
///
void TestAlarmsIntegration::secondAcknowledgeIsRejected()
{
    _moving->setValue(true);
    _alarm->setAcknowledged(true);
    QVariantList outputs;

    const UA_StatusCode status = _client->call(
        _alarm->nodeId(), AcknowledgeMethod,
        { _alarm->eventId(), QVariant::fromValue(QUaLocalizedText(QStringLiteral("again"))) }, outputs);

    QCOMPARE(status, UA_STATUSCODE_BADCONDITIONBRANCHALREADYACKED);
}

///
/// \brief A disabled alarm cannot be acknowledged.
///
void TestAlarmsIntegration::disabledAlarmRejectsAcknowledge()
{
    _moving->setValue(true);
    _alarm->Disable();
    QVariantList outputs;

    const UA_StatusCode status = _client->call(
        _alarm->nodeId(), AcknowledgeMethod,
        { _alarm->eventId(), QVariant::fromValue(QUaLocalizedText(QStringLiteral("late"))) }, outputs);

    QCOMPARE(status, UA_STATUSCODE_BADCONDITIONDISABLED);
    QVERIFY(!_alarm->acknowledged());
}

///
/// \brief Once acknowledged and back to normal, the alarm no longer needs to be retained.
///
void TestAlarmsIntegration::acknowledgedAlarmReturningToNormalIsNotRetained()
{
    _moving->setValue(true);
    _alarm->setAcknowledged(true);

    _moving->setValue(false);

    QVERIFY(!_alarm->active());
    QVERIFY(!_alarm->retain());
}

///
/// \brief Regression: ConditionRefresh rejected the subscription id, which open62541 decodes as IntegerId.
///
void TestAlarmsIntegration::conditionRefreshResendsRetainedAlarm()
{
    _moving->setValue(true);
    QCOMPARE(_client->subscribeEvents(_sensor->nodeId(), selectClauses()), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    const UA_StatusCode status = _client->call(ConditionType, ConditionRefreshMethod,
                                                { _client->subscriptionId() }, outputs);
    QList<QUaNodeId> types;
    for (const QVariantList &event : _client->waitForEvents(3))
    {
        types << event.at(EventTypeField).value<QUaNodeId>();
    }

    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QCOMPARE(types, QList<QUaNodeId>({ RefreshStartEventType, OffNormalAlarmType, RefreshEndEventType }));
}

///
/// \brief ConditionRefresh without its argument is refused.
///
void TestAlarmsIntegration::conditionRefreshRejectsMissingSubscriptionId()
{
    QVariantList outputs;

    const UA_StatusCode status = _client->call(ConditionType, ConditionRefreshMethod, {}, outputs);

    QVERIFY(status != UA_STATUSCODE_GOOD);
}

///
/// \brief ConditionRefresh2 reports which of its ids is unknown.
///
void TestAlarmsIntegration::conditionRefresh2ValidatesIds()
{
    QCOMPARE(_client->subscribeEvents(_sensor->nodeId(), selectClauses()), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    const UA_StatusCode badSubscription = _client->call(
        ConditionType, ConditionRefresh2Method, { _client->subscriptionId() + 1000, 1u }, outputs);
    const UA_StatusCode badItem = _client->call(
        ConditionType, ConditionRefresh2Method, { _client->subscriptionId(), 99999u }, outputs);

    QCOMPARE(badSubscription, UA_STATUSCODE_BADSUBSCRIPTIONIDINVALID);
    QCOMPARE(badItem, UA_STATUSCODE_BADMONITOREDITEMIDINVALID);
}

QTEST_GUILESS_MAIN(TestAlarmsIntegration)

#include "test_alarms_integration.moc"
