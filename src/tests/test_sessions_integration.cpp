#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

class TestSessionsIntegration : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startAndStopToggleRunning();
    void runningServerKeepsEventLoopResponsive();
    void serverCanBeRestarted();
    void serverCanBeRestartedAfterClientDisconnects();
    void startFailsOnPortInUse();
    void anonymousClientConnectsAndDisconnects();
    void anonymousLoginCanBeDisabled();
    void endpointsOfferUserNameToken();
    void userLoginChecksPassword_data();
    void userLoginChecksPassword();
    void validationCallbackReplacesPasswordCheck();
    void closingSessionWithSubscriptionKeepsServerAlive();
    void stopDisconnectsClients();
    void anonymousSessionDescribesItsEndpoint();
    void userSessionIsNotAnonymous();
    void sessionDiagnosticsCountSubscriptions();
    void sessionDiagnosticsOfUnknownSessionAreInvalid();
    void subscriptionDiagnosticsListSessionSubscriptions();
    void subscriptionDiagnosticsOfUnknownSessionAreEmpty();

private:
    QUaServer *_server = nullptr;
};

///
/// \brief Every test gets a fresh, stopped server.
///
void TestSessionsIntegration::init()
{
    _server = new QUaServer;
}

///
/// \brief Deletes the per-test server, closing any session left open.
///
void TestSessionsIntegration::cleanup()
{
    delete _server;
    _server = nullptr;
}

///
/// \brief start() and stop() switch isRunning and announce each transition once.
///
void TestSessionsIntegration::startAndStopToggleRunning()
{
    QSignalSpy spy(_server, &QUaServer::isRunningChanged);

    QVERIFY(TestServer::start(*_server));
    QVERIFY(_server->isRunning());
    QVERIFY(_server->start());

    _server->stop();
    QVERIFY(!_server->isRunning());
    _server->stop();

    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).first().toBool(), true);
    QCOMPARE(spy.at(1).first().toBool(), false);
}

///
/// \brief Regression: an idle server blocked the Qt event loop for up to 500 ms per iteration (open62541 >= 1.4).
///
void TestSessionsIntegration::runningServerKeepsEventLoopResponsive()
{
    QVERIFY(TestServer::start(*_server));
    QElapsedTimer total;
    qint64 longestStep = 0;

    total.start();
    while (total.elapsed() < 300)
    {
        QElapsedTimer step;
        step.start();
        QCoreApplication::processEvents();
        longestStep = qMax(longestStep, step.elapsed());
    }

    // Allow CI scheduling jitter while still detecting the 500 ms blocking regression.
    QVERIFY2(longestStep < 250, qPrintable(QStringLiteral("event loop stalled for %1 ms").arg(longestStep)));
}

///
/// \brief A stopped server accepts clients again after the next start().
///
void TestSessionsIntegration::serverCanBeRestarted()
{
    QVERIFY(TestServer::start(*_server));
    _server->stop();
    QVERIFY(_server->start());

    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
}

