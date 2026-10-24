#include <QBuffer>
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
    void limitsStartWithOpen62541Defaults();
    void setLimitsStoresAndNotifiesOnChange();
    void invalidLimitsAreRejected_data();
    void invalidLimitsAreRejected();
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
    void readsVariableTypeAttributes_data();
    void readsVariableTypeAttributes();
    void readsVariableInstanceAttributes();
    void invalidVariableAttributesLeaveOutputUnchanged_data();
    void invalidVariableAttributesLeaveOutputUnchanged();
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

void TestServer::limitsStartWithOpen62541Defaults()
{
    QUaServer server;

    const QUaServerLimits limits = server.limits();

    QVERIFY(limits.isValid());
    QCOMPARE(limits.maxSessionTimeout, 3600.0 * 1000.0);
    QCOMPARE(limits.minPublishingInterval, 100.0);
    QCOMPARE(limits.maxPublishingInterval, 3600.0 * 1000.0);
    QCOMPARE(limits.minSamplingInterval, 50.0);
    QCOMPARE(limits.maxSamplingInterval, 24.0 * 3600.0 * 1000.0);
    QCOMPARE(limits.minQueueSize, quint32(1));
    QCOMPARE(limits.maxQueueSize, quint32(100));
}

void TestServer::setLimitsStoresAndNotifiesOnChange()
{
    QUaServer server;
    QSignalSpy spy(&server, &QUaServer::limitsChanged);
    QUaServerLimits limits = server.limits();
    limits.maxSubscriptionsPerSession = 3;
    limits.minPublishingInterval = 500.0;

    QVERIFY(server.setLimits(limits));
    QVERIFY(server.setLimits(limits));

    QCOMPARE(server.limits(), limits);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().value<QUaServerLimits>(), limits);
}

void TestServer::invalidLimitsAreRejected_data()
{
    QTest::addColumn<QUaServerLimits>("limits");
    const QUaServerLimits defaults = QUaServer().limits();

    QUaServerLimits limits = defaults;
    limits.minPublishingInterval = limits.maxPublishingInterval + 1.0;
    QTest::newRow("publishing interval range inverted") << limits;
    limits = defaults;
    limits.minPublishingInterval = 1.0;
    QTest::newRow("publishing interval below 5 ms") << limits;
    limits = defaults;
    limits.minSamplingInterval = limits.maxSamplingInterval + 1.0;
    QTest::newRow("sampling interval range inverted") << limits;
    limits = defaults;
    limits.minSamplingInterval = 0.0;
    QTest::newRow("sampling interval below 5 ms") << limits;
    limits = defaults;
    limits.minQueueSize = limits.maxQueueSize + 1;
    QTest::newRow("queue size range inverted") << limits;
}

