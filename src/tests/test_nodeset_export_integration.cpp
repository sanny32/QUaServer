#include <QBuffer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <QUaServer>

namespace {

const QString kDevicesUri = QStringLiteral("http://quaserver.test/devices/");
const QString kOtherUri   = QStringLiteral("http://quaserver.test/other/");

QString nodeSetPath(const QString &fileName)
{
    return QStringLiteral(QUASERVER_TEST_NODESET_DIR "/") + fileName;
}

///
/// \brief Exports \a namespaceUris of \a server into memory.
///
QByteArray exportToMemory(const QUaServer &server, const QStringList &namespaceUris, QUaNodeSetExportResult &result)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    result = server.exportNodeSet(&buffer, namespaceUris);
    return buffer.data();
}

///
/// \brief Loads NodeSet2 XML held in memory into \a server.
///
QUaNodeSetResult loadFromMemory(QUaServer &server, const QByteArray &xml)
{
    QBuffer buffer;
    buffer.setData(xml);
    buffer.open(QIODevice::ReadOnly);
    return server.loadNodeSet(&buffer);
}

///
/// \brief Returns the warnings that are not about the matrix value open62541 cannot encode as XML.
///
QStringList unexpectedWarnings(const QStringList &warnings)
{
    QStringList unexpected;
    for (const QString &warning : warnings)
    {
        if (!warning.contains(QStringLiteral("s=Grid")))
        {
            unexpected << warning;
        }
    }
    return unexpected;
}

} // namespace

class TestNodeSetExportIntegration : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void exportListsTheNodesOfItsNamespaces();
    void exportedNodeSetLoadsBack();
    void exportKeepsValuesAndDefinitions();
    void exportKeepsReferences();
    void exportKeepsNodesCreatedInCode();
    void exportOfAllNamespacesIncludesApplicationNodes();
    void unknownNamespaceIsRejected();
    void namespaceZeroIsRejected();
    void unwritableDeviceIsRejected();
    void exportWritesAndReportsFiles();
    void companionSpecificationRoundTrips();

private:
    quint16 namespaceOf(const QUaServer &server, const QString &uri) const;
    void loadExportedDevices();

    QUaServer *m_source = nullptr;
    QUaServer *m_target = nullptr;
    QUaNodeSetExportResult m_export;
    QUaNodeSetResult m_load;
};

void TestNodeSetExportIntegration::init()
{
    m_source = new QUaServer;
    m_target = new QUaServer;
    const QUaNodeSetResult loaded = m_source->loadNodeSet(nodeSetPath(QStringLiteral("devices.NodeSet2.xml")));
    QVERIFY2(loaded.isOk(), qPrintable(loaded.errorString));
}

void TestNodeSetExportIntegration::cleanup()
{
    delete m_target;
    m_target = nullptr;
    delete m_source;
    m_source = nullptr;
}

quint16 TestNodeSetExportIntegration::namespaceOf(const QUaServer &server, const QString &uri) const
{
    return static_cast<quint16>(server.namespaces().indexOf(uri));
}

///
/// \brief Exports the devices namespaces of the source server and loads them into the target server.
///
void TestNodeSetExportIntegration::loadExportedDevices()
{
    const QByteArray xml = exportToMemory(*m_source, { kDevicesUri, kOtherUri }, m_export);
    QVERIFY2(m_export.isOk(), qPrintable(m_export.errorString));
    m_load = loadFromMemory(*m_target, xml);
    QVERIFY2(m_load.isOk(), qPrintable(m_load.errorString));
}

///
/// \brief Only nodes of the requested namespaces are exported, among them nodes reachable by non-hierarchical
///        references such as the encodings of a structure.
///
void TestNodeSetExportIntegration::exportListsTheNodesOfItsNamespaces()
{
    QUaNodeSetExportResult result;
    exportToMemory(*m_source, { kDevicesUri }, result);
    QVERIFY2(result.isOk(), qPrintable(result.errorString));

    const quint16 devices = this->namespaceOf(*m_source, kDevicesUri);
    QVERIFY(result.exportedNodes.count() >= 17);
    for (const QUaNodeId &nodeId : result.exportedNodes)
    {
        QCOMPARE(nodeId.namespaceIndex(), devices);
    }
    QVERIFY(result.exportedNodes.contains(QUaNodeId(devices, quint32(5101))));
    QVERIFY(result.exportedNodes.contains(QUaNodeId(devices, quint32(3001))));
    QVERIFY(!result.exportedNodes.contains(QUaNodeId(this->namespaceOf(*m_source, kOtherUri), quint32(1))));
}

