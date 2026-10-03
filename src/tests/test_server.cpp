#include <QSignalSpy>
#include <QTest>

#include <QUaServer>

class Valve : public QObject
{
    Q_OBJECT

public:
    enum class Position
    {
        Closed = 0,
        Open = 1,
        Fault = 10
    };
    Q_ENUM(Position)
};

class TestServer : public QObject
{
    Q_OBJECT

private slots:
    void descriptionPropertiesNotify();
    void limitsNotify();
    void hostnameIsEmptyByDefaultAndNotifies();
#ifdef UA_ENABLE_ENCRYPTION
    void trustListsAreEmptyByDefaultAndNotify();
    void allSecurityPoliciesAndModesAreAllowedByDefault();
    void securityPoliciesAndModesNotify();
#endif // UA_ENABLE_ENCRYPTION
    void anonymousLoginIsAllowedByDefault();
    void usersAddUpdateAndRemove();
    void emptyUserNameIsIgnored();
    void qtEnumRegistersItsKeys();
    void enumMapCanBeEdited();
    void registeringEnumTwiceKeepsFirstMap();
    void referenceTypeRegistersOnce();
    void nodeIdUsageReflectsAddressSpace();
    void objectsFolderIsNamespaceZeroFolder();
};

///
/// \brief Description setters store the value and emit their change signal.
///
void TestServer::descriptionPropertiesNotify()
{
    QUaServer server;
    QSignalSpy nameSpy(&server, &QUaServer::applicationNameChanged);
    QSignalSpy uriSpy(&server, &QUaServer::applicationUriChanged);

    server.setApplicationName(QStringLiteral("Test Server"));
    server.setApplicationUri(QStringLiteral("urn:quaserver:test"));
    server.setProductName(QStringLiteral("Product"));
    server.setManufacturerName(QStringLiteral("Manufacturer"));
    server.setSoftwareVersion(QStringLiteral("1.2.3"));
    server.setBuildNumber(QStringLiteral("42"));

    QCOMPARE(server.applicationName(), QStringLiteral("Test Server"));
    QCOMPARE(server.applicationUri(), QStringLiteral("urn:quaserver:test"));
    QCOMPARE(server.productName(), QStringLiteral("Product"));
    QCOMPARE(server.manufacturerName(), QStringLiteral("Manufacturer"));
    QCOMPARE(server.softwareVersion(), QStringLiteral("1.2.3"));
    QCOMPARE(server.buildNumber(), QStringLiteral("42"));
    QCOMPARE(nameSpy.count(), 1);
    QCOMPARE(nameSpy.first().first().toString(), QStringLiteral("Test Server"));
    QCOMPARE(uriSpy.count(), 1);
}

///
/// \brief Session and channel limits are stored and announced.
///
void TestServer::limitsNotify()
{
    QUaServer server;
    QSignalSpy sessionsSpy(&server, &QUaServer::maxSessionsChanged);

    server.setMaxSessions(7);
    server.setMaxSecureChannels(9);

    QCOMPARE(server.maxSessions(), quint16(7));
    QCOMPARE(server.maxSecureChannels(), quint16(9));
    QCOMPARE(sessionsSpy.count(), 1);
}

