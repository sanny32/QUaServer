#include <QSignalSpy>
#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

class DoorEvent : public QUaBaseEvent
{
    Q_OBJECT

public:
    Q_INVOKABLE explicit DoorEvent(QUaServer *server)
        : QUaBaseEvent(server)
    {
    }
};

class TestEventsIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void serverEventReachesSubscriber();
    void everyTriggerGetsNewEventId();
    void objectEventCarriesItsSource();
    void eventOfOtherEmitterIsNotDelivered();

private:
    QUaServer *_server = nullptr;
    TestClient *_client = nullptr;

    static QList<QUaBrowsePath> selectClauses();
};

namespace {

enum Field
{
    EventIdField,
    MessageField,
    SeverityField,
    SourceNameField,
    EventTypeField
};

} // namespace

///
/// \brief Starts one server for the whole suite with the custom event type registered.
///
void TestEventsIntegration::initTestCase()
{
    _server = new QUaServer;
    _server->registerType<DoorEvent>(QUaNodeId(1, QStringLiteral("DoorEventType")));
    QVERIFY(TestServer::start(*_server));
}

///
/// \brief Stops and deletes the suite server.
///
void TestEventsIntegration::cleanupTestCase()
{
    delete _server;
    _server = nullptr;
}

///
/// \brief Connects a fresh client for each test, so subscriptions never leak between tests.
///
void TestEventsIntegration::init()
{
    _client = new TestClient;
    QCOMPARE(_client->connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
}

///
/// \brief Disconnects the client, which drops its subscriptions.
///
void TestEventsIntegration::cleanup()
{
    delete _client;
    _client = nullptr;
}

///
/// \brief Event fields requested by every subscription, in the order of the Field enum.
///
QList<QUaBrowsePath> TestEventsIntegration::selectClauses()
{
    return { { QUaQualifiedName(0, QStringLiteral("EventId")) },
             { QUaQualifiedName(0, QStringLiteral("Message")) },
             { QUaQualifiedName(0, QStringLiteral("Severity")) },
             { QUaQualifiedName(0, QStringLiteral("SourceName")) },
             { QUaQualifiedName(0, QStringLiteral("EventType")) } };
}

///
/// \brief An event created on the server is delivered to a Server subscription with its fields.
///
void TestEventsIntegration::serverEventReachesSubscriber()
{
    QCOMPARE(_client->subscribeEvents(QUaNodeId(0, quint32(UA_NS0ID_SERVER)), selectClauses()), UA_STATUSCODE_GOOD);
    DoorEvent *event = _server->createEvent<DoorEvent>();
    QVERIFY(event);
    QSignalSpy triggeredSpy(event, &QUaBaseEvent::triggered);
    event->setMessage(QStringLiteral("Door opened"));
    event->setSeverity(600);

    event->trigger();
    const QList<QVariantList> events = _client->waitForEvents(1);

    QCOMPARE(triggeredSpy.count(), 1);
    QCOMPARE(events.count(), 1);
    const QVariantList &fields = events.first();
    QCOMPARE(fields.at(EventIdField).toByteArray(), event->eventId());
    QCOMPARE(fields.at(MessageField).value<QUaLocalizedText>().text(), QStringLiteral("Door opened"));
    QCOMPARE(fields.at(SeverityField).toUInt(), 600u);
    QCOMPARE(fields.at(EventTypeField).value<QUaNodeId>(), QUaNodeId(1, QStringLiteral("DoorEventType")));
    delete event;
}

///
/// \brief Each trigger produces a separate notification with a fresh EventId.
///
void TestEventsIntegration::everyTriggerGetsNewEventId()
{
    QCOMPARE(_client->subscribeEvents(QUaNodeId(0, quint32(UA_NS0ID_SERVER)), selectClauses()), UA_STATUSCODE_GOOD);
    DoorEvent *event = _server->createEvent<DoorEvent>();

    event->trigger();
    const QByteArray firstId = event->eventId();
    event->trigger();
    const QList<QVariantList> events = _client->waitForEvents(2);

    QCOMPARE(events.count(), 2);
    QVERIFY(firstId != event->eventId());
    QCOMPARE(events.at(0).at(EventIdField).toByteArray(), firstId);
    QCOMPARE(events.at(1).at(EventIdField).toByteArray(), event->eventId());
    delete event;
}

///
/// \brief An event created by an object reports that object as its source.
///
void TestEventsIntegration::objectEventCarriesItsSource()
{
    QUaBaseObject *door = _server->objectsFolder()->addBaseObject(QStringLiteral("door"),
                                                                    QUaNodeId(1, QStringLiteral("door")));
    door->setSubscribeToEvents(true);
    QCOMPARE(_client->subscribeEvents(door->nodeId(), selectClauses()), UA_STATUSCODE_GOOD);
    DoorEvent *event = door->createEvent<DoorEvent>();

    QCOMPARE(event->sourceNode(), door->nodeId());
    event->trigger();
    const QList<QVariantList> events = _client->waitForEvents(1);

    QCOMPARE(events.count(), 1);
    QCOMPARE(events.first().at(SourceNameField).toString(), QStringLiteral("door"));
    delete door;
}

///
/// \brief A subscription on one object does not receive events emitted by another.
///
void TestEventsIntegration::eventOfOtherEmitterIsNotDelivered()
{
    QUaBaseObject *watched = _server->objectsFolder()->addBaseObject(QStringLiteral("watched"));
    QUaBaseObject *other = _server->objectsFolder()->addBaseObject(QStringLiteral("other"));
    watched->setSubscribeToEvents(true);
    other->setSubscribeToEvents(true);
    QCOMPARE(_client->subscribeEvents(watched->nodeId(), selectClauses()), UA_STATUSCODE_GOOD);
    DoorEvent *event = other->createEvent<DoorEvent>();

    event->trigger();

    QVERIFY(_client->waitForEvents(1, 500).isEmpty());
    delete watched;
    delete other;
}

QTEST_GUILESS_MAIN(TestEventsIntegration)

#include "test_events_integration.moc"