///
/// \brief Every exported node is added again by loadNodeSet(), with the same identifiers and parents.
///
void TestNodeSetExportIntegration::exportedNodeSetLoadsBack()
{
    this->loadExportedDevices();
    QVERIFY2(unexpectedWarnings(m_export.warnings).isEmpty(), qPrintable(m_export.warnings.join(QLatin1Char('\n'))));
    QVERIFY2(unexpectedWarnings(m_load.warnings).isEmpty(), qPrintable(m_load.warnings.join(QLatin1Char('\n'))));
    QCOMPARE(this->namespaceOf(*m_target, kDevicesUri), this->namespaceOf(*m_source, kDevicesUri));
    for (const QUaNodeId &nodeId : std::as_const(m_export.exportedNodes))
    {
        QVERIFY2(m_target->isNodeIdUsed(nodeId), qPrintable(nodeId.toXmlString()));
    }

    const quint16 ns = this->namespaceOf(*m_target, kDevicesUri);
    auto *device = m_target->nodeById<QUaBaseObject>(QUaNodeId(ns, quint32(2001)));
    QVERIFY(device);
    QCOMPARE(device->parent(), m_target->objectsFolder());
    QCOMPARE(device->typeDefinitionNodeId(), QUaNodeId(ns, quint32(1001)));
    QCOMPARE(device->displayName(), QUaLocalizedText(QStringLiteral("en"), QStringLiteral("Device 1")));
    QCOMPARE(device->description(), QUaLocalizedText(QString(), QStringLiteral("First device")));
    QVERIFY(device->browseChild<QUaBaseDataVariable>(QUaQualifiedName(ns, QStringLiteral("Text"))));

    auto *folder = m_target->nodeById<QUaFolderObject>(QUaNodeId(ns, quint32(7001)));
    QVERIFY(folder);
    QVERIFY(folder->browseChild<QUaBaseObject>(QUaQualifiedName(this->namespaceOf(*m_target, kOtherUri),
                                                                QStringLiteral("Other"))));
}

void TestNodeSetExportIntegration::exportKeepsValuesAndDefinitions()
{
    this->loadExportedDevices();
    const quint16 ns = this->namespaceOf(*m_target, kDevicesUri);

    auto *temperature = m_target->nodeById<QUaBaseDataVariable>(QUaNodeId(ns, quint32(2002)));
    auto *text = m_target->nodeById<QUaBaseDataVariable>(QUaNodeId(ns, quint32(3001)));
    auto *array = m_target->nodeById<QUaBaseDataVariable>(QUaNodeId(ns, QStringLiteral("Array")));
    QVERIFY(temperature && text && array);
    QCOMPARE(temperature->value(), QVariant(21.5));
    QCOMPARE(text->value(), QVariant(QStringLiteral("hello & <world>")));
    QCOMPARE(array->value().value<QVariantList>(), (QVariantList{ 1, 2, 3 }));
    QCOMPARE(array->valueRank(), 1);
    QCOMPARE(array->arrayDimensions(), (QVector<quint32>{ 3 }));

    auto *grid = m_target->nodeById<QUaBaseDataVariable>(QUaNodeId(ns, QStringLiteral("Grid")));
    QVERIFY(grid);
    QCOMPARE(grid->valueRank(), 2);
    QCOMPARE(grid->arrayDimensions(), (QVector<quint32>{ 2, 2 }));

    const QUaNodeId readingTypeId(ns, quint32(5100));
    QCOMPARE(m_target->structureFields(readingTypeId).count(), 3);
    auto *reading = m_target->nodeById<QUaBaseDataVariable>(QUaNodeId(ns, quint32(5110)));
    QVERIFY(reading);
    const QUaStructure value = reading->value().value<QUaStructure>();
    QCOMPARE(value.typeId(), readingTypeId);
    QCOMPARE(value.field(QStringLiteral("Value")).toDouble(), 12.5);
    QCOMPARE(value.field(QStringLiteral("Mode")).toInt(), 1);
    QCOMPARE(value.field(QStringLiteral("Tags")).value<QList<QString>>(),
             QList<QString>({ QStringLiteral("a"), QStringLiteral("b") }));
}

void TestNodeSetExportIntegration::exportKeepsReferences()
{
    this->loadExportedDevices();
    const quint16 ns = this->namespaceOf(*m_target, kDevicesUri);
    auto *device1 = m_target->nodeById<QUaBaseObject>(QUaNodeId(ns, quint32(2001)));
    auto *device2 = m_target->nodeById<QUaBaseObject>(QUaNodeId(ns, quint32(2003)));
    QVERIFY(device1 && device2);
    QCOMPARE(device2->parent(), m_target->objectsFolder());
    const QUaReferenceType feeds{ QStringLiteral("Feeds"), QStringLiteral("FedBy") };
    QCOMPARE(device1->findReferences(feeds).count(), 1);
    QCOMPARE(device1->findReferences(feeds).first(), device2);
}

