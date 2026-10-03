#include <QFile>
#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

namespace {

const QString kApplicationUri = QStringLiteral("urn:unconfigured:application");
const QString kNonePolicy = QStringLiteral("http://opcfoundation.org/UA/SecurityPolicy#None");
const QString kBasic256Sha256Policy = QStringLiteral("http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");

///
/// \brief Reads \a fileName from \a dir.
/// \return The file contents, or an empty array when it cannot be read.
///
QByteArray readFile(const QString &dir, const QString &fileName)
{
    QFile file(dir + QLatin1Char('/') + fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return file.readAll();
}

///
/// \brief Reads a file from the certificates directory of the encryption example.
///
QByteArray readCertificateFile(const QString &fileName)
{
    return readFile(QStringLiteral(QUASERVER_TEST_CERTIFICATES_DIR), fileName);
}

///
/// \brief Reads a file of the test PKI: a CA, a client and a revoked client it signed, its CRL and an unrelated certificate.
///
QByteArray readPkiFile(const QString &fileName)
{
    return readFile(QStringLiteral(QUASERVER_TEST_PKI_DIR), fileName);
}

///
/// \brief A client certificate of the test PKI, with its key and application URI.
///
struct ClientIdentity
{
    QByteArray certificate;
    QByteArray privateKey;
    QString applicationUri;
};

} // namespace

Q_DECLARE_METATYPE(ClientIdentity)

class TestEncryptionIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void certificateWithKeyOffersEncryptedEndpoints();
    void certificateWithoutKeyOffersOnlyNonePolicy();
    void encryptedSessionChecksPassword_data();
    void encryptedSessionChecksPassword();
    void passwordOverUnencryptedChannelIsRejected();
    void clientTrust_data();
    void clientTrust();
    void securityPoliciesAndModesFilterEndpoints();
    void noMatchingEndpointFailsToStart();
    void userCertificateTokenNeedsCallback_data();
    void userCertificateTokenNeedsCallback();
    void userCertificateMapsToUserName_data();
    void userCertificateMapsToUserName();
    void certificateUserPassesAccessChecks();
    void userCertificateMustBeTrusted_data();
    void userCertificateMustBeTrusted();

private:
    UA_StatusCode connectAsAlice(const ClientIdentity &identity);

    QByteArray m_certificate;
    QByteArray m_privateKey;
    QByteArray m_caCertificate;
    QByteArray m_caRevocationList;
    QByteArray m_untrustedCertificate;
    ClientIdentity m_client;
    ClientIdentity m_revokedClient;
    ClientIdentity m_user;
    ClientIdentity m_untrustedUser;

    UA_StatusCode connectAsUser(TestClient &client, const ClientIdentity &user);
    QUaServer *m_server = nullptr;
};

void TestEncryptionIntegration::initTestCase()
{
    m_certificate = readCertificateFile(QStringLiteral("server.crt.der"));
    m_privateKey = readCertificateFile(QStringLiteral("server.key.der"));
    m_caCertificate = readPkiFile(QStringLiteral("ca.crt.der"));
    m_caRevocationList = readPkiFile(QStringLiteral("ca.crl.der"));
    m_untrustedCertificate = readPkiFile(QStringLiteral("untrusted.crt.der"));
    m_client = { readPkiFile(QStringLiteral("client.crt.der")),
                 readPkiFile(QStringLiteral("client.key.der")),
                 QStringLiteral("urn:quaserver:test:client") };
    m_revokedClient = { readPkiFile(QStringLiteral("revoked.crt.der")),
                        readPkiFile(QStringLiteral("revoked.key.der")),
                        QStringLiteral("urn:quaserver:test:revoked") };
    QVERIFY(!m_certificate.isEmpty());
    QVERIFY(!m_privateKey.isEmpty());
    QVERIFY(!m_caCertificate.isEmpty());
    QVERIFY(!m_caRevocationList.isEmpty());
    QVERIFY(!m_untrustedCertificate.isEmpty());
    QVERIFY(!m_client.certificate.isEmpty() && !m_client.privateKey.isEmpty());
    QVERIFY(!m_revokedClient.certificate.isEmpty() && !m_revokedClient.privateKey.isEmpty());
    m_user = { readPkiFile(QStringLiteral("user.crt.der")),
               readPkiFile(QStringLiteral("user.key.der")),
               QStringLiteral("urn:quaserver:test:user") };
    m_untrustedUser = { m_untrustedCertificate, readPkiFile(QStringLiteral("untrusted.key.der")), QString() };
    QVERIFY(!m_user.certificate.isEmpty() && !m_user.privateKey.isEmpty());
    QVERIFY(!m_untrustedUser.privateKey.isEmpty());
}

