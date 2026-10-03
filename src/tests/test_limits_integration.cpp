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
    QUaServer *_server = nullptr;
    QUaBaseDataVariable *_first = nullptr;
    QUaBaseDataVariable *_second = nullptr;
    QUaBaseDataVariable *_third = nullptr;

    bool startWithLimits(const QUaServerLimits &limits);
    QString url() const;
};

///
/// \brief Every test gets a fresh, stopped server with three variables to read and monitor.
///
void TestLimitsIntegration::init()
{
    _server = new QUaServer;
    _first = _server->objectsFolder()->addBaseDataVariable(QStringLiteral("first"));
    _second = _server->objectsFolder()->addBaseDataVariable(QStringLiteral("second"));
    _third = _server->objectsFolder()->addBaseDataVariable(QStringLiteral("third"));
    _first->setValue(1);
    _second->setValue(2);
    _third->setValue(3);
}

void TestLimitsIntegration::cleanup()
{
    delete _server;
    _server = nullptr;
}

///
/// \brief Applies \a limits and starts the server on a free port.
///
bool TestLimitsIntegration::startWithLimits(const QUaServerLimits &limits)
{
    return _server->setLimits(limits) && TestServer::start(*_server);
}

QString TestLimitsIntegration::url() const
{
    return TestServer::endpointUrl(*_server);
}

void TestLimitsIntegration::subscriptionsPerSessionAreLimited()
{
    QUaServerLimits limits = _server->limits();
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
    QUaServerLimits limits = _server->limits();
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
    QUaServerLimits limits = _server->limits();
    limits.maxMonitoredItemsPerSubscription = 1;
    QVERIFY(startWithLimits(limits));
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);

    QCOMPARE(client.monitorValue(_first->nodeId()), UA_STATUSCODE_GOOD);
    QCOMPARE(client.monitorValue(_second->nodeId()), UA_STATUSCODE_BADTOOMANYMONITOREDITEMS);
}

void TestLimitsIntegration::samplingIntervalIsRevisedIntoLimits()
{
    QUaServerLimits limits = _server->limits();
    limits.minSamplingInterval = 200.0;
    QVERIFY(startWithLimits(limits));
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);
    double revised = 0.0;

    QCOMPARE(client.monitorValue(_first->nodeId(), 10.0, revised), UA_STATUSCODE_GOOD);

    QCOMPARE(revised, 200.0);
}

void TestLimitsIntegration::nodesPerReadAreLimited()
{
    QUaServerLimits limits = _server->limits();
    limits.maxNodesPerRead = 2;
    QVERIFY(startWithLimits(limits));
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);

    QCOMPARE(client.readValues({ _first->nodeId(), _second->nodeId() }), UA_STATUSCODE_GOOD);
    QCOMPARE(client.readValues({ _first->nodeId(), _second->nodeId(), _third->nodeId() }),
             UA_STATUSCODE_BADTOOMANYOPERATIONS);
}

///
/// \brief open62541 resets its configuration on every start, so the limits must be applied again.
///
void TestLimitsIntegration::limitsSurviveRestart()
{
    QUaServerLimits limits = _server->limits();
    limits.maxNodesPerRead = 1;
    QVERIFY(startWithLimits(limits));
    _server->stop();
    QVERIFY(_server->start());
    TestClient client;
    QCOMPARE(client.connect(url()), UA_STATUSCODE_GOOD);

    QCOMPARE(client.readValues({ _first->nodeId(), _second->nodeId() }), UA_STATUSCODE_BADTOOMANYOPERATIONS);
}

QTEST_GUILESS_MAIN(TestLimitsIntegration)

#include "test_limits_integration.moc"