///
/// \brief An empty hostname keeps listening on all interfaces until one is set.
///
void TestServer::hostnameIsEmptyByDefaultAndNotifies()
{
    QUaServer server;
    QSignalSpy spy(&server, &QUaServer::hostnameChanged);

    QVERIFY(server.hostname().isEmpty());
    server.setHostname(QStringLiteral("plc.local"));

    QCOMPARE(server.hostname(), QStringLiteral("plc.local"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toString(), QStringLiteral("plc.local"));
}

#ifdef UA_ENABLE_ENCRYPTION
void TestServer::trustListsAreEmptyByDefaultAndNotify()
{
    QUaServer server;
    QSignalSpy trustedSpy(&server, &QUaServer::trustedCertificatesChanged);
    QSignalSpy issuersSpy(&server, &QUaServer::issuerCertificatesChanged);
    QSignalSpy revocationSpy(&server, &QUaServer::revocationListsChanged);
    const QList<QByteArray> trusted{ QByteArray("client") };
    const QList<QByteArray> issuers{ QByteArray("ca") };
    const QList<QByteArray> revocation{ QByteArray("crl") };

    QVERIFY(server.trustedCertificates().isEmpty());
    QVERIFY(server.issuerCertificates().isEmpty());
    QVERIFY(server.revocationLists().isEmpty());
    server.setTrustedCertificates(trusted);
    server.setIssuerCertificates(issuers);
    server.setRevocationLists(revocation);

    QCOMPARE(server.trustedCertificates(), trusted);
    QCOMPARE(server.issuerCertificates(), issuers);
    QCOMPARE(server.revocationLists(), revocation);
    QCOMPARE(trustedSpy.count(), 1);
    QCOMPARE(issuersSpy.count(), 1);
    QCOMPARE(revocationSpy.count(), 1);
}

void TestServer::allSecurityPoliciesAndModesAreAllowedByDefault()
{
    QUaServer server;

    QCOMPARE(server.securityPolicies(), QUaSecurityPolicies(QUaSecurityPolicy::All));
    QVERIFY(server.securityPolicies().testFlag(QUaSecurityPolicy::None));
    QVERIFY(server.securityPolicies().testFlag(QUaSecurityPolicy::EccCurve448));
    QCOMPARE(server.securityModes(), QUaMessageSecurityModes(QUaMessageSecurityMode::All));
    QVERIFY(server.securityModes().testFlag(QUaMessageSecurityMode::Sign));
}

void TestServer::securityPoliciesAndModesNotify()
{
    QUaServer server;
    QSignalSpy policiesSpy(&server, &QUaServer::securityPoliciesChanged);
    QSignalSpy modesSpy(&server, &QUaServer::securityModesChanged);
    const QUaSecurityPolicies policies = QUaSecurityPolicy::Basic256Sha256 | QUaSecurityPolicy::Aes256Sha256RsaPss;

    server.setSecurityPolicies(policies);
    server.setSecurityModes(QUaMessageSecurityMode::SignAndEncrypt);

    QCOMPARE(server.securityPolicies(), policies);
    QCOMPARE(server.securityModes(), QUaMessageSecurityModes(QUaMessageSecurityMode::SignAndEncrypt));
    QCOMPARE(policiesSpy.count(), 1);
    QCOMPARE(modesSpy.count(), 1);
}
#endif // UA_ENABLE_ENCRYPTION

///
/// \brief Anonymous login starts enabled and can be switched off.
///
void TestServer::anonymousLoginIsAllowedByDefault()
{
    QUaServer server;
    QSignalSpy spy(&server, &QUaServer::anonymousLoginAllowedChanged);

    QVERIFY(server.anonymousLoginAllowed());
    server.setAnonymousLoginAllowed(false);

    QVERIFY(!server.anonymousLoginAllowed());
    QCOMPARE(spy.count(), 1);
}

///
/// \brief Adding an existing user replaces its key; removing a missing user is harmless.
///
void TestServer::usersAddUpdateAndRemove()
{
    QUaServer server;

    server.addUser(QStringLiteral("alice"), QStringLiteral("one"));
    server.addUser(QStringLiteral("bob"), QStringLiteral("two"));
    server.addUser(QStringLiteral("alice"), QStringLiteral("three"));

    QCOMPARE(server.userCount(), 2);
    QVERIFY(server.userExists(QStringLiteral("alice")));
    QCOMPARE(server.userKey(QStringLiteral("alice")), QStringLiteral("three"));

    server.removeUser(QStringLiteral("bob"));
    server.removeUser(QStringLiteral("nobody"));

    QCOMPARE(server.userNames(), QStringList({ QStringLiteral("alice") }));
    QVERIFY(!server.userExists(QStringLiteral("bob")));
    QVERIFY(server.userKey(QStringLiteral("bob")).isEmpty());
}

///
/// \brief A user without a name cannot be added.
///
void TestServer::emptyUserNameIsIgnored()
{
    QUaServer server;

    server.addUser(QString(), QStringLiteral("secret"));

    QCOMPARE(server.userCount(), 0);
}

///
/// \brief A Q_ENUM is registered under its scoped name with one entry per key.
///
void TestServer::qtEnumRegistersItsKeys()
{
    QUaServer server;

    server.registerEnum<Valve::Position>();

    const QString enumName = QStringLiteral("Valve::Position");
    QVERIFY(server.isEnumRegistered(enumName));
    const QUaEnumMap map = server.enumMap(enumName);
    QCOMPARE(map.keys(), QList<QUaEnumKey>({ 0, 1, 10 }));
    QCOMPARE(map.value(10).displayName.text(), QStringLiteral("Fault"));
}

///
/// \brief Entries can be added, changed and removed after registration.
///
void TestServer::enumMapCanBeEdited()
{
    QUaServer server;
    const QString enumName = QStringLiteral("Mode");
    server.registerEnum(enumName, { { 0, { QUaLocalizedText(QStringLiteral("Auto")), QUaLocalizedText() } } });

    server.updateEnumEntry(enumName, 1, { QUaLocalizedText(QStringLiteral("Manual")), QUaLocalizedText() });
    server.updateEnumEntry(enumName, 0, { QUaLocalizedText(QStringLiteral("Automatic")), QUaLocalizedText() });
    server.removeEnumEntry(enumName, 5);

    QUaEnumMap map = server.enumMap(enumName);
    QCOMPARE(map.count(), 2);
    QCOMPARE(map.value(0).displayName.text(), QStringLiteral("Automatic"));
    QCOMPARE(map.value(1).displayName.text(), QStringLiteral("Manual"));

    server.removeEnumEntry(enumName, 0);
    map = server.enumMap(enumName);
    QCOMPARE(map.keys(), QList<QUaEnumKey>({ 1 }));
    QVERIFY(server.enumMap(QStringLiteral("Unknown")).isEmpty());
}

///
/// \brief Registering a name a second time does not replace the existing entries.
///
void TestServer::registeringEnumTwiceKeepsFirstMap()
{
    QUaServer server;
    const QString enumName = QStringLiteral("State");
    server.registerEnum(enumName, { { 1, { QUaLocalizedText(QStringLiteral("On")), QUaLocalizedText() } } });

    server.registerEnum(enumName, { { 2, { QUaLocalizedText(QStringLiteral("Off")), QUaLocalizedText() } } });

    QCOMPARE(server.enumMap(enumName).keys(), QList<QUaEnumKey>({ 1 }));
}

///
/// \brief A reference type is registered once; registering it again succeeds without duplicating it.
///
void TestServer::referenceTypeRegistersOnce()
{
    QUaServer server;
    const QUaReferenceType feeds = { QStringLiteral("Feeds"), QStringLiteral("FedBy") };
    const int builtinCount = server.referenceTypes().count();

    QVERIFY(!server.referenceTypeRegistered(feeds));
    QVERIFY(server.registerReferenceType(feeds));
    QVERIFY(server.registerReferenceType(feeds));

    QVERIFY(server.referenceTypeRegistered(feeds));
    QCOMPARE(server.referenceTypes().count(), builtinCount + 1);
}

///
/// \brief isNodeIdUsed() tracks nodes as they are created and deleted.
///
void TestServer::nodeIdUsageReflectsAddressSpace()
{
    QUaServer server;
    const QUaNodeId nodeId(1, QStringLiteral("Tracked"));

    QVERIFY(server.isNodeIdUsed(QUaNodeId(0, quint32(UA_NS0ID_SERVER))));
    QVERIFY(!server.isNodeIdUsed(nodeId));

    QUaBaseDataVariable *variable = server.objectsFolder()->addBaseDataVariable(QStringLiteral("tracked"), nodeId);
    QVERIFY(server.isNodeIdUsed(nodeId));

    delete variable;
    QVERIFY(!server.isNodeIdUsed(nodeId));
}

///
/// \brief The objects folder wraps the standard ns=0;i=85 node.
///
void TestServer::objectsFolderIsNamespaceZeroFolder()
{
    QUaServer server;
    QUaFolderObject *objects = server.objectsFolder();

    QVERIFY(objects);
    QCOMPARE(objects->nodeId(), QUaNodeId(0, quint32(UA_NS0ID_OBJECTSFOLDER)));
    QCOMPARE(server.nodeById(objects->nodeId()), objects);
    QVERIFY(!server.isRunning());
}

QTEST_GUILESS_MAIN(TestServer)

#include "test_server.moc"
