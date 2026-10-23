#include <QBuffer>
#include <QTest>

#include <algorithm>

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
    void namesAndRelatesLoadedTypes();
    void readsReferencesAndTypeAttributes();
    void addsInstancesOfTypesWithoutCppClass();
    void bindsLoadedInstancesOfStandardTypesWithoutCppClass();
    void refusesInstancesOfOtherNodes();
    void childNodeIdCallbackAppliesBeforeStart();
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

///
/// \brief Types have no C++ instance, so their names and supertypes are read from the server.
///
void TestNodeSetIntegration::namesAndRelatesLoadedTypes()
{
    const QUaNodeId mode    = this->devicesNodeId(5001);
    const QUaNodeId reading = this->devicesNodeId(5100);
    const QUaNodeId enumeration(0, quint32(UA_NS0ID_ENUMERATION));
    const QUaNodeId structure(0, quint32(UA_NS0ID_STRUCTURE));

    QCOMPARE(m_server->browseName(mode), QUaQualifiedName(this->devicesNamespace(), QStringLiteral("Mode")));
    QCOMPARE(m_server->browseName(QUaNodeId(0, quint32(UA_NS0ID_DOUBLE))), QUaQualifiedName(0, QStringLiteral("Double")));
    QVERIFY(m_server->browseName(this->devicesNodeId(9999)).name().isEmpty());

    QVERIFY(m_server->isSubtypeOf(mode, enumeration));
    QVERIFY(m_server->isSubtypeOf(mode, QUaNodeId(0, quint32(UA_NS0ID_BASEDATATYPE))));
    QVERIFY(m_server->isSubtypeOf(mode, mode));
    QVERIFY(!m_server->isSubtypeOf(mode, structure));
    QVERIFY(m_server->isSubtypeOf(reading, structure));
    QVERIFY(!m_server->isSubtypeOf(reading, enumeration));
    QVERIFY(m_server->isSubtypeOf(QUaNodeId(0, quint32(UA_NS0ID_SERVERSTATE)), enumeration));
    QVERIFY(!m_server->isSubtypeOf(QUaNodeId(0, quint32(UA_NS0ID_DOUBLE)), enumeration));
    QVERIFY(!m_server->isSubtypeOf(this->devicesNodeId(9999), enumeration));
}

///
/// \brief The references and attributes of types give applications the type hierarchy of a NodeSet.
///
void TestNodeSetIntegration::readsReferencesAndTypeAttributes()
{
    const QUaNodeId deviceType = this->devicesNodeId(1001);
    const QList<QUaNodeReference> references = m_server->references(deviceType);
    const auto has = [&references](quint32 referenceType, const QUaNodeId &target, bool isForward) {
        return std::any_of(references.cbegin(), references.cend(), [&](const QUaNodeReference &reference) {
            return reference.referenceTypeId == QUaNodeId(0, referenceType) && reference.targetNodeId == target
                && reference.isForward == isForward;
        });
    };
    QVERIFY(has(UA_NS0ID_HASSUBTYPE, QUaNodeId(0, quint32(UA_NS0ID_BASEOBJECTTYPE)), false));
    QVERIFY(has(UA_NS0ID_HASCOMPONENT, this->devicesNodeId(1002), true));
    QVERIFY(has(UA_NS0ID_HASPROPERTY, this->devicesNodeId(1003), true));
    QVERIFY(has(UA_NS0ID_HASTYPEDEFINITION, this->devicesNodeId(2001), false));
    QVERIFY(m_server->references(this->devicesNodeId(9999)).isEmpty());

    QCOMPARE(m_server->description(this->devicesNodeId(2001)).text(), QStringLiteral("First device"));
    QVERIFY(m_server->description(this->devicesNodeId(9999)).text().isEmpty());

    QVERIFY(m_server->isAbstract(QUaNodeId(0, quint32(UA_NS0ID_BASEVARIABLETYPE))));
    QVERIFY(m_server->isAbstract(QUaNodeId(0, quint32(UA_NS0ID_BASEDATATYPE))));
    QVERIFY(!m_server->isAbstract(QUaNodeId(0, quint32(UA_NS0ID_BASEOBJECTTYPE))));
    QVERIFY(!m_server->isAbstract(deviceType));
    QVERIFY(!m_server->isAbstract(this->devicesNodeId(2001)));
    QVERIFY(!m_server->isAbstract(this->devicesNodeId(9999)));
}

///
/// \brief Instances of types without a C++ class get the class of their closest registered supertype and the
///        mandatory children of the type.
///
void TestNodeSetIntegration::addsInstancesOfTypesWithoutCppClass()
{
    const quint16 ns = this->devicesNamespace();
    QUaNode *node = m_server->objectsFolder()->addInstance(QUaQualifiedName(ns, QStringLiteral("Device9")),
                                                           this->devicesNodeId(1001), this->devicesNodeId(9001));
    auto *device = qobject_cast<QUaBaseObject *>(node);
    QVERIFY(device);
    QCOMPARE(device->nodeId(), this->devicesNodeId(9001));
    QCOMPARE(device->parent(), m_server->objectsFolder());
    QCOMPARE(device->typeDefinitionNodeId(), this->devicesNodeId(1001));
    auto *temperature = device->browseChild<QUaBaseDataVariable>(QUaQualifiedName(ns, QStringLiteral("Temperature")));
    QVERIFY(temperature);
    QCOMPARE(temperature->parent(), device);
    QVERIFY2(!device->browseChild(QUaQualifiedName(ns, QStringLiteral("Serial"))), "optional children are left out");

    auto *second = qobject_cast<QUaBaseObject *>(
        device->addInstance(QUaQualifiedName(ns, QStringLiteral("Sub")), this->devicesNodeId(1001)));
    QVERIFY2(second, "a type bound by the first instance is instantiated again");
    QVERIFY(!second->nodeId().isNull());
    QCOMPARE(second->parent(), device);

    auto *property = qobject_cast<QUaProperty *>(
        device->addInstance(QUaQualifiedName(ns, QStringLiteral("Note")), QUaNodeId(0, quint32(UA_NS0ID_PROPERTYTYPE))));
    QVERIFY2(property, "standard types get their registered class");
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
    auto *analog = qobject_cast<QUaBaseDataVariable *>(
        device->addInstance(QUaQualifiedName(ns, QStringLiteral("Level")), QUaNodeId(0, quint32(UA_NS0ID_ANALOGITEMTYPE))));
    QVERIFY(analog);
    QCOMPARE(analog->typeDefinitionNodeId(), QUaNodeId(0, quint32(UA_NS0ID_ANALOGITEMTYPE)));
    QVERIFY(analog->browseChild<QUaProperty>(QUaQualifiedName(0, QStringLiteral("EURange"))));
#endif
}

