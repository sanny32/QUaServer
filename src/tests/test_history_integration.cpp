#include <QTest>

#include <QUaServer>

#include "quainmemoryhistorizer.h"
#include "testclient.h"
#include "testserver.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
class ShiftEvent : public QUaBaseEvent
{
    Q_OBJECT

public:
    Q_INVOKABLE explicit ShiftEvent(QUaServer *server)
        : QUaBaseEvent(server)
    {
    }
};
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

class TestHistoryIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void rawHistoryReturnsWrittenValues();
    void requestedPageSizeIsHonoured();
    void variableWithoutHistoryAccessIsRefused();
    void onlyAcceptedClientWritesAreHistorized();
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
    void eventHistoryReturnsTriggeredEvents();
    void serverLimitAppliesWhenClientAsksForAllEvents();
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

private:
    QUaInMemoryHistorizer m_historizer;
    QUaServer *m_server = nullptr;
    TestClient *m_client = nullptr;
    QList<QUaNode *> m_created;

    QUaBaseDataVariable *addHistorizedVariable(const QString &name);
};

///
/// \brief Starts one server for the whole suite with an in-memory historizer.
///
void TestHistoryIntegration::initTestCase()
{
    m_server = new QUaServer;
    m_server->setHistorizer(m_historizer);
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
    m_server->registerType<ShiftEvent>();
    m_server->setEventHistoryRead(true);
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
    QVERIFY(TestServer::start(*m_server));
}

///
/// \brief Stops and deletes the suite server.
///
void TestHistoryIntegration::cleanupTestCase()
{
    delete m_server;
    m_server = nullptr;
}

