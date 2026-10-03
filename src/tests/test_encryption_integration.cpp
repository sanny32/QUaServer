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
/// \brief Reads a file from the certificates directory of the encryption example.
///
QByteArray readCertificateFile(const QString &fileName)
{
    QFile file(QStringLiteral(QUASERVER_TEST_CERTIFICATES_DIR "/") + fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        return QByteArray();
    }
    return file.readAll();
}

} // namespace

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

private:
    QByteArray m_certificate;
    QByteArray m_privateKey;
    QUaServer *m_server = nullptr;
};

void TestEncryptionIntegration::initTestCase()
{
    m_certificate = readCertificateFile(QStringLiteral("server.crt.der"));
    m_privateKey = readCertificateFile(QStringLiteral("server.key.der"));
    QVERIFY(!m_certificate.isEmpty());
    QVERIFY(!m_privateKey.isEmpty());
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

QTEST_GUILESS_MAIN(TestEncryptionIntegration)

#include "test_encryption_integration.moc"
