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
    void anonymousClientConnectsAndDisconnects();
    void anonymousLoginCanBeDisabled();
    void endpointsOfferUserNameToken();
    void userLoginChecksPassword_data();
    void userLoginChecksPassword();
    void validationCallbackReplacesPasswordCheck();
    void closingSessionWithSubscriptionKeepsServerAlive();
    void stopDisconnectsClients();

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

    QVERIFY2(longestStep < 100, qPrintable(QStringLiteral("event loop stalled for %1 ms").arg(longestStep)));
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

QTEST_GUILESS_MAIN(TestSessionsIntegration)

#include "test_sessions_integration.moc"