///
/// \brief Connects an anonymous client for each test.
///
void TestHistoryIntegration::init()
{
    m_client = new TestClient;
    QCOMPARE(m_client->connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
}

///
/// \brief Disconnects the client and removes the nodes the test created.
///
void TestHistoryIntegration::cleanup()
{
    delete m_client;
    m_client = nullptr;
    qDeleteAll(m_created);
    m_created.clear();
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
    m_server->setMaxHistoryEventResponseSize(1000);
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
}

///
/// \brief Adds a historized, history-readable variable whose string NodeId equals \a name.
///
QUaBaseDataVariable *TestHistoryIntegration::addHistorizedVariable(const QString &name)
{
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(name, QUaNodeId(1, name));
    variable->setDataType(QMetaType::Int);
    variable->setHistorizing(true);
    variable->setReadHistoryAccess(true);
    m_created << variable;
    return variable;
}

namespace {

///
/// \brief Writes \a values to \a variable one second apart, ending a minute ago.
///
void writeSeries(QUaBaseDataVariable *variable, const QList<int> &values)
{
    const QDateTime last = QDateTime::currentDateTimeUtc().addSecs(-60);
    for (qsizetype i = 0; i < values.size(); ++i)
    {
        variable->setValue(values.at(i), QUaStatus::Good, last.addSecs(i - values.size() + 1));
    }
}

///
/// \brief Converts history values to ints, for comparison with the written series.
///
QList<int> toInts(const QVariantList &values)
{
    QList<int> ints;
    for (const QVariant &value : values)
    {
        ints << value.toInt();
    }
    return ints;
}

} // namespace

///
/// \brief Values written to a historized variable are read back in time order.
///
void TestHistoryIntegration::rawHistoryReturnsWrittenValues()
{
    QUaBaseDataVariable *variable = addHistorizedVariable(QStringLiteral("level"));
    writeSeries(variable, { 10, 20, 30, 40 });
    QVariantList values;

    QCOMPARE(m_client->readHistoryRaw(variable->nodeId(), 0, values), UA_STATUSCODE_GOOD);

    QCOMPARE(toInts(values), QList<int>({ 10, 20, 30, 40 }));
}

///
/// \brief A client page size smaller than the history returns only that many values.
///
void TestHistoryIntegration::requestedPageSizeIsHonoured()
{
    QUaBaseDataVariable *variable = addHistorizedVariable(QStringLiteral("flow"));
    writeSeries(variable, { 1, 2, 3, 4, 5 });
    QVariantList values;

    QCOMPARE(m_client->readHistoryRaw(variable->nodeId(), 2, values), UA_STATUSCODE_GOOD);

    QCOMPARE(toInts(values), QList<int>({ 1, 2 }));
}

///
/// \brief Without read history access the client gets no history.
///
void TestHistoryIntegration::variableWithoutHistoryAccessIsRefused()
{
    QUaBaseDataVariable *variable = addHistorizedVariable(QStringLiteral("secret"));
    writeSeries(variable, { 1, 2 });
    variable->setReadHistoryAccess(false);
    QVariantList values;

    const UA_StatusCode status = m_client->readHistoryRaw(variable->nodeId(), 0, values);

    QVERIFY(status != UA_STATUSCODE_GOOD);
    QVERIFY(values.isEmpty());
}

///
/// \brief A write rejected by the write validator never reaches the history.
///
void TestHistoryIntegration::onlyAcceptedClientWritesAreHistorized()
{
    QUaBaseDataVariable *variable = addHistorizedVariable(QStringLiteral("validated"));
    variable->setWriteAccess(true);
    variable->setWriteValidator([](const QVariant &value, const QUaSession *) {
        return QUaStatusCode(value.toInt() >= 0 ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADOUTOFRANGE);
    });
    QVariantList values;

    QCOMPARE(m_client->writeValue(variable->nodeId(), 5), UA_STATUSCODE_GOOD);
    QCOMPARE(m_client->writeValue(variable->nodeId(), -1), UA_STATUSCODE_BADOUTOFRANGE);
    QCOMPARE(m_client->writeValue(variable->nodeId(), 7), UA_STATUSCODE_GOOD);

    QCOMPARE(m_client->readHistoryRaw(variable->nodeId(), 0, values), UA_STATUSCODE_GOOD);
    QCOMPARE(toInts(values), QList<int>({ 5, 7 }));
}

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
namespace {

///
/// \brief Adds an object that keeps event history and triggers \a count events from it, a second apart.
///
QUaBaseObject *addEmitterWithEvents(QUaServer *server, const QString &name, int count)
{
    QUaBaseObject *emitter = server->objectsFolder()->addBaseObject(name);
    emitter->setSubscribeToEvents(true);
    emitter->setEventHistoryRead(true);
    ShiftEvent *event = emitter->createEvent<ShiftEvent>();
    const QDateTime first = QDateTime::currentDateTimeUtc().addSecs(-60 - count);
    for (int i = 0; i < count; ++i)
    {
        event->setTime(first.addSecs(i));
        event->setMessage(QStringLiteral("shift %1").arg(i));
        event->trigger();
    }
    return emitter;
}

const QList<QUaBrowsePath> messageClause = { { QUaQualifiedName(0, QStringLiteral("Message")) } };

} // namespace

///
/// \brief Triggered events are stored and read back with their fields.
///
void TestHistoryIntegration::eventHistoryReturnsTriggeredEvents()
{
    QUaBaseObject *emitter = addEmitterWithEvents(m_server, QStringLiteral("line"), 3);
    m_created << emitter;
    QList<QVariantList> events;

    QCOMPARE(m_client->readHistoryEvents(emitter->nodeId(), messageClause, 0, events), UA_STATUSCODE_GOOD);

    QCOMPARE(events.count(), 3);
    QCOMPARE(events.first().first().value<QUaLocalizedText>().text(), QStringLiteral("shift 0"));
    QCOMPARE(events.last().first().value<QUaLocalizedText>().text(), QStringLiteral("shift 2"));
}

///
/// \brief Regression: with no client limit the server limit for event history was not applied.
///
void TestHistoryIntegration::serverLimitAppliesWhenClientAsksForAllEvents()
{
    QUaBaseObject *emitter = addEmitterWithEvents(m_server, QStringLiteral("press"), 5);
    m_created << emitter;
    m_server->setMaxHistoryEventResponseSize(2);
    QList<QVariantList> events;

    QCOMPARE(m_client->readHistoryEvents(emitter->nodeId(), messageClause, 0, events), UA_STATUSCODE_GOOD);

    QCOMPARE(events.count(), 2);
}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS

QTEST_GUILESS_MAIN(TestHistoryIntegration)

#include "test_history_integration.moc"