///
/// \brief Opens a session authenticated with the certificate of \a user, over a channel secured with the test client certificate.
///
UA_StatusCode TestEncryptionIntegration::connectAsUser(TestClient &client, const ClientIdentity &user)
{
    const UA_StatusCode status = client.setEncryption(m_client.certificate, m_client.privateKey, m_client.applicationUri);
    if (status != UA_STATUSCODE_GOOD)
    {
        return status;
    }
    return client.connectCertificate(TestServer::endpointUrl(*m_server), user.certificate, user.privateKey);
}

UA_StatusCode TestEncryptionIntegration::connectAsAlice(const ClientIdentity &identity)
{
    TestClient client;
    return client.connectEncrypted(TestServer::endpointUrl(*m_server),
                                   identity.certificate,
                                   identity.privateKey,
                                   identity.applicationUri,
                                   QStringLiteral("alice"),
                                   QStringLiteral("secret"));
}

///
/// \brief Every test gets a fresh, stopped server whose application URI matches the certificate.
///
void TestEncryptionIntegration::init()
{
    m_server = new QUaServer;
    m_server->setApplicationUri(kApplicationUri);
}

void TestEncryptionIntegration::cleanup()
{
    delete m_server;
    m_server = nullptr;
}

void TestEncryptionIntegration::certificateWithKeyOffersEncryptedEndpoints()
{
    m_server->setCertificate(m_certificate);
    m_server->setPrivateKey(m_privateKey);
    QVERIFY(TestServer::start(*m_server));
    TestClient client;

    const QStringList encrypted =
        client.endpointSecurityPolicyUris(TestServer::endpointUrl(*m_server), UA_MESSAGESECURITYMODE_SIGNANDENCRYPT);

    QVERIFY(encrypted.contains(kBasic256Sha256Policy));
    QVERIFY(!encrypted.contains(kNonePolicy));
}

///
/// \brief Without a private key the server cannot encrypt, so it falls back to SecurityPolicy None.
///
void TestEncryptionIntegration::certificateWithoutKeyOffersOnlyNonePolicy()
{
    m_server->setCertificate(m_certificate);
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    const QString url = TestServer::endpointUrl(*m_server);

    QCOMPARE(client.endpointSecurityPolicyUris(url, UA_MESSAGESECURITYMODE_NONE), QStringList{ kNonePolicy });
    QVERIFY(client.endpointSecurityPolicyUris(url, UA_MESSAGESECURITYMODE_SIGN).isEmpty());
    QVERIFY(client.endpointSecurityPolicyUris(url, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT).isEmpty());
}

void TestEncryptionIntegration::encryptedSessionChecksPassword_data()
{
    QTest::addColumn<QString>("password");
    QTest::addColumn<bool>("accepted");

    QTest::newRow("correct password") << QStringLiteral("secret") << true;
    QTest::newRow("wrong password") << QStringLiteral("guess") << false;
}

void TestEncryptionIntegration::encryptedSessionChecksPassword()
{
    QFETCH(QString, password);
    QFETCH(bool, accepted);
    m_server->setCertificate(m_certificate);
    m_server->setPrivateKey(m_privateKey);
    m_server->setAnonymousLoginAllowed(false);
    m_server->addUser(QStringLiteral("alice"), QStringLiteral("secret"));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;

    const UA_StatusCode status = client.connectEncrypted(TestServer::endpointUrl(*m_server),
                                                         m_certificate,
                                                         m_privateKey,
                                                         kApplicationUri,
                                                         QStringLiteral("alice"),
                                                         password);

    QCOMPARE(status == UA_STATUSCODE_GOOD, accepted);
    if (accepted)
    {
        QTRY_COMPARE(m_server->sessions().count(), 1);
        QCOMPARE(m_server->sessions().first()->userName(), QStringLiteral("alice"));
    }
}

///
/// \brief Once encryption is available, passwords must not travel over SecurityPolicy None.
///
void TestEncryptionIntegration::passwordOverUnencryptedChannelIsRejected()
{
    m_server->setCertificate(m_certificate);
    m_server->setPrivateKey(m_privateKey);
    m_server->addUser(QStringLiteral("alice"), QStringLiteral("secret"));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;

    const UA_StatusCode status =
        client.connectUsername(TestServer::endpointUrl(*m_server), QStringLiteral("alice"), QStringLiteral("secret"));

    QVERIFY(status != UA_STATUSCODE_GOOD);
    QCOMPARE(m_server->sessions().count(), 0);
}