void TestNodeSetExportIntegration::exportKeepsNodesCreatedInCode()
{
    QUaFolderObject *line = m_source->objectsFolder()->addFolderObject(
        QUaQualifiedName(1, QStringLiteral("Line")), QUaNodeId(1, QStringLiteral("Line")));
    QUaBaseDataVariable *speed = line->addBaseDataVariable(
        QUaQualifiedName(1, QStringLiteral("Speed")), QUaNodeId(1, QStringLiteral("Line.Speed")));
    speed->setValue(12.5);
    speed->setWriteAccess(true);

    QUaNodeSetExportResult result;
    const QByteArray xml = exportToMemory(*m_source, { m_source->namespaces().at(1) }, result);
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY(result.exportedNodes.contains(QUaNodeId(1, QStringLiteral("Line.Speed"))));

    const QUaNodeSetResult loaded = loadFromMemory(*m_target, xml);
    QVERIFY2(loaded.isOk(), qPrintable(loaded.errorString));
    QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(QLatin1Char('\n'))));
    auto *copy = m_target->nodeById<QUaBaseDataVariable>(QUaNodeId(1, QStringLiteral("Line.Speed")));
    QVERIFY(copy);
    QCOMPARE(copy->value().toDouble(), 12.5);
    QVERIFY(copy->writeAccess());
    QCOMPARE(copy->parent(), m_target->nodeById(QUaNodeId(1, QStringLiteral("Line"))));
    QCOMPARE(copy->parent()->parent(), m_target->objectsFolder());
}

void TestNodeSetExportIntegration::exportOfAllNamespacesIncludesApplicationNodes()
{
    m_source->objectsFolder()->addBaseDataVariable(QUaQualifiedName(1, QStringLiteral("Own")),
                                                   QUaNodeId(1, QStringLiteral("Own")));
    QUaNodeSetExportResult result;
    exportToMemory(*m_source, {}, result);
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY(result.exportedNodes.contains(QUaNodeId(1, QStringLiteral("Own"))));
    QVERIFY(result.exportedNodes.contains(QUaNodeId(this->namespaceOf(*m_source, kOtherUri), quint32(1))));
}

void TestNodeSetExportIntegration::unknownNamespaceIsRejected()
{
    QUaNodeSetExportResult result;
    const QByteArray xml = exportToMemory(*m_source, { QStringLiteral("urn:unknown") }, result);
    QVERIFY(!result.isOk());
    QVERIFY(result.errorString.contains(QStringLiteral("urn:unknown")));
    QVERIFY(result.exportedNodes.isEmpty());
    QVERIFY(xml.isEmpty());
}

void TestNodeSetExportIntegration::namespaceZeroIsRejected()
{
    QUaNodeSetExportResult result;
    exportToMemory(*m_source, { QStringLiteral("http://opcfoundation.org/UA/") }, result);
    QVERIFY(!result.isOk());
}

void TestNodeSetExportIntegration::unwritableDeviceIsRejected()
{
    QBuffer buffer;
    QVERIFY(!m_source->exportNodeSet(&buffer).isOk());
    QVERIFY(!m_source->exportNodeSet(nullptr).isOk());
}

void TestNodeSetExportIntegration::exportWritesAndReportsFiles()
{
    QTemporaryDir directory;
    const QString fileName = directory.filePath(QStringLiteral("devices.xml"));
    const QUaNodeSetExportResult result = m_source->exportNodeSet(fileName, { kDevicesUri, kOtherUri });
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY(QFile::exists(fileName));
    const QUaNodeSetResult loaded = m_target->loadNodeSet(fileName);
    QVERIFY2(loaded.isOk(), qPrintable(loaded.errorString));
    QCOMPARE(loaded.addedNodes.count(), result.exportedNodes.count());

    const QUaNodeSetExportResult failed = m_source->exportNodeSet(directory.filePath(QStringLiteral("no/such/dir.xml")));
    QVERIFY(!failed.isOk());
    QVERIFY(failed.errorString.startsWith(QStringLiteral("Cannot open")));
}

///
/// \brief A real companion specification survives an export and a load into another server.
///
void TestNodeSetExportIntegration::companionSpecificationRoundTrips()
{
#ifndef UA_GENERATED_NAMESPACE_ZERO_FULL
    QSKIP("DI builds on types of the full namespace zero, such as FileType and the alarm types");
#endif
    const QString diUri = QStringLiteral("http://opcfoundation.org/UA/DI/");
    const QUaNodeSetResult di = m_source->loadNodeSet(nodeSetPath(QStringLiteral("Opc.Ua.Di.NodeSet2.xml")));
    QVERIFY2(di.isOk(), qPrintable(di.errorString));

    QUaNodeSetExportResult result;
    const QByteArray xml = exportToMemory(*m_source, { diUri }, result);
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    QStringList missing;
    for (const QUaNodeId &nodeId : di.addedNodes)
    {
        if (!result.exportedNodes.contains(nodeId))
        {
            missing << nodeId.toXmlString();
        }
    }
    QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QLatin1Char(' '))));

    const QUaNodeSetResult loaded = loadFromMemory(*m_target, xml);
    QVERIFY2(loaded.isOk(), qPrintable(loaded.errorString));
    QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(QLatin1Char('\n'))));
    const quint16 ns = this->namespaceOf(*m_target, diUri);
    auto *deviceSet = m_target->nodeById<QUaBaseObject>(QUaNodeId(ns, quint32(5001)));
    QVERIFY(deviceSet);
    QCOMPARE(deviceSet->parent(), m_target->objectsFolder());
}

QTEST_GUILESS_MAIN(TestNodeSetExportIntegration)

#include "test_nodeset_export_integration.moc"
