#include <QBuffer>
#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

namespace {

const QString kDevicesUri = QStringLiteral("http://quaserver.test/devices/");
const QString kOtherUri   = QStringLiteral("http://quaserver.test/other/");

QString nodeSetPath(const QString &fileName)
{
    return QStringLiteral(QUASERVER_TEST_NODESET_DIR "/") + fileName;
}

///
/// \brief Wraps \a nodes in a NodeSet with one namespace, as the devices NodeSet uses.
///
QByteArray nodeSet(const QByteArray &nodes)
{
    return QByteArrayLiteral(
               "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
               "<UANodeSet xmlns=\"http://opcfoundation.org/UA/2011/03/UANodeSet.xsd\" "
               "xmlns:uax=\"http://opcfoundation.org/UA/2008/02/Types.xsd\">"
               "<NamespaceUris><Uri>http://quaserver.test/devices/</Uri></NamespaceUris>")
           + nodes + QByteArrayLiteral("</UANodeSet>");
}

QUaNodeSetResult loadNodeSet(QUaServer &server, const QByteArray &xml)
{
    QBuffer buffer;
    buffer.setData(xml);
    buffer.open(QIODevice::ReadOnly);
    return server.loadNodeSet(&buffer);
}

} // namespace

class TestNodeSetIntegration : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void loadsNodesInDependencyOrder();
    void mapsNamespacesToServerIndexes();
    void createsCppInstancesOfLoadedNodes();
    void instancesOfLoadedTypesGetMandatoryChildren();
    void decodesValues();
    void clientsReadAndWriteLoadedVariables();
    void loadedMethodsAreNotImplemented();
    void nonHierarchicalReferencesDoNotMakeParents();
    void loadingTwiceSkipsExistingNodes();
    void unresolvedContentIsReportedAndSkipped();
    void invalidNodeSetAddsNothing_data();
    void invalidNodeSetAddsNothing();
    void missingFileIsReported();
    void loadsCompanionSpecification();

private:
    quint16 devicesNamespace() const;
    QUaNodeId devicesNodeId(quint32 numericId) const;

    QUaServer *m_server = nullptr;
    QUaNodeSetResult m_result;
};

///
/// \brief Every test gets a fresh server with the devices NodeSet loaded.
///
void TestNodeSetIntegration::init()
{
    m_server = new QUaServer;
    m_result = m_server->loadNodeSet(nodeSetPath(QStringLiteral("devices.NodeSet2.xml")));
    QVERIFY2(m_result.isOk(), qPrintable(m_result.errorString));
    QVERIFY2(m_result.warnings.isEmpty(), qPrintable(m_result.warnings.join(QLatin1Char('\n'))));
}

void TestNodeSetIntegration::cleanup()
{
    delete m_server;
    m_server = nullptr;
}

quint16 TestNodeSetIntegration::devicesNamespace() const
{
    return static_cast<quint16>(m_server->namespaces().indexOf(kDevicesUri));
}

QUaNodeId TestNodeSetIntegration::devicesNodeId(quint32 numericId) const
{
    return QUaNodeId(this->devicesNamespace(), numericId);
}

///
/// \brief Nodes referring to types and parents declared later in the file are still added after them.
///
void TestNodeSetIntegration::loadsNodesInDependencyOrder()
{
    QCOMPARE(m_result.addedNodes.count(), 18);
    const int typeIndex = m_result.addedNodes.indexOf(this->devicesNodeId(1001));
    const int instanceIndex = m_result.addedNodes.indexOf(this->devicesNodeId(2001));
    const int childIndex = m_result.addedNodes.indexOf(this->devicesNodeId(3001));
    QVERIFY(typeIndex >= 0);
    QVERIFY(typeIndex < instanceIndex);
    QVERIFY(instanceIndex < childIndex);
}

void TestNodeSetIntegration::mapsNamespacesToServerIndexes()
{
    const QStringList namespaces = m_server->namespaces();

    QCOMPARE(namespaces.first(), QStringLiteral("http://opcfoundation.org/UA/"));
    QCOMPARE(namespaces.indexOf(kDevicesUri), 2);
    QCOMPARE(namespaces.indexOf(kOtherUri), 3);
    QVERIFY(m_server->nodeById(QUaNodeId(3, quint32(1))));
}

void TestNodeSetIntegration::createsCppInstancesOfLoadedNodes()
{
    auto *device = m_server->nodeById<QUaBaseObject>(this->devicesNodeId(2001));
    QVERIFY(device);
    QCOMPARE(device->parent(), m_server->objectsFolder());
    QCOMPARE(device->typeDefinitionNodeId(), this->devicesNodeId(1001));
    QCOMPARE(device->displayName(), QUaLocalizedText(QStringLiteral("en"), QStringLiteral("Device 1")));
    QCOMPARE(device->description(), QUaLocalizedText(QString(), QStringLiteral("First device")));

    const quint16 ns = this->devicesNamespace();
    auto *temperature = device->browseChild<QUaBaseDataVariable>(QUaQualifiedName(ns, QStringLiteral("Temperature")));
    QVERIFY(temperature);
    QCOMPARE(temperature->nodeId(), this->devicesNodeId(2002));
    auto *text = device->browseChild<QUaBaseDataVariable>(QUaQualifiedName(ns, QStringLiteral("Text")));
    QVERIFY2(text, "children declared only by a forward reference of the parent are bound too");
    QVERIFY(!device->browseChild(QUaQualifiedName(ns, QStringLiteral("Serial"))));

    auto *folder = m_server->nodeById<QUaFolderObject>(this->devicesNodeId(7001));
    QVERIFY(folder);
    QVERIFY(folder->browseChild<QUaBaseObject>(QUaQualifiedName(3, QStringLiteral("Other"))));
}

