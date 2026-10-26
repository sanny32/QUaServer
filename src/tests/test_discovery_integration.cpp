#include <thread>

#include <QFile>
#include <QHash>
#include <QSignalSpy>
#include <QTest>

#include <QUaServer>

#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>
#include <open62541/plugin/log_stdout.h>
#include <open62541/server_config_default.h>

#include "testserver.h"

namespace {

///
/// \brief A Local Discovery Server on a free port, run by open62541 in its own thread so it answers while
///        QUaServer::stop() blocks the test thread.
///
class DiscoveryServer
{
public:
    ~DiscoveryServer() { stop(); }

    ///
    /// \brief Starts listening; with \a certificate and \a privateKey it also offers encrypted endpoints.
    ///
    bool start(const QByteArray &certificate = QByteArray(), const QByteArray &privateKey = QByteArray(),
               const QString &applicationUri = QString())
    {
        _port = TestServer::freePort();
        UA_ServerConfig config;
        memset(&config, 0, sizeof(UA_ServerConfig));
        config.logging = UA_Log_Stdout_new(UA_LOGLEVEL_WARNING);
        UA_StatusCode st = UA_STATUSCODE_GOOD;
#ifdef UA_ENABLE_ENCRYPTION
        if (!certificate.isEmpty())
        {
            const UA_ByteString cert = byteString(certificate);
            const UA_ByteString key = byteString(privateKey);
            st = UA_ServerConfig_setDefaultWithSecurityPolicies(&config, _port, &cert, &key,
                                                                nullptr, 0, nullptr, 0, nullptr, 0);
        }
        else
#endif // UA_ENABLE_ENCRYPTION
        {
            Q_UNUSED(certificate);
            Q_UNUSED(privateKey);
            st = UA_ServerConfig_setMinimal(&config, _port, nullptr);
        }
        if (st != UA_STATUSCODE_GOOD)
        {
            UA_ServerConfig_clear(&config);
            return false;
        }
        config.applicationDescription.applicationType = UA_APPLICATIONTYPE_DISCOVERYSERVER;
        if (!applicationUri.isEmpty())
        {
            UA_String_clear(&config.applicationDescription.applicationUri);
            config.applicationDescription.applicationUri = UA_STRING_ALLOC(applicationUri.toUtf8().constData());
        }
        _server = UA_Server_newWithConfig(&config);
        if (!_server)
        {
            return false;
        }
        _running = true;
        _thread = std::thread([this] { UA_Server_run(_server, &_running); });
        return true;
    }

    void stop()
    {
        if (!_server)
        {
            return;
        }
        _running = false;
        _thread.join();
        UA_Server_delete(_server);
        _server = nullptr;
    }

    QString url() const { return QStringLiteral("opc.tcp://127.0.0.1:%1").arg(_port); }

    ///
    /// \brief Asks the discovery server with FindServers for the servers registered online.
    /// \return Number of discovery URLs of each registered server, by application URI.
    ///
    QHash<QString, int> registeredServers() const
    {
        UA_ClientConfig config;
        memset(&config, 0, sizeof(UA_ClientConfig));
        config.logging = UA_Log_Stdout_new(UA_LOGLEVEL_WARNING);
        UA_ClientConfig_setDefault(&config);
        UA_Client *client = UA_Client_newWithConfig(&config);
        size_t serversSize = 0;
        UA_ApplicationDescription *servers = nullptr;
        QHash<QString, int> registered;
        const UA_StatusCode st = UA_Client_findServers(client, url().toUtf8().constData(), 0, nullptr, 0, nullptr,
                                                       &serversSize, &servers);
        for (size_t i = 0; st == UA_STATUSCODE_GOOD && i < serversSize; ++i)
        {
            if (servers[i].applicationType == UA_APPLICATIONTYPE_DISCOVERYSERVER)
            {
                continue;
            }
            const UA_String &uri = servers[i].applicationUri;
            registered.insert(QString::fromUtf8(reinterpret_cast<const char *>(uri.data), static_cast<int>(uri.length)),
                              static_cast<int>(servers[i].discoveryUrlsSize));
        }
        UA_Array_delete(servers, serversSize, &UA_TYPES[UA_TYPES_APPLICATIONDESCRIPTION]);
        UA_Client_delete(client);
        return registered;
    }

private:
    static UA_ByteString byteString(const QByteArray &bytes)
    {
        UA_ByteString string;
        string.length = static_cast<size_t>(bytes.size());
        string.data = reinterpret_cast<UA_Byte *>(const_cast<char *>(bytes.constData()));
        return string;
    }

