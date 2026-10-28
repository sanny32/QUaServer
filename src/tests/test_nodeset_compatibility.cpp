#include <QBuffer>
#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

namespace {

const QString kUri = QStringLiteral("urn:quaserver:test:compatibility");

///
/// \brief Loads an isolated model without relying on companion specification downloads.
///
QUaNodeSetResult loadModel(QUaServer &server, const QByteArray &nodes)
{
    QBuffer buffer;
    buffer.setData(QByteArrayLiteral(
        "<UANodeSet xmlns=\"http://opcfoundation.org/UA/2011/03/UANodeSet.xsd\">"
        "<NamespaceUris><Uri>urn:quaserver:test:compatibility</Uri></NamespaceUris>")
        + nodes + QByteArrayLiteral("</UANodeSet>"));
    buffer.open(QIODevice::ReadOnly);
    return server.loadNodeSet(&buffer);
}

///
/// \brief Resolves a fixture identifier after namespace remapping by the server.
///
QUaNodeId modelId(const QUaServer &server, quint32 identifier)
{
    return QUaNodeId(static_cast<quint16>(server.namespaces().indexOf(kUri)), identifier);
}

const QByteArray kTypes = QByteArrayLiteral(R"xml(
  <UADataType NodeId="ns=1;i=3003" BrowseName="1:Record">
    <References>
      <Reference ReferenceType="i=45" IsForward="false">i=22</Reference>
      <Reference ReferenceType="i=38">ns=1;i=5003</Reference>
    </References>
    <Definition Name="Record">
      <Field Name="Label" DataType="ns=1;i=3004" IsOptional="true"/>
      <Field Name="Choice" DataType="ns=1;i=3001" IsOptional="true"/>
    </Definition>
  </UADataType>
  <UADataType NodeId="ns=1;i=3001" BrowseName="1:Choice">
    <References>
      <Reference ReferenceType="i=45" IsForward="false">i=12756</Reference>
      <Reference ReferenceType="i=38">ns=1;i=5001</Reference>
    </References>
    <Definition Name="Choice" IsUnion="true">
      <Field Name="Text" DataType="i=12"/>
      <Field Name="Numbers" DataType="i=11" ValueRank="1"/>
      <Field Name="Anything" DataType="i=24"/>
    </Definition>
  </UADataType>
  <UADataType NodeId="ns=1;i=3004" BrowseName="1:Label">
    <References><Reference ReferenceType="i=45" IsForward="false">ns=1;i=3000</Reference></References>
  </UADataType>
  <UADataType NodeId="ns=1;i=3000" BrowseName="1:Text">
    <References><Reference ReferenceType="i=45" IsForward="false">i=12</Reference></References>
  </UADataType>
  <UAObject NodeId="ns=1;i=5001" BrowseName="Default Binary"/>
  <UAObject NodeId="ns=1;i=5003" BrowseName="Default Binary"/>
  <UAVariable NodeId="ns=1;i=6001" BrowseName="1:ChoiceValue" DataType="ns=1;i=3001" AccessLevel="3">
    <References><Reference ReferenceType="i=35" IsForward="false">i=85</Reference></References>
  </UAVariable>
  <UAVariable NodeId="ns=1;i=6003" BrowseName="1:RecordValue" DataType="ns=1;i=3003" AccessLevel="3">
    <References><Reference ReferenceType="i=35" IsForward="false">i=85</Reference></References>
  </UAVariable>
)xml");

} // namespace

class TestNodeSetCompatibility : public QObject
{
    Q_OBJECT

private slots:
    void omittedArrayMetadata_data();
    void omittedArrayMetadata();
    void explicitIncompatibleMetadataIsRejected();
    void primitiveSubtypesAndUnionValues();
    void invalidUnionValuesAreRejected();
    void clientsReadAndWriteUnions();
    void unionExportRoundTrip();
};

///
/// \brief Covers inherited array constraints and unknown lengths for multidimensional arrays.
///
void TestNodeSetCompatibility::omittedArrayMetadata_data()
{
    QTest::addColumn<QByteArray>("attributes");
    QTest::addColumn<QByteArray>("typeId");
    QTest::addColumn<QVector<quint32>>("dimensions");
    QTest::newRow("inherited-rank-and-dimensions") << QByteArray() << QByteArray("ns=1;i=1001") << QVector<quint32>{5};
    QTest::newRow("inherited-dimensions") << QByteArray("ValueRank=\"1\"") << QByteArray("ns=1;i=1001") << QVector<quint32>{5};
    QTest::newRow("one-unknown-dimension") << QByteArray("ValueRank=\"1\"") << QByteArray("i=63") << QVector<quint32>{0};
    QTest::newRow("two-unknown-dimensions") << QByteArray("ValueRank=\"2\"") << QByteArray("i=63") << QVector<quint32>({0, 0});
}