///
/// \brief A trusted client certificate needs its issuer to complete the chain; a trusted CA accepts what it signed and did not revoke.
///
void TestEncryptionIntegration::clientTrust_data()
{
    using Certificates = QList<QByteArray>;
    QTest::addColumn<ClientIdentity>("client");
    QTest::addColumn<Certificates>("trusted");
    QTest::addColumn<Certificates>("issuers");
    QTest::addColumn<bool>("accepted");

    QTest::newRow("empty trust list accepts any client")
        << m_client << Certificates() << Certificates() << true;
    QTest::newRow("trusted client with its issuer")
        << m_client << Certificates{ m_client.certificate } << Certificates{ m_caCertificate } << true;
    QTest::newRow("trusted client without its issuer")
        << m_client << Certificates{ m_client.certificate } << Certificates() << false;
    QTest::newRow("client of a trusted CA")
        << m_client << Certificates{ m_caCertificate } << Certificates() << true;
    QTest::newRow("revoked client of a trusted CA")
        << m_revokedClient << Certificates{ m_caCertificate } << Certificates() << false;
    QTest::newRow("unrelated certificate only")
        << m_client << Certificates{ m_untrustedCertificate } << Certificates() << false;
}

void TestEncryptionIntegration::clientTrust()
{
    QFETCH(ClientIdentity, client);
    QFETCH(QList<QByteArray>, trusted);
    QFETCH(QList<QByteArray>, issuers);
    QFETCH(bool, accepted);
    m_server->setCertificate(m_certificate);
    m_server->setPrivateKey(m_privateKey);
    m_server->setTrustedCertificates(trusted);
    m_server->setIssuerCertificates(issuers);
    m_server->setRevocationLists({ m_caRevocationList });
    m_server->setAnonymousLoginAllowed(false);
    m_server->addUser(QStringLiteral("alice"), QStringLiteral("secret"));
    QVERIFY(TestServer::start(*m_server));

    const UA_StatusCode status = connectAsAlice(client);

    QCOMPARE(status == UA_STATUSCODE_GOOD, accepted);
    if (!accepted)
    {
        QCOMPARE(m_server->sessions().count(), 0);
    }
}

void TestEncryptionIntegration::securityPoliciesAndModesFilterEndpoints()
{
    m_server->setCertificate(m_certificate);
    m_server->setPrivateKey(m_privateKey);
    m_server->setSecurityPolicies(QUaSecurityPolicy::Basic256Sha256);
    m_server->setSecurityModes(QUaMessageSecurityMode::SignAndEncrypt);
    m_server->addUser(QStringLiteral("alice"), QStringLiteral("secret"));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    // the open62541 client only discovers through an endpoint it can use, and None is not published
    QCOMPARE(client.setEncryption(m_client.certificate, m_client.privateKey, m_client.applicationUri), UA_STATUSCODE_GOOD);
    const QString url = TestServer::endpointUrl(*m_server);

    QVERIFY(client.endpointSecurityPolicyUris(url, UA_MESSAGESECURITYMODE_NONE).isEmpty());
    QVERIFY(client.endpointSecurityPolicyUris(url, UA_MESSAGESECURITYMODE_SIGN).isEmpty());
    QCOMPARE(client.endpointSecurityPolicyUris(url, UA_MESSAGESECURITYMODE_SIGNANDENCRYPT),
             QStringList{ kBasic256Sha256Policy });
    QCOMPARE(connectAsAlice(m_client), UA_STATUSCODE_GOOD);
}

///
/// \brief Without a private key only SecurityPolicy None exists, so requiring signing leaves no endpoint.
///
void TestEncryptionIntegration::noMatchingEndpointFailsToStart()
{
    m_server->setCertificate(m_certificate);
    m_server->setSecurityModes(QUaMessageSecurityMode::Sign | QUaMessageSecurityMode::SignAndEncrypt);

    QVERIFY(!TestServer::start(*m_server));
    QVERIFY(!m_server->isRunning());
}

///
/// \brief Certificate user tokens are only offered while a callback can map them to users.
///
void TestEncryptionIntegration::userCertificateTokenNeedsCallback_data()
{
    QTest::addColumn<bool>("withCallback");

    QTest::newRow("without callback") << false;
    QTest::newRow("with callback") << true;
}

