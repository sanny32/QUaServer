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
    void operatorStatesAreOptional();
    void unsupportedOperatorMethodIsInvalid();
    void supportExposesOperatorMethods();
    void withdrawnSupportRemovesOperatorMethods();
    void suppress2SuppressesWithComment();
    void unsuppress2ClearsSuppression();
    void nullCommentKeepsCurrentComment();
    void removeFromService2TakesAlarmOutOfService();
    void placeInService2ReturnsAlarmToService();
    void suppressedOrShelveFollowsSuppressionAndService();
    void silenceSilencesAudibleAlarm();
    void acknowledgeSilencesAlarm();
    void activationClearsSilence();
    void latchedAlarmIsRetainedUntilReset2();
    void resetRejectsActiveAlarm();
    void resetRejectsUnacknowledgedAlarm();
    void resetRejectsUnlatchedAlarm();
    void getGroupMembershipsIsExposedOnAlarms();
    void getGroupMembershipsListsAlarmGroups();
    void cloneKeepsOperatorStates();

private:
    QUaServer *_server = nullptr;
    TestClient *_client = nullptr;
    QUaBaseObject *_sensor = nullptr;
    QUaBaseDataVariable *_moving = nullptr;
    QUaOffNormalAlarm *_alarm = nullptr;

    static QList<QUaBrowsePath> selectClauses();
    QVariantList lastAlarmEvent(const QList<QVariantList> &events) const;
    UA_StatusCode callAlarm(const QUaNodeId &methodId, const QVariantList &inputs = {});
    UA_StatusCode callAlarm(const QUaNodeId &methodId, const QVariantList &inputs, QVariantList &outputs);
    QList<QUaNodeId> alarmComponents();
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
const QUaNodeId HasComponent(0, quint32(UA_NS0ID_HASCOMPONENT));
const QUaNodeId SilenceMethod(0, quint32(UA_NS0ID_ALARMCONDITIONTYPE_SILENCE));
const QUaNodeId SuppressMethod(0, quint32(UA_NS0ID_ALARMCONDITIONTYPE_SUPPRESS));
const QUaNodeId Suppress2Method(0, quint32(UA_NS0ID_ALARMCONDITIONTYPE_SUPPRESS2));
const QUaNodeId Unsuppress2Method(0, quint32(UA_NS0ID_ALARMCONDITIONTYPE_UNSUPPRESS2));
const QUaNodeId RemoveFromService2Method(0, quint32(UA_NS0ID_ALARMCONDITIONTYPE_REMOVEFROMSERVICE2));
const QUaNodeId PlaceInService2Method(0, quint32(UA_NS0ID_ALARMCONDITIONTYPE_PLACEINSERVICE2));
const QUaNodeId ResetMethod(0, quint32(UA_NS0ID_ALARMCONDITIONTYPE_RESET));
const QUaNodeId Reset2Method(0, quint32(UA_NS0ID_ALARMCONDITIONTYPE_RESET2));
const QUaNodeId GetGroupMembershipsMethod(0, quint32(UA_NS0ID_ALARMCONDITIONTYPE_GETGROUPMEMBERSHIPS));

QVariant comment(const QString &text)
{
    return QVariant::fromValue(QUaLocalizedText(QStringLiteral("en"), text));
}

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
/// \brief Calls the method \a methodId of the test alarm, discarding its outputs.
///
UA_StatusCode TestAlarmsIntegration::callAlarm(const QUaNodeId &methodId, const QVariantList &inputs)
{
    QVariantList outputs;
    return callAlarm(methodId, inputs, outputs);
}

///
/// \brief Calls the method \a methodId of the test alarm.
///
UA_StatusCode TestAlarmsIntegration::callAlarm(const QUaNodeId &methodId,
                                                const QVariantList &inputs,
                                                QVariantList &outputs)
{
    return _client->call(_alarm->nodeId(), methodId, inputs, outputs);
}