///
/// \brief Checks that omitted metadata preserves the variable instead of dropping it during type checks.
///
void TestNodeSetCompatibility::omittedArrayMetadata()
{
    QFETCH(QByteArray, attributes);
    QFETCH(QByteArray, typeId);
    QFETCH(QVector<quint32>, dimensions);
    QUaServer server;
    const QUaNodeSetResult result = loadModel(server, QByteArrayLiteral(R"xml(
      <UAVariableType NodeId="ns=1;i=1001" BrowseName="1:ArrayType" DataType="i=11" ValueRank="1" ArrayDimensions="5">
        <References><Reference ReferenceType="i=45" IsForward="false">i=63</Reference></References>
      </UAVariableType>
      <UAVariable NodeId="ns=1;i=2001" BrowseName="1:Array" DataType="i=11" )xml") + attributes +
      QByteArrayLiteral("><References><Reference ReferenceType=\"i=40\">") + typeId +
      QByteArrayLiteral("</Reference></References></UAVariable>"));
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    auto *variable = server.nodeById<QUaBaseDataVariable>(modelId(server, 2001));
    QVERIFY(variable);
    QCOMPARE(variable->valueRank(), dimensions.size());
    QCOMPARE(variable->arrayDimensions(), dimensions);
}

///
/// \brief Keeps explicit scalar constraints authoritative even when their type requires an array.
///
void TestNodeSetCompatibility::explicitIncompatibleMetadataIsRejected()
{
    QUaServer server;
    const QUaNodeSetResult result = loadModel(server, QByteArrayLiteral(R"xml(
      <UAVariable NodeId="ns=1;i=2001" BrowseName="1:Invalid" DataType="i=11" ValueRank="-1">
        <References><Reference ReferenceType="i=40">ns=1;i=1001</Reference></References>
      </UAVariable>
      <UAVariableType NodeId="ns=1;i=1001" BrowseName="1:ArrayType" DataType="i=11" ValueRank="1" ArrayDimensions="0">
        <References><Reference ReferenceType="i=45" IsForward="false">i=63</Reference></References>
      </UAVariableType>
    )xml"));
    QVERIFY(result.isOk());
    QVERIFY(!server.isNodeIdUsed(modelId(server, 2001)));
    QVERIFY(result.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("BadTypeMismatch")));
}

///
/// \brief Exercises primitive aliases inside optional structures and unions, including empty and array selections.
///
void TestNodeSetCompatibility::primitiveSubtypesAndUnionValues()
{
    QUaServer server;
    const QUaNodeSetResult result = loadModel(server, kTypes);
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    auto *variable = server.nodeById<QUaBaseDataVariable>(modelId(server, 6001));
    auto *recordVariable = server.nodeById<QUaBaseDataVariable>(modelId(server, 6003));
    QVERIFY(variable);
    QVERIFY(recordVariable);
    QUaStructure choice(modelId(server, 3001));
    QCOMPARE(variable->value().value<QUaStructure>(), choice);
    choice.setField(QStringLiteral("Text"), QStringLiteral("hello"));
    variable->setValue(QVariant::fromValue(choice));
    QCOMPARE(variable->value().value<QUaStructure>(), choice);
    QUaStructure record(modelId(server, 3003));
    record.setField(QStringLiteral("Label"), QStringLiteral("tag"));
    record.setField(QStringLiteral("Choice"), QVariant::fromValue(choice));
    recordVariable->setValue(QVariant::fromValue(record));
    QCOMPARE(recordVariable->value().value<QUaStructure>(), record);
    choice.removeField(QStringLiteral("Text"));
    choice.setField(QStringLiteral("Numbers"), QVariant::fromValue(QList<double>{1.0, 2.0}));
    variable->setValue(QVariant::fromValue(choice));
    QCOMPARE(variable->value().value<QUaStructure>(), choice);
    choice.setField(QStringLiteral("Numbers"), QVariant::fromValue(QList<double>()));
    variable->setValue(QVariant::fromValue(choice));
    QCOMPARE(variable->value().value<QUaStructure>(), choice);
    choice.removeField(QStringLiteral("Numbers"));
    choice.setField(QStringLiteral("Anything"), 42);
    variable->setValue(QVariant::fromValue(choice));
    QCOMPARE(variable->value().value<QUaStructure>(), choice);
}