///
/// \brief A server accepts clients after stopping and restarting with a previously connected client.
///
void TestSessionsIntegration::serverCanBeRestartedAfterClientDisconnects()
{
    QVERIFY(TestServer::start(*_server));
    {
        TestClient client;
        QCOMPARE(client.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
        QCOMPARE(client.disconnect(), UA_STATUSCODE_GOOD);
    }
    _server->stop();
    QVERIFY(_server->start());

    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
}

///
/// \brief A second server must not share an occupied listen port on any platform.
///
void TestSessionsIntegration::startFailsOnPortInUse()
{
    QVERIFY(TestServer::start(*_server));

    QUaServer second;
    second.setPort(_server->port());
    QSignalSpy runningSpy(&second, &QUaServer::isRunningChanged);
    QVERIFY(!second.start());
    QVERIFY(!second.isRunning());
    QCOMPARE(runningSpy.count(), 0);

    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
}

///
/// \brief An anonymous session is reported on connect and on disconnect.
///
void TestSessionsIntegration::anonymousClientConnectsAndDisconnects()
{
    QSignalSpy connectedSpy(_server, &QUaServer::clientConnected);
    QSignalSpy disconnectedSpy(_server, &QUaServer::clientDisconnected);
    QVERIFY(TestServer::start(*_server));
    TestClient client;

    QCOMPARE(client.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);

    QTRY_COMPARE(connectedSpy.count(), 1);
    QCOMPARE(_server->sessions().count(), 1);
    const QUaSession *session = _server->sessions().first();
    QVERIFY(!session->sessionId().isEmpty());
    QVERIFY(!session->address().isEmpty());
    QVERIFY(session->port() != 0);

    QCOMPARE(client.disconnect(), UA_STATUSCODE_GOOD);

    QTRY_COMPARE(disconnectedSpy.count(), 1);
    QVERIFY(_server->sessions().isEmpty());
}

///
/// \brief Without anonymous login only authenticated clients get a session.
///
void TestSessionsIntegration::anonymousLoginCanBeDisabled()
{
    _server->setAnonymousLoginAllowed(false);
    _server->addUser(QStringLiteral("alice"), QStringLiteral("secret"));
    QVERIFY(TestServer::start(*_server));
    TestClient anonymous;
    TestClient authenticated;

    QVERIFY(anonymous.connect(TestServer::endpointUrl(*_server)) != UA_STATUSCODE_GOOD);
    QCOMPARE(authenticated.connectUsername(TestServer::endpointUrl(*_server),
                                           QStringLiteral("alice"), QStringLiteral("secret")),
             UA_STATUSCODE_GOOD);
}

///
/// \brief Regression: without configured users the endpoints lacked the UserName token policy.
///
void TestSessionsIntegration::endpointsOfferUserNameToken()
{
    QVERIFY(TestServer::start(*_server));
    TestClient client;

    const QList<UA_UserTokenType> tokenTypes = client.endpointUserTokenTypes(TestServer::endpointUrl(*_server));

    QVERIFY(tokenTypes.contains(UA_USERTOKENTYPE_ANONYMOUS));
    QVERIFY(tokenTypes.contains(UA_USERTOKENTYPE_USERNAME));
}

void TestSessionsIntegration::userLoginChecksPassword_data()
{
    QTest::addColumn<QString>("userName");
    QTest::addColumn<QString>("password");
    QTest::addColumn<bool>("accepted");

    QTest::newRow("valid") << QStringLiteral("alice") << QStringLiteral("secret") << true;
    QTest::newRow("wrong password") << QStringLiteral("alice") << QStringLiteral("guess") << false;
    QTest::newRow("unknown user") << QStringLiteral("mallory") << QStringLiteral("secret") << false;
}

///
/// \brief Only a known user with the matching password gets a session, which carries the user name.
///
void TestSessionsIntegration::userLoginChecksPassword()
{
    QFETCH(QString, userName);
    QFETCH(QString, password);
    QFETCH(bool, accepted);
    _server->addUser(QStringLiteral("alice"), QStringLiteral("secret"));
    QVERIFY(TestServer::start(*_server));
    TestClient client;

    const UA_StatusCode status = client.connectUsername(TestServer::endpointUrl(*_server), userName, password);

    QCOMPARE(status == UA_STATUSCODE_GOOD, accepted);
    if (accepted)
    {
        QTRY_COMPARE(_server->sessions().count(), 1);
        QCOMPARE(_server->sessions().first()->userName(), userName);
        QVERIFY(!_server->sessions().first()->isAnonymous());
    }
}

///
/// \brief A validation callback decides instead of comparing the stored key with the password.
///
void TestSessionsIntegration::validationCallbackReplacesPasswordCheck()
{
    _server->addUser(QStringLiteral("alice"), QStringLiteral("stored-key"));
    _server->setUserValidationCallback([](const QString &userName, const QString &password) {
        return userName == QStringLiteral("alice") && password == QStringLiteral("token-123");
    });
    QVERIFY(TestServer::start(*_server));
    TestClient storedKey;
    TestClient token;

    QVERIFY(storedKey.connectUsername(TestServer::endpointUrl(*_server),
                                      QStringLiteral("alice"), QStringLiteral("stored-key")) != UA_STATUSCODE_GOOD);
    QCOMPARE(token.connectUsername(TestServer::endpointUrl(*_server),
                                   QStringLiteral("alice"), QStringLiteral("token-123")),
             UA_STATUSCODE_GOOD);
}

///
/// \brief Regression: closing a session that owns a subscription crashed on open62541 node contexts.
///
void TestSessionsIntegration::closingSessionWithSubscriptionKeepsServerAlive()
{
    QUaBaseDataVariable *variable = _server->objectsFolder()->addBaseDataVariable(
        QStringLiteral("watched"), QUaNodeId(1, QStringLiteral("watched")));
    variable->setValue(1);
    QSignalSpy disconnectedSpy(_server, &QUaServer::clientDisconnected);
    QVERIFY(TestServer::start(*_server));
    {
        TestClient client;
        QCOMPARE(client.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
        QCOMPARE(client.monitorValue(variable->nodeId()), UA_STATUSCODE_GOOD);
        QCOMPARE(client.disconnect(), UA_STATUSCODE_GOOD);
    }
    QTRY_COMPARE(disconnectedSpy.count(), 1);

    TestClient next;
    QVariant value;
    QCOMPARE(next.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
    QCOMPARE(next.readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);
    QCOMPARE(value.toInt(), 1);
}

///
/// \brief Stopping the server closes the open sessions and reports them as disconnected.
///
void TestSessionsIntegration::stopDisconnectsClients()
{
    QSignalSpy disconnectedSpy(_server, &QUaServer::clientDisconnected);
    QVERIFY(TestServer::start(*_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
    QTRY_COMPARE(_server->sessions().count(), 1);

    _server->stop();

    QCOMPARE(disconnectedSpy.count(), 1);
    QVERIFY(_server->sessions().isEmpty());
}

///
/// \brief An anonymous session reports itself as anonymous and the unsecured endpoint it was activated on.
///
void TestSessionsIntegration::anonymousSessionDescribesItsEndpoint()
{
    QVERIFY(TestServer::start(*_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);

    QTRY_COMPARE(_server->sessions().count(), 1);
    const QUaSession *session = _server->sessions().first();
    QVERIFY(session->isAnonymous());
    QCOMPARE(session->securityMode(), QUaMessageSecurityMode::None);
    QCOMPARE(session->securityPolicyUri(), QStringLiteral("http://opcfoundation.org/UA/SecurityPolicy#None"));
    QVERIFY(session->endpointUrl().startsWith(QStringLiteral("opc.tcp://")));
}

///
/// \brief A session logged in with a user name is not anonymous.
///
void TestSessionsIntegration::userSessionIsNotAnonymous()
{
    _server->addUser(QStringLiteral("alice"), QStringLiteral("secret"));
    QVERIFY(TestServer::start(*_server));
    TestClient client;
    QCOMPARE(client.connectUsername(TestServer::endpointUrl(*_server), QStringLiteral("alice"), QStringLiteral("secret")),
             UA_STATUSCODE_GOOD);

    QTRY_COMPARE(_server->sessions().count(), 1);
    QVERIFY(!_server->sessions().first()->isAnonymous());
}

///
/// \brief The diagnostics of a session count its subscriptions and monitored items.
///
void TestSessionsIntegration::sessionDiagnosticsCountSubscriptions()
{
    QUaBaseDataVariable *variable = _server->objectsFolder()->addBaseDataVariable(
        QStringLiteral("watched"), QUaNodeId(1, QStringLiteral("watched")));
    variable->setValue(1);
    QVERIFY(TestServer::start(*_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
    QTRY_COMPARE(_server->sessions().count(), 1);
    const QUaSession *session = _server->sessions().first();

    QUaSessionDiagnostics diagnostics = _server->sessionDiagnostics(session);
    QVERIFY(diagnostics.isValid);
    QCOMPARE(diagnostics.subscriptionsCount, 0u);

    QCOMPARE(client.monitorValue(variable->nodeId()), UA_STATUSCODE_GOOD);
    diagnostics = _server->sessionDiagnostics(session);
    QCOMPARE(diagnostics.subscriptionsCount, 1u);
    QCOMPARE(diagnostics.monitoredItemsCount, 1u);
    QVERIFY(diagnostics.connectionTime.isValid());
    QVERIFY(diagnostics.lastContactTime >= diagnostics.connectionTime);
}

///
/// \brief Diagnostics are invalid for a session the server does not know.
///
void TestSessionsIntegration::sessionDiagnosticsOfUnknownSessionAreInvalid()
{
    QVERIFY(TestServer::start(*_server));
    QUaSession stranger;
    QVERIFY(!_server->sessionDiagnostics(&stranger).isValid);
    QVERIFY(!_server->sessionDiagnostics(nullptr).isValid);
}

///
/// \brief The subscription diagnostics of a session list only its own subscriptions with their settings.
///
void TestSessionsIntegration::subscriptionDiagnosticsListSessionSubscriptions()
{
    QUaBaseDataVariable *variable = _server->objectsFolder()->addBaseDataVariable(
        QStringLiteral("watched"), QUaNodeId(1, QStringLiteral("watched")));
    variable->setValue(1);
    QVERIFY(TestServer::start(*_server));
    TestClient watcher;
    QCOMPARE(watcher.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
    QTRY_COMPARE(_server->sessions().count(), 1);
    const QUaSession *watching = _server->sessions().first();
    QVERIFY(_server->subscriptionDiagnostics(watching).isEmpty());

    TestClient idle;
    QCOMPARE(idle.connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
    QTRY_COMPARE(_server->sessions().count(), 2);
    const QUaSession *idling = _server->sessions().first() == watching ? _server->sessions().last()
                                                                       : _server->sessions().first();

    QCOMPARE(watcher.monitorValue(variable->nodeId()), UA_STATUSCODE_GOOD);
    const QList<QUaSubscriptionDiagnostics> subscriptions = _server->subscriptionDiagnostics(watching);
    QCOMPARE(subscriptions.count(), 1);
    QVERIFY(subscriptions.first().subscriptionId > 0);
    QVERIFY(subscriptions.first().publishingInterval > 0.0);
    QVERIFY(subscriptions.first().publishingEnabled);
    QCOMPARE(subscriptions.first().monitoredItemCount, 1u);
    QVERIFY(_server->subscriptionDiagnostics(idling).isEmpty());
}

///
/// \brief Subscription diagnostics are empty for a session the server does not know.
///
void TestSessionsIntegration::subscriptionDiagnosticsOfUnknownSessionAreEmpty()
{
    QVERIFY(TestServer::start(*_server));
    QUaSession stranger;
    QVERIFY(_server->subscriptionDiagnostics(&stranger).isEmpty());
    QVERIFY(_server->subscriptionDiagnostics(nullptr).isEmpty());
}

QTEST_GUILESS_MAIN(TestSessionsIntegration)

#include "test_sessions_integration.moc"
