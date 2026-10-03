#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

class TestLimitsIntegration : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void subscriptionsPerSessionAreLimited();
    void publishingIntervalIsRevisedIntoLimits();
    void monitoredItemsPerSubscriptionAreLimited();
    void samplingIntervalIsRevisedIntoLimits();
    void nodesPerReadAreLimited();
    void limitsSurviveRestart();

private:
    QUaServer *m_server = nullptr;
    QUaBaseDataVariable *m_first = nullptr;
    QUaBaseDataVariable *m_second = nullptr;
    QUaBaseDataVariable *m_third = nullptr;

    bool startWithLimits(const QUaServerLimits &limits);
    QString url() const;
};

///
/// \brief Every test gets a fresh, stopped server with three variables to read and monitor.
///
void TestLimitsIntegration::init()
{
    m_server = new QUaServer;
    m_first = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("first"));
    m_second = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("second"));
    m_third = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("third"));
    m_first->setValue(1);
    m_second->setValue(2);
    m_third->setValue(3);
}

void TestLimitsIntegration::cleanup()
{
    delete m_server;
    m_server = nullptr;
}

///
/// \brief Applies \a limits and starts the server on a free port.
///
bool TestLimitsIntegration::startWithLimits(const QUaServerLimits &limits)
{
    return m_server->setLimits(limits) && TestServer::start(*m_server);
}

QString TestLimitsIntegration::url() const
{
    return TestServer::endpointUrl(*m_server);
}

void TestLimitsIntegration::subscriptionsPerSessionAreLimited()
{
    QUaServerLimits limits = m_server->limits();
    limits.maxSubscriptionsPerSession = 1;
    QVERIFY(startWithLimits(limits));
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);
    double revised = 0.0;

    QCOMPARE(client.createSubscription(500.0, revised), UA_STATUSCODE_GOOD);
    QCOMPARE(client.createSubscription(500.0, revised), UA_STATUSCODE_BADTOOMANYSUBSCRIPTIONS);
}

void TestLimitsIntegration::publishingIntervalIsRevisedIntoLimits()
{
    QUaServerLimits limits = m_server->limits();
    limits.minPublishingInterval = 500.0;
    limits.maxPublishingInterval = 1000.0;
    QVERIFY(startWithLimits(limits));
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);
    double tooFast = 0.0;
    double tooSlow = 0.0;

    QCOMPARE(client.createSubscription(50.0, tooFast), UA_STATUSCODE_GOOD);
    QCOMPARE(client.createSubscription(5000.0, tooSlow), UA_STATUSCODE_GOOD);

    QCOMPARE(tooFast, 500.0);
    QCOMPARE(tooSlow, 1000.0);
}

void TestLimitsIntegration::monitoredItemsPerSubscriptionAreLimited()
{
    QUaServerLimits limits = m_server->limits();
    limits.maxMonitoredItemsPerSubscription = 1;
    QVERIFY(startWithLimits(limits));
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);

    QCOMPARE(client.monitorValue(m_first->nodeId()), UA_STATUSCODE_GOOD);
    QCOMPARE(client.monitorValue(m_second->nodeId()), UA_STATUSCODE_BADTOOMANYMONITOREDITEMS);
}

void TestLimitsIntegration::samplingIntervalIsRevisedIntoLimits()
{
    QUaServerLimits limits = m_server->limits();
    limits.minSamplingInterval = 200.0;
    QVERIFY(startWithLimits(limits));
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);
    double revised = 0.0;

    QCOMPARE(client.monitorValue(m_first->nodeId(), 10.0, revised), UA_STATUSCODE_GOOD);

    QCOMPARE(revised, 200.0);
}

void TestLimitsIntegration::nodesPerReadAreLimited()
{
    QUaServerLimits limits = m_server->limits();
    limits.maxNodesPerRead = 2;
    QVERIFY(startWithLimits(limits));
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);

    QCOMPARE(client.readValues({ m_first->nodeId(), m_second->nodeId() }), UA_STATUSCODE_GOOD);
    QCOMPARE(client.readValues({ m_first->nodeId(), m_second->nodeId(), m_third->nodeId() }),
             UA_STATUSCODE_BADTOOMANYOPERATIONS);
}

///
/// \brief open62541 resets its configuration on every start, so the limits must be applied again.
///
void TestLimitsIntegration::limitsSurviveRestart()
{
    QUaServerLimits limits = m_server->limits();
    limits.maxNodesPerRead = 1;
    QVERIFY(startWithLimits(limits));
    m_server->stop();
    QVERIFY(m_server->start());
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);

    QCOMPARE(client.readValues({ m_first->nodeId(), m_second->nodeId() }), UA_STATUSCODE_BADTOOMANYOPERATIONS);
}

QTEST_GUILESS_MAIN(TestLimitsIntegration)

#include "test_limits_integration.moc"