///
/// \brief Rejects ambiguous selections and unknown fields without changing the last valid union value.
///
void TestNodeSetCompatibility::invalidUnionValuesAreRejected()
{
    QUaServer server;
    const QUaNodeSetResult result = loadModel(server, kTypes);
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    auto *variable = server.nodeById<QUaBaseDataVariable>(modelId(server, 6001));
    QVERIFY(variable);
    QUaStructure valid(modelId(server, 3001));
    valid.setField(QStringLiteral("Text"), QStringLiteral("keep"));
    variable->setValue(QVariant::fromValue(valid));
    QUaStructure ambiguous = valid;
    ambiguous.setField(QStringLiteral("Numbers"), QVariantList{1.0});
    variable->setValue(QVariant::fromValue(ambiguous));
    QCOMPARE(variable->value().value<QUaStructure>(), valid);
    QUaStructure unknown(modelId(server, 3001));
    unknown.setField(QStringLiteral("Missing"), 1);
    variable->setValue(QVariant::fromValue(unknown));
    QCOMPARE(variable->value().value<QUaStructure>(), valid);
    QUaStructure wrongArray(modelId(server, 3001));
    wrongArray.setField(QStringLiteral("Numbers"), QStringLiteral("invalid"));
    variable->setValue(QVariant::fromValue(wrongArray));
    QCOMPARE(variable->value().value<QUaStructure>(), valid);
}

///
/// \brief Checks union definitions and binary values through an independent OPC UA client.
///
void TestNodeSetCompatibility::clientsReadAndWriteUnions()
{
    QUaServer server;
    const QUaNodeSetResult result = loadModel(server, kTypes);
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    auto *variable = server.nodeById<QUaBaseDataVariable>(modelId(server, 6001));
    QVERIFY(variable);
    QUaStructure choice(modelId(server, 3001));
    choice.setField(QStringLiteral("Text"), QStringLiteral("read"));
    variable->setValue(QVariant::fromValue(choice));
    QVERIFY(TestServer::start(server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(server)), UA_STATUSCODE_GOOD);
    QCOMPARE(client.loadServerDataTypes({modelId(server, 3001)}), UA_STATUSCODE_GOOD);
    QVariant value;
    QCOMPARE(client.readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);
    QCOMPARE(value.value<QUaStructure>(), choice);
    choice.removeField(QStringLiteral("Text"));
    choice.setField(QStringLiteral("Numbers"), QVariant::fromValue(QList<double>{3.0, 4.0}));
    QCOMPARE(client.writeValue(variable->nodeId(), QVariant::fromValue(choice)), UA_STATUSCODE_GOOD);
    QCOMPARE(variable->value().value<QUaStructure>(), choice);
}

///
/// \brief Preserves union definitions and Variant selections through NodeSet XML export and reimport.
///
void TestNodeSetCompatibility::unionExportRoundTrip()
{
    QUaServer source;
    const QUaNodeSetResult loaded = loadModel(source, kTypes);
    QVERIFY2(loaded.warnings.isEmpty(), qPrintable(loaded.warnings.join(QLatin1Char('\n'))));
    auto *variable = source.nodeById<QUaBaseDataVariable>(modelId(source, 6001));
    QVERIFY(variable);
    QUaStructure choice(modelId(source, 3001));
    choice.setField(QStringLiteral("Anything"), 42);
    variable->setValue(QVariant::fromValue(choice));
    QBuffer buffer;
    QVERIFY(buffer.open(QIODevice::ReadWrite));
    const QUaNodeSetExportResult exported = source.exportNodeSet(&buffer, {kUri});
    QVERIFY2(exported.isOk(), qPrintable(exported.errorString));
    QVERIFY2(exported.warnings.isEmpty(), qPrintable(exported.warnings.join(QLatin1Char('\n'))));
    QVERIFY(buffer.seek(0));
    QUaServer destination;
    const QUaNodeSetResult reimported = destination.loadNodeSet(&buffer);
    QVERIFY2(reimported.isOk(), qPrintable(reimported.errorString));
    QVERIFY2(reimported.warnings.isEmpty(), qPrintable(reimported.warnings.join(QLatin1Char('\n'))));
    auto *copy = destination.nodeById<QUaBaseDataVariable>(modelId(destination, 6001));
    QVERIFY(copy);
    QCOMPARE(copy->value().value<QUaStructure>(), choice);
}

QTEST_GUILESS_MAIN(TestNodeSetCompatibility)
#include "test_nodeset_compatibility.moc"