///
/// \brief open62541 instantiates the mandatory children a NodeSet instance leaves out, with C++ instances as well.
///
void TestNodeSetIntegration::instancesOfLoadedTypesGetMandatoryChildren()
{
    auto *device = m_server->nodeById<QUaBaseObject>(this->devicesNodeId(2003));
    QVERIFY(device);

    auto *temperature = device->browseChild<QUaBaseDataVariable>(
        QUaQualifiedName(this->devicesNamespace(), QStringLiteral("Temperature")));
    QVERIFY(temperature);
    QCOMPARE(temperature->parent(), device);
}

void TestNodeSetIntegration::decodesValues()
{
    auto *temperature = m_server->nodeById<QUaBaseDataVariable>(this->devicesNodeId(2002));
    auto *text = m_server->nodeById<QUaBaseDataVariable>(this->devicesNodeId(3001));
    auto *array = m_server->nodeById<QUaBaseDataVariable>(QUaNodeId(this->devicesNamespace(), QStringLiteral("Array")));
    QVERIFY(temperature && text && array);

    QCOMPARE(temperature->value(), QVariant(21.5));
    QCOMPARE(text->value(), QVariant(QStringLiteral("hello & <world>")));
    QCOMPARE(array->value().value<QVariantList>(), (QVariantList{ 1, 2, 3 }));
    QCOMPARE(array->valueRank(), 1);
    QCOMPARE(array->arrayDimensions(), (QVector<quint32>{ 3 }));

    auto *grid = m_server->nodeById<QUaBaseDataVariable>(QUaNodeId(this->devicesNamespace(), QStringLiteral("Grid")));
    QVERIFY(grid);
    QCOMPARE(grid->valueRank(), 2);
    QCOMPARE(grid->arrayDimensions(), (QVector<quint32>{ 2, 2 }));
    const QVariantList rows = grid->value().value<QVariantList>();
    QCOMPARE(rows.count(), 2);
    QCOMPARE(rows.at(1).value<QList<int>>(), QList<int>({ 3, 4 }));

    const QUaNodeId readingTypeId = this->devicesNodeId(5100);
    QCOMPARE(m_server->structureFields(readingTypeId).count(), 3);
    auto *reading = m_server->nodeById<QUaBaseDataVariable>(this->devicesNodeId(5110));
    QVERIFY(reading);
    QCOMPARE(reading->dataType(), QMetaType_Structure);
    const QUaStructure value = reading->value().value<QUaStructure>();
    QCOMPARE(value.typeId(), readingTypeId);
    QCOMPARE(value.field(QStringLiteral("Value")).toDouble(), 12.5);
    QCOMPARE(value.field(QStringLiteral("Mode")).toInt(), 1);
    QCOMPARE(value.field(QStringLiteral("Tags")).value<QList<QString>>(), QList<QString>({ QStringLiteral("a"), QStringLiteral("b") }));
}

void TestNodeSetIntegration::clientsReadAndWriteLoadedVariables()
{
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QVariant value;

    QCOMPARE(client.readValue(this->devicesNodeId(2002), value), UA_STATUSCODE_GOOD);
    QCOMPARE(value, QVariant(21.5));
    QCOMPARE(client.writeValue(this->devicesNodeId(2002), 30.0), UA_STATUSCODE_GOOD);

    auto *temperature = m_server->nodeById<QUaBaseDataVariable>(this->devicesNodeId(2002));
    QTRY_COMPARE(temperature->value(), QVariant(30.0));
}

void TestNodeSetIntegration::loadedMethodsAreNotImplemented()
{
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    QCOMPARE(client.call(this->devicesNodeId(2001), this->devicesNodeId(6001), {}, outputs),
             UA_STATUSCODE_BADNOTIMPLEMENTED);
}

///
/// \brief Device1 references Device2 with a custom non-hierarchical type, which must not make it Device2's parent.
///
void TestNodeSetIntegration::nonHierarchicalReferencesDoNotMakeParents()
{
    auto *device1 = m_server->nodeById<QUaBaseObject>(this->devicesNodeId(2001));
    auto *device2 = m_server->nodeById<QUaBaseObject>(this->devicesNodeId(2003));
    QVERIFY(device1 && device2);

    QCOMPARE(device2->parent(), m_server->objectsFolder());
    const QUaReferenceType feeds{ QStringLiteral("Feeds"), QStringLiteral("FedBy") };
    QCOMPARE(device1->findReferences(feeds).count(), 1);
}