///
/// \brief Returns the components, methods included, a client browsing the test alarm finds.
///
QList<QUaNodeId> TestAlarmsIntegration::alarmComponents()
{
    QList<QUaNodeId> components;
    if (_client->browseTargets(_alarm->nodeId(), HasComponent, components) != UA_STATUSCODE_GOOD)
    {
        return {};
    }
    return components;
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

///
/// \brief A new alarm exposes none of the optional operator states or their methods.
///
void TestAlarmsIntegration::operatorStatesAreOptional()
{
    const QList<QUaNodeId> components = alarmComponents();

    QVERIFY(!_alarm->suppressionSupported());
    QVERIFY(!_alarm->outOfServiceSupported());
    QVERIFY(!_alarm->silenceSupported());
    QVERIFY(!_alarm->latchingSupported());
    QVERIFY(!components.contains(Suppress2Method));
    QVERIFY(!components.contains(Reset2Method));
}

///
/// \brief An operator method of a state the alarm does not expose is refused.
///
void TestAlarmsIntegration::unsupportedOperatorMethodIsInvalid()
{
    const UA_StatusCode status = callAlarm(Suppress2Method, { comment(QStringLiteral("hide")) });

    QCOMPARE(status, UA_STATUSCODE_BADMETHODINVALID);
    QVERIFY(!_alarm->suppressed());
}

///
/// \brief Supporting suppression adds SuppressedState and the four suppression methods to the alarm.
///
void TestAlarmsIntegration::supportExposesOperatorMethods()
{
    _alarm->setSuppressionSupported(true);
    const QList<QUaNodeId> components = alarmComponents();

    QVERIFY(_alarm->suppressionSupported());
    QVERIFY(!_alarm->suppressed());
    QVERIFY(components.contains(SuppressMethod));
    QVERIFY(components.contains(Suppress2Method));
    QVERIFY(components.contains(Unsuppress2Method));
}

///
/// \brief Regression: methods inherited from AlarmConditionType could not be removed from an alarm subtype.
///
void TestAlarmsIntegration::withdrawnSupportRemovesOperatorMethods()
{
    _alarm->setLatchingSupported(true);

    _alarm->setLatchingSupported(false);
    const QList<QUaNodeId> components = alarmComponents();

    QVERIFY(!_alarm->latchingSupported());
    QVERIFY(!components.contains(ResetMethod));
    QVERIFY(!components.contains(Reset2Method));
    QCOMPARE(callAlarm(Reset2Method, { comment(QStringLiteral("clear")) }), UA_STATUSCODE_BADMETHODINVALID);
}

///
/// \brief Suppress2 suppresses the alarm and records the operator comment.
///
void TestAlarmsIntegration::suppress2SuppressesWithComment()
{
    _alarm->setSuppressionSupported(true);
    QSignalSpy suppressedSpy(_alarm, &QUaAlarmCondition::suppressedChanged);

    const UA_StatusCode status = callAlarm(Suppress2Method, { comment(QStringLiteral("maintenance")) });

    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QVERIFY(_alarm->suppressed());
    QCOMPARE(suppressedSpy.count(), 1);
    QCOMPARE(_alarm->comment().text(), QStringLiteral("maintenance"));
}

///
/// \brief Unsuppress2 clears a suppression.
///
void TestAlarmsIntegration::unsuppress2ClearsSuppression()
{
    _alarm->setSuppressionSupported(true);
    _alarm->setSuppressed(true);

    const UA_StatusCode status = callAlarm(Unsuppress2Method, { comment(QStringLiteral("back")) });

    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QVERIFY(!_alarm->suppressed());
    QCOMPARE(_alarm->comment().text(), QStringLiteral("back"));
}

///
/// \brief A null Comment (empty locale and text) leaves the current comment untouched.
///
void TestAlarmsIntegration::nullCommentKeepsCurrentComment()
{
    _alarm->setSuppressionSupported(true);
    _alarm->setComment(QUaLocalizedText(QStringLiteral("en"), QStringLiteral("earlier")));

    const UA_StatusCode status = callAlarm(Suppress2Method, { QVariant::fromValue(QUaLocalizedText()) });

    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QVERIFY(_alarm->suppressed());
    QCOMPARE(_alarm->comment().text(), QStringLiteral("earlier"));
}

///
/// \brief RemoveFromService2 takes the alarm out of service.
///
void TestAlarmsIntegration::removeFromService2TakesAlarmOutOfService()
{
    _alarm->setOutOfServiceSupported(true);
    QSignalSpy serviceSpy(_alarm, &QUaAlarmCondition::outOfServiceChanged);

    const UA_StatusCode status = callAlarm(RemoveFromService2Method, { comment(QStringLiteral("repair")) });

    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QVERIFY(_alarm->outOfService());
    QCOMPARE(serviceSpy.count(), 1);
    QCOMPARE(_alarm->comment().text(), QStringLiteral("repair"));
}

///
/// \brief PlaceInService2 returns an out of service alarm to service.
///
void TestAlarmsIntegration::placeInService2ReturnsAlarmToService()
{
    _alarm->setOutOfServiceSupported(true);
    _alarm->setOutOfService(true);

    const UA_StatusCode status = callAlarm(PlaceInService2Method, { comment(QStringLiteral("fixed")) });

    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QVERIFY(!_alarm->outOfService());
}

///
/// \brief SuppressedOrShelved stays set while the alarm is either suppressed or out of service.
///
void TestAlarmsIntegration::suppressedOrShelveFollowsSuppressionAndService()
{
    _alarm->setSuppressionSupported(true);
    _alarm->setOutOfServiceSupported(true);

    _alarm->setSuppressed(true);
    _alarm->setOutOfService(true);
    _alarm->setSuppressed(false);
    const bool whileOutOfService = _alarm->suppressedOrShelve();
    _alarm->setOutOfService(false);

    QVERIFY(whileOutOfService);
    QVERIFY(!_alarm->suppressedOrShelve());
}

///
/// \brief Silence silences an active alarm.
///
void TestAlarmsIntegration::silenceSilencesAudibleAlarm()
{
    _alarm->setSilenceSupported(true);
    _moving->setValue(true);
    QSignalSpy silencedSpy(_alarm, &QUaAlarmCondition::silencedChanged);

    const UA_StatusCode status = callAlarm(SilenceMethod);

    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QVERIFY(_alarm->silenced());
    QCOMPARE(silencedSpy.count(), 1);
}

///
/// \brief Acknowledging an alarm also silences it.
///
void TestAlarmsIntegration::acknowledgeSilencesAlarm()
{
    _alarm->setSilenceSupported(true);
    _moving->setValue(true);

    _alarm->setAcknowledged(true);

    QVERIFY(_alarm->silenced());
}

///
/// \brief A new activation makes a silenced alarm audible again.
///
void TestAlarmsIntegration::activationClearsSilence()
{
    _alarm->setSilenceSupported(true);
    _moving->setValue(true);
    _alarm->setAcknowledged(true);
    _moving->setValue(false);

    _moving->setValue(true);

    QVERIFY(!_alarm->silenced());
}

///
/// \brief A latched alarm stays retained after returning to normal until Reset2 clears the latch.
///
void TestAlarmsIntegration::latchedAlarmIsRetainedUntilReset2()
{
    _alarm->setLatchingSupported(true);
    _moving->setValue(true);
    _alarm->setAcknowledged(true);
    _moving->setValue(false);
    const bool latchedWhileNormal = _alarm->latched();
    const bool retainedWhileNormal = _alarm->retain();

    const UA_StatusCode status = callAlarm(Reset2Method, { comment(QStringLiteral("cleared")) });

    QVERIFY(latchedWhileNormal);
    QVERIFY(retainedWhileNormal);
    QCOMPARE(status, UA_STATUSCODE_GOOD);
    QVERIFY(!_alarm->latched());
    QVERIFY(!_alarm->retain());
    QCOMPARE(_alarm->comment().text(), QStringLiteral("cleared"));
}

///
/// \brief Reset is refused while the alarm is still active.
///
void TestAlarmsIntegration::resetRejectsActiveAlarm()
{
    _alarm->setLatchingSupported(true);
    _moving->setValue(true);
    _alarm->setAcknowledged(true);

    const UA_StatusCode status = callAlarm(ResetMethod);

    QCOMPARE(status, UA_STATUSCODE_BADINVALIDSTATE);
    QVERIFY(_alarm->latched());
}

///
/// \brief Reset is refused until the alarm is acknowledged.
///
void TestAlarmsIntegration::resetRejectsUnacknowledgedAlarm()
{
    _alarm->setLatchingSupported(true);
    _moving->setValue(true);
    _moving->setValue(false);

    const UA_StatusCode status = callAlarm(ResetMethod);

    QCOMPARE(status, UA_STATUSCODE_BADINVALIDSTATE);
    QVERIFY(_alarm->latched());
}

///
/// \brief Reset is refused when the alarm is not latched.
///
void TestAlarmsIntegration::resetRejectsUnlatchedAlarm()
{
    _alarm->setLatchingSupported(true);

    const UA_StatusCode status = callAlarm(ResetMethod);

    QCOMPARE(status, UA_STATUSCODE_BADINVALIDSTATE);
}

///
/// \brief Every alarm exposes GetGroupMemberships, which needs no optional state.
///
void TestAlarmsIntegration::getGroupMembershipsIsExposedOnAlarms()
{
    QVERIFY(alarmComponents().contains(GetGroupMembershipsMethod));
}

///
/// \brief GetGroupMemberships lists the groups referencing the alarm through AlarmGroupMember.
///
void TestAlarmsIntegration::getGroupMembershipsListsAlarmGroups()
{
    QVariantList outputs;
    const UA_StatusCode ungroupedStatus = callAlarm(GetGroupMembershipsMethod, {}, outputs);
    const QVariantList ungrouped = outputs;
    QUaBaseObject *group = _server->objectsFolder()->addBaseObject(QStringLiteral("group"));
    group->addReference({ QStringLiteral("AlarmGroupMember"), QStringLiteral("MemberOfAlarmGroup") }, _alarm);

    const UA_StatusCode groupedStatus = callAlarm(GetGroupMembershipsMethod, {}, outputs);
    const QUaNodeId groupId = group->nodeId();
    delete group;

    QCOMPARE(ungroupedStatus, UA_STATUSCODE_GOOD);
    QCOMPARE(ungrouped.size(), 1);
    QVERIFY(ungrouped.first().toList().isEmpty());
    QCOMPARE(groupedStatus, UA_STATUSCODE_GOOD);
    QCOMPARE(outputs.size(), 1);
    const QVariantList groups = outputs.first().toList();
    QCOMPARE(groups.size(), 1);
    QCOMPARE(groups.first().value<QUaNodeId>(), groupId);
}

///
/// \brief A cloned alarm, restored through serialization, keeps its operator states and their methods.
///
void TestAlarmsIntegration::cloneKeepsOperatorStates()
{
    _alarm->setSuppressionSupported(true);
    _alarm->setSuppressed(true);

    auto *clone = qobject_cast<QUaOffNormalAlarm *>(_alarm->cloneNode(_sensor, QStringLiteral("clone")));

    QVERIFY(clone);
    QVERIFY(clone->suppressionSupported());
    QVERIFY(clone->suppressed());
    QVERIFY(clone->suppressedOrShelve());
    QVariantList outputs;
    QCOMPARE(_client->call(clone->nodeId(), Unsuppress2Method, { comment(QStringLiteral("clone")) }, outputs),
             UA_STATUSCODE_GOOD);
    QVERIFY(!clone->suppressed());
}

QTEST_GUILESS_MAIN(TestAlarmsIntegration)

#include "test_alarms_integration.moc"