void TestEncryptionIntegration::userCertificateTokenNeedsCallback()
{
    QFETCH(bool, withCallback);
    m_server->setCertificate(m_certificate);
    m_server->setPrivateKey(m_privateKey);
    if (withCallback)
    {
        m_server->setUserCertificateCallback([](const QByteArray &) { return QStringLiteral("anyone"); });
    }
    QVERIFY(TestServer::start(*m_server));
    TestClient client;

    const QList<UA_UserTokenType> tokenTypes = client.endpointUserTokenTypes(TestServer::endpointUrl(*m_server));

    QCOMPARE(tokenTypes.contains(UA_USERTOKENTYPE_CERTIFICATE), withCallback);
}

void TestEncryptionIntegration::userCertificateMapsToUserName_data()
{
    QTest::addColumn<QString>("mappedName");

    QTest::newRow("known certificate") << QStringLiteral("operator");
    QTest::newRow("rejected by callback") << QString();
}

void TestEncryptionIntegration::userCertificateMapsToUserName()
{
    QFETCH(QString, mappedName);
    m_server->setCertificate(m_certificate);
    m_server->setPrivateKey(m_privateKey);
    m_server->setAnonymousLoginAllowed(false);
    QByteArray seen;
    m_server->setUserCertificateCallback([&seen, mappedName](const QByteArray &certificate) {
        seen = certificate;
        return mappedName;
    });
    QVERIFY(TestServer::start(*m_server));
    TestClient client;

    const UA_StatusCode status = connectAsUser(client, m_user);

    QCOMPARE(seen, m_user.certificate);
    QCOMPARE(status == UA_STATUSCODE_GOOD, !mappedName.isEmpty());
    if (mappedName.isEmpty())
    {
        QCOMPARE(m_server->sessions().count(), 0);
        return;
    }
    QTRY_COMPARE(m_server->sessions().count(), 1);
    QCOMPARE(m_server->sessions().first()->userName(), mappedName);
}

///
/// \brief Regression guard: access callbacks deny users they do not know, so certificate users must be known.
///
void TestEncryptionIntegration::certificateUserPassesAccessChecks()
{
    m_server->setCertificate(m_certificate);
    m_server->setPrivateKey(m_privateKey);
    m_server->setUserCertificateCallback([](const QByteArray &) { return QStringLiteral("operator"); });
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("setpoint"));
    variable->setWriteAccess(true);
    variable->setValue(1);
    variable->setUserAccessLevelCallback([](const QString &userName) {
        QUaAccessLevel access;
        access.bits.bRead = true;
        access.bits.bWrite = userName == QStringLiteral("operator");
        return access;
    });
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(connectAsUser(client, m_user), UA_STATUSCODE_GOOD);

    QCOMPARE(client.writeValue(variable->nodeId(), 2), UA_STATUSCODE_GOOD);

    QCOMPARE(variable->value<int>(), 2);
    QVERIFY(!m_server->userNames().contains(QStringLiteral("operator")));
}

///
/// \brief With trust lists set, a user certificate is checked against them before the callback sees it.
///
void TestEncryptionIntegration::userCertificateMustBeTrusted_data()
{
    QTest::addColumn<ClientIdentity>("user");
    QTest::addColumn<bool>("accepted");

    QTest::newRow("user certificate signed by the trusted CA") << m_user << true;
    QTest::newRow("unrelated user certificate") << m_untrustedUser << false;
}

void TestEncryptionIntegration::userCertificateMustBeTrusted()
{
    QFETCH(ClientIdentity, user);
    QFETCH(bool, accepted);
    m_server->setCertificate(m_certificate);
    m_server->setPrivateKey(m_privateKey);
    m_server->setTrustedCertificates({ m_caCertificate });
    m_server->setRevocationLists({ m_caRevocationList });
    m_server->setAnonymousLoginAllowed(false);
    int calls = 0;
    m_server->setUserCertificateCallback([&calls](const QByteArray &) {
        ++calls;
        return QStringLiteral("anyone");
    });
    QVERIFY(TestServer::start(*m_server));
    TestClient client;

    const UA_StatusCode status = connectAsUser(client, user);

    QCOMPARE(status == UA_STATUSCODE_GOOD, accepted);
    QCOMPARE(calls, accepted ? 1 : 0);
}

QTEST_GUILESS_MAIN(TestEncryptionIntegration)

#include "test_encryption_integration.moc"