void TestNodeSetIntegration::loadingTwiceSkipsExistingNodes()
{
    const int namespaceCount = m_server->namespaces().count();

    const QUaNodeSetResult result = m_server->loadNodeSet(nodeSetPath(QStringLiteral("devices.NodeSet2.xml")));

    QVERIFY(result.isOk());
    QVERIFY(result.addedNodes.isEmpty());
    QCOMPARE(result.warnings.count(), m_result.addedNodes.count());
    QCOMPARE(m_server->namespaces().count(), namespaceCount);
}

void TestNodeSetIntegration::unresolvedContentIsReportedAndSkipped()
{
    const QUaNodeSetResult result = loadNodeSet(*m_server, nodeSet(
        "<UAVariable NodeId=\"ns=1;i=9001\" BrowseName=\"1:Broken\" DataType=\"i=6\">"
        "<DisplayName>Broken</DisplayName>"
        "<References>"
        "<Reference ReferenceType=\"i=35\" IsForward=\"false\">i=85</Reference>"
        "<Reference ReferenceType=\"Unknown\">ns=1;i=9002</Reference>"
        "<Reference ReferenceType=\"i=47\">ns=1;i=9999</Reference>"
        "</References>"
        "<Value><uax:Unknown>1</uax:Unknown></Value>"
        "</UAVariable>"
        "<UAObject NodeId=\"ns=7;i=1\" BrowseName=\"1:BadNamespace\"/>"
        "<UAObject NodeId=\"ns=1;i=9003\" BrowseName=\"1:Orphan\">"
        "<References><Reference ReferenceType=\"i=47\" IsForward=\"false\">ns=1;i=9998</Reference></References>"
        "</UAObject>"));

    QVERIFY(result.isOk());
    QCOMPARE(result.addedNodes, (QList<QUaNodeId>{ this->devicesNodeId(9001) }));
    QCOMPARE(result.warnings.count(), 5);
    QVERIFY(result.warnings.at(0).contains(QStringLiteral("Unknown")));
    QVERIFY(result.warnings.at(1).contains(QStringLiteral("ns=7;i=1")));
    QVERIFY(m_server->nodeById<QUaBaseDataVariable>(this->devicesNodeId(9001)));
}

void TestNodeSetIntegration::invalidNodeSetAddsNothing_data()
{
    QTest::addColumn<QByteArray>("xml");

    QTest::newRow("truncated") << nodeSet("<UAObject NodeId=\"ns=1;i=9001\" BrowseName=\"1:Lost\">").chopped(12);
    QTest::newRow("not a NodeSet") << QByteArrayLiteral("<Other><UAObject NodeId=\"ns=1;i=9001\"/></Other>");
    QTest::newRow("empty") << QByteArray();
}

void TestNodeSetIntegration::invalidNodeSetAddsNothing()
{
    QFETCH(QByteArray, xml);
    const QStringList namespaces = m_server->namespaces();

    const QUaNodeSetResult result = loadNodeSet(*m_server, xml);

    QVERIFY(!result.isOk());
    QVERIFY(!result.errorString.isEmpty());
    QVERIFY(result.addedNodes.isEmpty());
    QCOMPARE(m_server->namespaces(), namespaces);
    QVERIFY(!m_server->nodeById(this->devicesNodeId(9001)));
}

void TestNodeSetIntegration::missingFileIsReported()
{
    const QUaNodeSetResult result = m_server->loadNodeSet(nodeSetPath(QStringLiteral("missing.NodeSet2.xml")));

    QVERIFY(!result.isOk());
    QVERIFY(result.errorString.contains(QStringLiteral("missing.NodeSet2.xml")));
}

///
/// \brief The OPC UA DI specification exercises what real NodeSets use: methods, enumerations, structure values.
///
void TestNodeSetIntegration::loadsCompanionSpecification()
{
#ifndef UA_GENERATED_NAMESPACE_ZERO_FULL
    QSKIP("DI builds on types of the full namespace zero, such as FileType and the alarm types");
#endif
    const QUaNodeSetResult result = m_server->loadNodeSet(nodeSetPath(QStringLiteral("Opc.Ua.Di.NodeSet2.xml")));

    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(result.addedNodes.count(), 447);
    const quint16 di = static_cast<quint16>(m_server->namespaces().indexOf(QStringLiteral("http://opcfoundation.org/UA/DI/")));
    auto *deviceSet = m_server->nodeById<QUaBaseObject>(QUaNodeId(di, quint32(5001)));
    QVERIFY(deviceSet);
    QCOMPARE(deviceSet->parent(), m_server->objectsFolder());

    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QVariant healthStrings;
    QCOMPARE(client.readValue(QUaNodeId(di, quint32(6450)), healthStrings), UA_STATUSCODE_GOOD);
    const QVariantList strings = healthStrings.value<QVariantList>();
    QCOMPARE(strings.count(), 5);
    QCOMPARE(strings.first().value<QUaLocalizedText>().text(), QStringLiteral("NORMAL"));
}

QTEST_GUILESS_MAIN(TestNodeSetIntegration)

#include "test_nodeset_integration.moc"