    UA_Server *_server = nullptr;
    quint16 _port = 0;
    volatile UA_Boolean _running = false;
    std::thread _thread;
};

#ifdef UA_ENABLE_ENCRYPTION
///
/// \brief Reads a file of the test PKI or, with \a pkiDir false, of the encryption example.
///
QByteArray readFile(const QString &fileName, bool pkiDir = true)
{
    QFile file((pkiDir ? QStringLiteral(QUASERVER_TEST_PKI_DIR) : QStringLiteral(QUASERVER_TEST_CERTIFICATES_DIR))
               + QLatin1Char('/') + fileName);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
#endif // UA_ENABLE_ENCRYPTION

///
/// \brief Returns the status argument of a recorded QUaServer::discoveryRegistrationFinished signal.
///
QUaStatus statusOf(const QList<QVariant> &arguments)
{
    return arguments.at(1).value<QUaStatusCode>();
}

constexpr int kWaitMs = 10000;

} // namespace

class TestDiscoveryIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void registersAtDiscoveryServer();
    void stopRemovesRegistration();
    void renewsRegistration();
    void emptyUrlRegistersNowhere();
    void unreachableDiscoveryServerFails();
    void restartRegistersAgain();
#ifdef UA_ENABLE_ENCRYPTION
    void encryptedRegistrationChecksTrust_data();
    void encryptedRegistrationChecksTrust();
    void registersAtExternalDiscoveryServer();
#endif // UA_ENABLE_ENCRYPTION

private:
    QUaServer *_server = nullptr;
    DiscoveryServer *_lds = nullptr;
};

void TestDiscoveryIntegration::initTestCase()
{
    qRegisterMetaType<QUaStatusCode>("QUaStatusCode");
}

///
/// \brief Every test gets a stopped server and a stopped discovery server.
///
void TestDiscoveryIntegration::init()
{
    _server = new QUaServer();
    _lds = new DiscoveryServer();
}

void TestDiscoveryIntegration::cleanup()
{
    delete _server;
    _server = nullptr;
    delete _lds;
    _lds = nullptr;
}

///
/// \brief start() registers the server online with its application URI and discovery URLs.
///
void TestDiscoveryIntegration::registersAtDiscoveryServer()
{
    QVERIFY(_lds->start());
    _server->setDiscoveryServerUrl(_lds->url());
    QSignalSpy spy(_server, &QUaServer::discoveryRegistrationFinished);
    QVERIFY(TestServer::start(*_server));

    QVERIFY(spy.wait(kWaitMs));
    QCOMPARE(spy.first().at(0).toBool(), true);
    QCOMPARE(statusOf(spy.first()), QUaStatus::Good);
    const QHash<QString, int> registered = _lds->registeredServers();
    QCOMPARE(registered.keys(), QStringList{ _server->applicationUri() });
    QVERIFY(registered.value(_server->applicationUri()) > 0);
}

///
/// \brief stop() tells the discovery server that the server went offline before shutting down.
///
void TestDiscoveryIntegration::stopRemovesRegistration()
{
    QVERIFY(_lds->start());
    _server->setDiscoveryServerUrl(_lds->url());
    QSignalSpy spy(_server, &QUaServer::discoveryRegistrationFinished);
    QVERIFY(TestServer::start(*_server));
    QVERIFY(spy.wait(kWaitMs));
    QVERIFY(!_lds->registeredServers().isEmpty());

    _server->stop();

    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
    QCOMPARE(statusOf(spy.at(1)), QUaStatus::Good);
    QVERIFY(_lds->registeredServers().isEmpty());
}

///
/// \brief The registration is repeated every discoveryRegistrationInterval().
///
void TestDiscoveryIntegration::renewsRegistration()
{
    QVERIFY(_lds->start());
    _server->setDiscoveryServerUrl(_lds->url());
    _server->setDiscoveryRegistrationInterval(200);
    QSignalSpy spy(_server, &QUaServer::discoveryRegistrationFinished);
    QVERIFY(TestServer::start(*_server));

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 3, kWaitMs);
    for (const QList<QVariant> &arguments : spy)
    {
        QCOMPARE(arguments.at(0).toBool(), true);
        QCOMPARE(statusOf(arguments), QUaStatus::Good);
    }
}

///
/// \brief Without a discovery server URL nothing is registered or unregistered.
///
void TestDiscoveryIntegration::emptyUrlRegistersNowhere()
{
    QVERIFY(_lds->start());
    QCOMPARE(_server->discoveryServerUrl(), QString());
    QSignalSpy spy(_server, &QUaServer::discoveryRegistrationFinished);
    QVERIFY(TestServer::start(*_server));

    QVERIFY(!spy.wait(500));
    QVERIFY(_lds->registeredServers().isEmpty());
    _server->stop();
    QCOMPARE(spy.count(), 0);
}

///
/// \brief A URL nobody listens at reports failed requests, and the server keeps running.
///
void TestDiscoveryIntegration::unreachableDiscoveryServerFails()
{
    _server->setDiscoveryServerUrl(QStringLiteral("opc.tcp://127.0.0.1:%1").arg(TestServer::freePort()));
    QSignalSpy spy(_server, &QUaServer::discoveryRegistrationFinished);
    QVERIFY(TestServer::start(*_server));

    QVERIFY(spy.wait(kWaitMs));
    QCOMPARE(spy.first().at(0).toBool(), true);
    QVERIFY(statusOf(spy.first()) != QUaStatus::Good);
    QVERIFY(_server->isRunning());

    _server->stop();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toBool(), false);
    QVERIFY(statusOf(spy.at(1)) != QUaStatus::Good);
}