void TestServer::invalidLimitsAreRejected()
{
    QFETCH(QUaServerLimits, limits);
    QUaServer server;
    const QUaServerLimits before = server.limits();
    QSignalSpy spy(&server, &QUaServer::limitsChanged);

    QVERIFY(!server.setLimits(limits));

    QCOMPARE(server.limits(), before);
    QCOMPARE(spy.count(), 0);
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

///
/// \brief Variable types expose scalar, unrestricted and fixed array constraints without C++ instances.
///
void TestServer::readsVariableTypeAttributes_data()
{
    QTest::addColumn<int>("rank");
    QTest::addColumn<QVector<quint32>>("dimensions");
    QTest::newRow("any") << -2 << QVector<quint32>();
    QTest::newRow("scalar-or-vector") << -3 << QVector<quint32>();
    QTest::newRow("scalar") << -1 << QVector<quint32>();
    QTest::newRow("one-or-more") << 0 << QVector<quint32>();
    QTest::newRow("vector") << 1 << QVector<quint32> { 0 };
    QTest::newRow("fixed-vector") << 1 << QVector<quint32> { 3 };
    QTest::newRow("matrix") << 2 << QVector<quint32> { 2, 3 };
}

///
/// \brief Imported variable type attributes are read without instantiating or modifying the type.
///
void TestServer::readsVariableTypeAttributes()
{
    QFETCH(int, rank);
    QFETCH(QVector<quint32>, dimensions);
    QStringList lengths;
    for (quint32 length : dimensions)
        lengths.append(QString::number(length));
    QByteArray xml = QStringLiteral(
        "<UANodeSet xmlns=\"http://opcfoundation.org/UA/2011/03/UANodeSet.xsd\">"
        "<NamespaceUris><Uri>urn:test:constraints</Uri></NamespaceUris>"
        "<UAVariableType NodeId=\"ns=1;i=5000\" BrowseName=\"1:Constrained\" DataType=\"i=11\" "
        "ValueRank=\"%1\" ArrayDimensions=\"%2\">"
        "<References><Reference ReferenceType=\"i=45\" IsForward=\"false\">i=63</Reference></References>"
        "</UAVariableType></UANodeSet>").arg(rank).arg(lengths.join(QLatin1Char(','))).toUtf8();
    QBuffer buffer(&xml);
    QVERIFY(buffer.open(QIODevice::ReadOnly));
    QUaServer server;
    const QUaNodeSetResult loaded = server.loadNodeSet(&buffer);
    QVERIFY2(loaded.isOk(), qPrintable(loaded.errorString));
    QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(QLatin1Char('\n'))));
    const int ns = server.namespaces().indexOf(QStringLiteral("urn:test:constraints"));
    QVERIFY(ns >= 0);
    const QUaNodeId id(quint16(ns), quint32(5000));
    QVERIFY(server.isNodeIdUsed(id));
    QVERIFY(!server.nodeById(id));
    QUaVariableAttributes attributes;
    QVERIFY(server.variableAttributes(id, attributes));
    QCOMPARE(attributes.dataType, QUaNodeId(0, quint32(UA_NS0ID_DOUBLE)));
    QCOMPARE(attributes.valueRank, rank);
    QCOMPARE(attributes.arrayDimensions, dimensions);
    QVERIFY(!server.nodeById(id));
    QVERIFY(server.variableAttributes(QUaNodeId(0, quint32(UA_NS0ID_BASEDATAVARIABLETYPE)), attributes));
    QCOMPARE(attributes.dataType, QUaNodeId(0, quint32(UA_NS0ID_BASEDATATYPE)));
    QCOMPARE(attributes.valueRank, qint32(UA_VALUERANK_ANY));
    QVERIFY(attributes.arrayDimensions.isEmpty());
}

///
/// \brief Variable instances use the same attribute reader as types.
///
void TestServer::readsVariableInstanceAttributes()
{
    QUaServer server;
    auto *variable = server.objectsFolder()->addBaseDataVariable(QStringLiteral("Scalar"));
    QVERIFY(variable);
    variable->setValue(quint16(42));
    QUaVariableAttributes attributes;
    QVERIFY(server.variableAttributes(variable->nodeId(), attributes));
    QCOMPARE(attributes.dataType, QUaNodeId(0, quint32(UA_NS0ID_UINT16)));
    QCOMPARE(attributes.valueRank, variable->valueRank());
    QCOMPARE(attributes.arrayDimensions, variable->arrayDimensions());
    QCOMPARE(variable->value().toUInt(), 42u);
}

///
/// \brief Missing nodes and nodes of other classes have no variable attributes.
///
void TestServer::invalidVariableAttributesLeaveOutputUnchanged_data()
{
    QTest::addColumn<QString>("nodeId");
    QTest::newRow("missing") << "ns=1;s=Missing";
    QTest::newRow("object") << "i=85";
    QTest::newRow("object-type") << "i=58";
    QTest::newRow("data-type") << "i=11";
}

///
/// \brief A failed read preserves the caller's previous constraints.
///
void TestServer::invalidVariableAttributesLeaveOutputUnchanged()
{
    QFETCH(QString, nodeId);
    QUaServer server;
    QUaVariableAttributes attributes;
    attributes.dataType = QUaNodeId(0, quint32(UA_NS0ID_DOUBLE));
    attributes.valueRank = 1;
    attributes.arrayDimensions = { 5 };
    QVERIFY(!server.variableAttributes(QUaNodeId(nodeId), attributes));
    QCOMPARE(attributes.dataType, QUaNodeId(0, quint32(UA_NS0ID_DOUBLE)));
    QCOMPARE(attributes.valueRank, 1);
    QCOMPARE(attributes.arrayDimensions, QVector<quint32> { 5 });
}

QTEST_GUILESS_MAIN(TestServer)

#include "test_server.moc"