///
/// \brief A NodeSet instance of a standard type without a C++ class of its own, such as AnalogItemType, gets the
///        class of the closest registered supertype; encoding objects of data types stay without one.
///
void TestNodeSetIntegration::bindsLoadedInstancesOfStandardTypesWithoutCppClass()
{
#ifdef UA_GENERATED_NAMESPACE_ZERO_FULL
    const QUaNodeSetResult result = loadNodeSet(*m_server, nodeSet(
        "<UAVariable NodeId=\"ns=1;i=9101\" BrowseName=\"1:Pressure\" DataType=\"i=11\">"
        "<DisplayName>Pressure</DisplayName>"
        "<References>"
        "<Reference ReferenceType=\"i=40\">i=2368</Reference>"
        "<Reference ReferenceType=\"i=47\" IsForward=\"false\">ns=1;i=2001</Reference>"
        "</References>"
        "<Value><uax:Double>2.5</uax:Double></Value>"
        "</UAVariable>"
        "<UAVariable NodeId=\"ns=1;i=9102\" BrowseName=\"EURange\" DataType=\"i=884\">"
        "<DisplayName>EURange</DisplayName>"
        "<References>"
        "<Reference ReferenceType=\"i=40\">i=68</Reference>"
        "<Reference ReferenceType=\"i=46\" IsForward=\"false\">ns=1;i=9101</Reference>"
        "</References>"
        "</UAVariable>"));
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));

    auto *pressure = m_server->nodeById<QUaBaseDataVariable>(this->devicesNodeId(9101));
    QVERIFY(pressure);
    QCOMPARE(pressure->parent(), m_server->nodeById(this->devicesNodeId(2001)));
    QCOMPARE(pressure->typeDefinitionNodeId(), QUaNodeId(0, quint32(UA_NS0ID_ANALOGITEMTYPE)));
    QCOMPARE(pressure->value().toDouble(), 2.5);
    auto *range = pressure->browseChild<QUaProperty>(QUaQualifiedName(0, QStringLiteral("EURange")));
    QVERIFY(range);
    QCOMPARE(range->nodeId(), this->devicesNodeId(9102));
#else
    QSKIP("AnalogItemType needs the full namespace zero");
#endif
    QVERIFY2(!m_server->nodeById(this->devicesNodeId(5101)), "encoding objects get no C++ instance");
}

///
/// \brief Abstract types, event types, data types and instances cannot be instantiated.
///
void TestNodeSetIntegration::refusesInstancesOfOtherNodes()
{
    QUaFolderObject *objects = m_server->objectsFolder();
    const auto add = [objects](const QString &name, const QUaNodeId &typeId) {
        return objects->addInstance(QUaQualifiedName(0, name), typeId);
    };
    QVERIFY(!add(QStringLiteral("Abstract"), QUaNodeId(0, quint32(UA_NS0ID_BASEVARIABLETYPE))));
    QVERIFY(!add(QStringLiteral("Event"), QUaNodeId(0, quint32(UA_NS0ID_BASEEVENTTYPE))));
    QVERIFY(!add(QStringLiteral("DataType"), QUaNodeId(0, quint32(UA_NS0ID_DOUBLE))));
    QVERIFY(!add(QStringLiteral("Instance"), this->devicesNodeId(2001)));
    QVERIFY(!add(QStringLiteral("Unknown"), this->devicesNodeId(9999)));
    QVERIFY(!objects->hasChild(QUaQualifiedName(0, QStringLiteral("Abstract"))));
}

///
/// \brief Applications build their address space before starting the server, so the child node ids they choose
///        must apply then too.
///
void TestNodeSetIntegration::childNodeIdCallbackAppliesBeforeStart()
{
    QVERIFY(!m_server->isRunning());
    m_server->setChildNodeIdCallback([](const QUaNodeId &parentId, const QUaQualifiedName &browseName) {
        return QUaNodeId(1, parentId.toXmlString() + QLatin1Char('.') + browseName.name());
    });
    const quint16 ns = this->devicesNamespace();
    QUaNode *device = m_server->objectsFolder()->addInstance(QUaQualifiedName(ns, QStringLiteral("Device9")),
                                                             this->devicesNodeId(1001), QUaNodeId(1, QStringLiteral("Device9")));
    QVERIFY(device);
    auto *temperature = device->browseChild<QUaBaseDataVariable>(QUaQualifiedName(ns, QStringLiteral("Temperature")));
    QVERIFY(temperature);
    QCOMPARE(temperature->nodeId(), QUaNodeId(1, QStringLiteral("ns=1;s=Device9.Temperature")));
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