///
/// \brief A restarted server registers at the URL set while it was stopped.
///
void TestDiscoveryIntegration::restartRegistersAgain()
{
    QVERIFY(_lds->start());
    QSignalSpy spy(_server, &QUaServer::discoveryRegistrationFinished);
    QVERIFY(TestServer::start(*_server));
    _server->stop();
    QCOMPARE(spy.count(), 0);

    _server->setDiscoveryServerUrl(_lds->url());
    QVERIFY(_server->start());
    QVERIFY(spy.wait(kWaitMs));
    QCOMPARE(statusOf(spy.first()), QUaStatus::Good);
    QCOMPARE(_lds->registeredServers().count(), 1);
}

#ifdef UA_ENABLE_ENCRYPTION
void TestDiscoveryIntegration::encryptedRegistrationChecksTrust_data()
{
    QTest::addColumn<QByteArray>("ldsCertificate");
    QTest::addColumn<QByteArray>("ldsKey");
    QTest::addColumn<QString>("ldsUri");
    QTest::addColumn<bool>("trustedOnly");
    QTest::addColumn<bool>("accepted");

    const QByteArray caSigned = readFile(QStringLiteral("client.crt.der"));
    const QByteArray caSignedKey = readFile(QStringLiteral("client.key.der"));
    const QString caSignedUri = QStringLiteral("urn:quaserver:test:client");
    // signed by another CA, the one of the encryption example
    const QByteArray foreign = readFile(QStringLiteral("server.crt.der"), false);
    const QByteArray foreignKey = readFile(QStringLiteral("server.key.der"), false);
    const QString foreignUri = QStringLiteral("urn:unconfigured:application");

    QTest::newRow("any discovery server while the trust list is empty")
        << foreign << foreignKey << foreignUri << false << true;
    QTest::newRow("discovery server signed by a trusted CA")
        << caSigned << caSignedKey << caSignedUri << true << true;
    QTest::newRow("untrusted discovery server")
        << foreign << foreignKey << foreignUri << true << false;
}

///
/// \brief A server with a certificate registers over an encrypted channel and checks the discovery server against
///        its trusted certificates, like it checks clients.
///
void TestDiscoveryIntegration::encryptedRegistrationChecksTrust()
{
    QFETCH(QByteArray, ldsCertificate);
    QFETCH(QByteArray, ldsKey);
    QFETCH(QString, ldsUri);
    QFETCH(bool, trustedOnly);
    QFETCH(bool, accepted);

    QVERIFY(_lds->start(ldsCertificate, ldsKey, ldsUri));
    _server->setCertificate(readFile(QStringLiteral("server.crt.der"), false));
    _server->setPrivateKey(readFile(QStringLiteral("server.key.der"), false));
    _server->setApplicationUri(QStringLiteral("urn:unconfigured:application"));
    if (trustedOnly)
    {
        _server->setTrustedCertificates({ readFile(QStringLiteral("ca.crt.der")) });
        _server->setRevocationLists({ readFile(QStringLiteral("ca.crl.der")) });
        _server->setRejectUntrustedCertificates(true);
    }
    _server->setDiscoveryServerUrl(_lds->url());
    QSignalSpy spy(_server, &QUaServer::discoveryRegistrationFinished);
    QVERIFY(TestServer::start(*_server));

    QVERIFY(spy.wait(kWaitMs));
    QCOMPARE(statusOf(spy.first()) == QUaStatus::Good, accepted);
    QCOMPARE(_lds->registeredServers().count(), accepted ? 1 : 0);
}

///
/// \brief Registers at the discovery server named by QUASERVER_TEST_LDS_URL, e.g. the OPC Foundation LDS, which
///        answers FindServers and GetEndpoints with another URL and accepts only encrypted registrations. That
///        server must trust server.crt.der of the encryption example; skipped when the variable is not set.
///
void TestDiscoveryIntegration::registersAtExternalDiscoveryServer()
{
    const QString url = qEnvironmentVariable("QUASERVER_TEST_LDS_URL");
    if (url.isEmpty())
    {
        QSKIP("Set QUASERVER_TEST_LDS_URL to register at an external discovery server");
    }
    _server->setCertificate(readFile(QStringLiteral("server.crt.der"), false));
    _server->setPrivateKey(readFile(QStringLiteral("server.key.der"), false));
    _server->setApplicationUri(QStringLiteral("urn:unconfigured:application"));
    _server->setDiscoveryServerUrl(url);
    QSignalSpy spy(_server, &QUaServer::discoveryRegistrationFinished);
    QVERIFY(TestServer::start(*_server));

    QVERIFY(spy.wait(kWaitMs));
    QCOMPARE(QString(spy.first().at(1).value<QUaStatusCode>()), QStringLiteral("Good"));
    _server->stop();
    QCOMPARE(spy.count(), 2);
    QCOMPARE(QString(spy.at(1).at(1).value<QUaStatusCode>()), QStringLiteral("Good"));
}
#endif // UA_ENABLE_ENCRYPTION

QTEST_GUILESS_MAIN(TestDiscoveryIntegration)

#include "test_discovery_integration.moc"
