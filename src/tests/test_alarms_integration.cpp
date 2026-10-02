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
    QUaServer *m_server = nullptr;
    TestClient *m_client = nullptr;
    QUaBaseObject *m_sensor = nullptr;
    QUaBaseDataVariable *m_moving = nullptr;
    QUaOffNormalAlarm *m_alarm = nullptr;

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
    m_server = new QUaServer;
    QVERIFY(TestServer::start(*m_server));
}

///
/// \brief Stops and deletes the suite server.
///
void TestAlarmsIntegration::cleanupTestCase()
{
    delete m_server;
    m_server = nullptr;
}

///
/// \brief Creates a motion sensor with an enabled off-normal alarm on its bool input, and a connected client.
///
void TestAlarmsIntegration::init()
{
    m_sensor = m_server->objectsFolder()->addBaseObject(QStringLiteral("sensor"));
    m_sensor->setSubscribeToEvents(true);
    m_moving = m_sensor->addBaseDataVariable(QStringLiteral("moving"));
    m_moving->setDataType(QMetaType::Bool);
    m_moving->setValue(false);
    m_alarm = m_sensor->addChild<QUaOffNormalAlarm>(QStringLiteral("alarm"));
    m_alarm->setConditionName(QStringLiteral("Motion"));
    m_alarm->setInputNode(m_moving);
    m_alarm->setNormalValue(false);
    m_alarm->setEnabled(true);
    m_client = new TestClient;
    QCOMPARE(m_client->connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
}

///
/// \brief Disconnects the client and deletes the sensor together with its alarm.
///
void TestAlarmsIntegration::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_sensor;
    m_sensor = nullptr;
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
    QSignalSpy activatedSpy(m_alarm, &QUaAlarmCondition::activated);

    m_moving->setValue(true);

    QCOMPARE(activatedSpy.count(), 1);
    QVERIFY(m_alarm->active());
    QVERIFY(m_alarm->retain());
    QVERIFY(!m_alarm->acknowledged());
}

///
/// \brief Subscribers on the source object receive the activation as an OffNormalAlarmType event.
///
void TestAlarmsIntegration::activationIsNotifiedToSubscribers()
{
    QCOMPARE(m_client->subscribeEvents(m_sensor->nodeId(), selectClauses()), UA_STATUSCODE_GOOD);

    m_moving->setValue(true);
    const QVariantList event = lastAlarmEvent(m_client->waitForEvents(1));

    QVERIFY(!event.isEmpty());
    QCOMPARE(event.at(EventIdField).toByteArray(), m_alarm->eventId());
    QCOMPARE(event.at(SourceNameField).toString(), QStringLiteral("sensor"));
    QCOMPARE(event.at(RetainField).toBool(), true);
}

///
/// \brief The Acknowledge method acknowledges the alarm and records the comment.
///
void TestAlarmsIntegration::clientAcknowledgesAlarm()
{
    m_moving->setValue(true);
    QSignalSpy ackSpy(m_alarm, &QUaAcknowledgeableCondition::conditionAcknowledged);
    QVariantList outputs;

    const UA_StatusCode status = m_client->call(
        m_alarm->nodeId(), AcknowledgeMethod,
        { m_alarm->eventId(), QVariant::fromValue(QUaLocalizedText(QStringLiteral("seen"))) }, outputs);

    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QVERIFY(m_alarm->acknowledged());
    QCOMPARE(ackSpy.count(), 1);
    QCOMPARE(m_alarm->comment().text(), QStringLiteral("seen"));
}

///
/// \brief Acknowledging an already acknowledged alarm fails.
///
void TestAlarmsIntegration::secondAcknowledgeIsRejected()
{
    m_moving->setValue(true);
    m_alarm->setAcknowledged(true);
    QVariantList outputs;

    const UA_StatusCode status = m_client->call(
        m_alarm->nodeId(), AcknowledgeMethod,
        { m_alarm->eventId(), QVariant::fromValue(QUaLocalizedText(QStringLiteral("again"))) }, outputs);

    QCOMPARE(status, UA_STATUSCODE_BADCONDITIONBRANCHALREADYACKED);
}

///
/// \brief A disabled alarm cannot be acknowledged.
///
void TestAlarmsIntegration::disabledAlarmRejectsAcknowledge()
{
    m_moving->setValue(true);
    m_alarm->Disable();
    QVariantList outputs;

    const UA_StatusCode status = m_client->call(
        m_alarm->nodeId(), AcknowledgeMethod,
        { m_alarm->eventId(), QVariant::fromValue(QUaLocalizedText(QStringLiteral("late"))) }, outputs);

    QCOMPARE(status, UA_STATUSCODE_BADCONDITIONDISABLED);
    QVERIFY(!m_alarm->acknowledged());
}

///
/// \brief Once acknowledged and back to normal, the alarm no longer needs to be retained.
///
void TestAlarmsIntegration::acknowledgedAlarmReturningToNormalIsNotRetained()
{
    m_moving->setValue(true);
    m_alarm->setAcknowledged(true);

    m_moving->setValue(false);

    QVERIFY(!m_alarm->active());
    QVERIFY(!m_alarm->retain());
}

///
/// \brief Regression: ConditionRefresh rejected the subscription id, which open62541 decodes as IntegerId.
///
void TestAlarmsIntegration::conditionRefreshResendsRetainedAlarm()
{
    m_moving->setValue(true);
    QCOMPARE(m_client->subscribeEvents(m_sensor->nodeId(), selectClauses()), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    const UA_StatusCode status = m_client->call(ConditionType, ConditionRefreshMethod,
                                                { m_client->subscriptionId() }, outputs);
    QList<QUaNodeId> types;
    for (const QVariantList &event : m_client->waitForEvents(3))
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

    const UA_StatusCode status = m_client->call(ConditionType, ConditionRefreshMethod, {}, outputs);

    QVERIFY(status != UA_STATUSCODE_GOOD);
}

///
/// \brief ConditionRefresh2 reports which of its ids is unknown.
///
void TestAlarmsIntegration::conditionRefresh2ValidatesIds()
{
    QCOMPARE(m_client->subscribeEvents(m_sensor->nodeId(), selectClauses()), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    const UA_StatusCode badSubscription = m_client->call(
        ConditionType, ConditionRefresh2Method, { m_client->subscriptionId() + 1000, 1u }, outputs);
    const UA_StatusCode badItem = m_client->call(
        ConditionType, ConditionRefresh2Method, { m_client->subscriptionId(), 99999u }, outputs);

    QCOMPARE(badSubscription, UA_STATUSCODE_BADSUBSCRIPTIONIDINVALID);
    QCOMPARE(badItem, UA_STATUSCODE_BADMONITOREDITEMIDINVALID);
}

QTEST_GUILESS_MAIN(TestAlarmsIntegration)

#include "test_alarms_integration.moc"
