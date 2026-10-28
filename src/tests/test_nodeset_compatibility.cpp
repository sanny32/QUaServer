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
    void restoresRoleSetForImportedRoles();
    void unrelatedModelsDoNotCreateRoleSet();
    void polymorphicDefinitionsAreReported_data();
    void polymorphicDefinitionsAreReported();
    void unsupportedDefinitionsAreNotPublished();
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

///
/// \brief Reuses a restored RoleSet when importing multiple roles and preserves their child variables.
///
void TestNodeSetCompatibility::restoresRoleSetForImportedRoles()
{
#ifndef UA_GENERATED_NAMESPACE_ZERO_FULL
    QSKIP("RoleSetType and RoleType require the full namespace zero");
#endif
    QUaServer server;
    const QUaNodeId roleSet(0, quint32(UA_NS0ID_SERVER_SERVERCAPABILITIES_ROLESET));
    QVERIFY(!server.isNodeIdUsed(roleSet));
    const QUaNodeSetResult first = loadModel(server, QByteArrayLiteral(R"xml(
      <UAObject NodeId="ns=1;i=95" BrowseName="1:Seeker">
        <References>
          <Reference ReferenceType="i=47" IsForward="false">i=15606</Reference>
          <Reference ReferenceType="i=40">i=15620</Reference>
        </References>
      </UAObject>
      <UAVariable NodeId="ns=1;i=10" BrowseName="Identities" DataType="i=15634" ValueRank="1" ArrayDimensions="0">
        <References>
          <Reference ReferenceType="i=46" IsForward="false">ns=1;i=95</Reference>
          <Reference ReferenceType="i=40">i=68</Reference>
        </References>
      </UAVariable>
    )xml"));
    QVERIFY2(first.isOk(), qPrintable(first.errorString));
    QVERIFY2(first.warnings.isEmpty(), qPrintable(first.warnings.join(QLatin1Char('\n'))));
    QVERIFY(server.isNodeIdUsed(roleSet));
    QVERIFY(server.isNodeIdUsed(modelId(server, 95)));
    QVERIFY(server.nodeById<QUaBaseVariable>(modelId(server, 10)));
    const QUaNodeSetResult second = loadModel(server, QByteArrayLiteral(R"xml(
      <UAObject NodeId="ns=1;i=96" BrowseName="1:Observer">
        <References>
          <Reference ReferenceType="i=47" IsForward="false">i=15606</Reference>
          <Reference ReferenceType="i=40">i=15620</Reference>
        </References>
      </UAObject>
    )xml"));
    QVERIFY2(second.isOk(), qPrintable(second.errorString));
    QVERIFY2(second.warnings.isEmpty(), qPrintable(second.warnings.join(QLatin1Char('\n'))));
    QVERIFY(TestServer::start(server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(server)), UA_STATUSCODE_GOOD);
    QList<QUaNodeId> targets;
    QCOMPARE(client.browseTargets(roleSet, QUaNodeId(0, quint32(UA_NS0ID_HASCOMPONENT)), targets), UA_STATUSCODE_GOOD);
    QVERIFY(targets.contains(modelId(server, 95)));
    QVERIFY(targets.contains(modelId(server, 96)));
}

///
/// \brief Leaves the standard namespace untouched when models do not reference RoleSet.
///
void TestNodeSetCompatibility::unrelatedModelsDoNotCreateRoleSet()
{
    QUaServer server;
    const QUaNodeId roleSet(0, quint32(UA_NS0ID_SERVER_SERVERCAPABILITIES_ROLESET));
    QVERIFY(!server.isNodeIdUsed(roleSet));
    const QUaNodeSetResult result = loadModel(server, kTypes);
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
    QVERIFY(!server.isNodeIdUsed(roleSet));
}

///
/// \brief Distinguishes unsupported polymorphism from ordinary structure and union fields.
///
void TestNodeSetCompatibility::polymorphicDefinitionsAreReported_data()
{
    QTest::addColumn<QByteArray>("definitionAttributes");
    QTest::addColumn<QByteArray>("fieldAttributes");
    QTest::addColumn<bool>("unsupported");
    QTest::newRow("structure-subtypes") << QByteArray() << QByteArray("AllowSubTypes=\"true\"") << true;
    QTest::newRow("union-subtypes") << QByteArray("IsUnion=\"true\"") << QByteArray("AllowSubTypes=\"true\"") << true;
    QTest::newRow("any-rank-subtypes") << QByteArray() << QByteArray("AllowSubTypes=\"true\" ValueRank=\"-2\"") << true;
    QTest::newRow("numeric-boolean") << QByteArray() << QByteArray("AllowSubTypes=\"1\"") << true;
    QTest::newRow("ordinary-structure") << QByteArray() << QByteArray("AllowSubTypes=\"false\"") << false;
    QTest::newRow("ordinary-union") << QByteArray("IsUnion=\"true\"") << QByteArray("AllowSubTypes=\"false\"") << false;
}

///
/// \brief Reports unsupported definitions while retaining the rest of the model and its data type nodes.
///
void TestNodeSetCompatibility::polymorphicDefinitionsAreReported()
{
    QFETCH(QByteArray, definitionAttributes);
    QFETCH(QByteArray, fieldAttributes);
    QFETCH(bool, unsupported);
    QUaServer server;
    const QByteArray supertype = definitionAttributes.isEmpty() ? QByteArrayLiteral("i=22") : QByteArrayLiteral("i=12756");
    const QByteArray nodes = QByteArrayLiteral(R"xml(
      <UADataType NodeId="ns=1;i=3000" BrowseName="1:Point">
        <References><Reference ReferenceType="i=45" IsForward="false">i=22</Reference></References>
        <Definition Name="Point"><Field Name="X" DataType="i=11"/></Definition>
      </UADataType>
      <UADataType NodeId="ns=1;i=3001" BrowseName="1:Container">
        <References><Reference ReferenceType="i=45" IsForward="false">)xml") + supertype + QByteArrayLiteral(R"xml(</Reference></References>
        <Definition Name="Container" )xml") + definitionAttributes + QByteArrayLiteral("><Field Name=\"Value\" DataType=\"ns=1;i=3000\" ") +
        fieldAttributes + QByteArrayLiteral("/></Definition></UADataType>");
    const QUaNodeSetResult result = loadModel(server, nodes);
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY(server.isNodeIdUsed(modelId(server, 3001)));
    QCOMPARE(server.structureFields(modelId(server, 3000)).size(), 1);
    QCOMPARE(result.warnings.size(), unsupported ? 1 : 0);
    if (unsupported) {
        QVERIFY(result.warnings.first().contains(QStringLiteral("ns=1;i=3001")));
        QVERIFY(result.warnings.first().contains(QStringLiteral("AllowSubTypes is not implemented by open62541")));
        QVERIFY(server.structureFields(modelId(server, 3001)).isEmpty());
    } else {
        QCOMPARE(server.structureFields(modelId(server, 3001)).size(), 1);
    }
}

///
/// \brief Prevents clients from receiving a concrete wire layout for a polymorphic union.
///
void TestNodeSetCompatibility::unsupportedDefinitionsAreNotPublished()
{
    QUaServer server;
    const QUaNodeSetResult result = loadModel(server, QByteArrayLiteral(R"xml(
      <UADataType NodeId="ns=1;i=3000" BrowseName="1:Point">
        <References><Reference ReferenceType="i=45" IsForward="false">i=22</Reference></References>
        <Definition Name="Point"><Field Name="X" DataType="i=11"/></Definition>
      </UADataType>
      <UADataType NodeId="ns=1;i=3001" BrowseName="1:Union">
        <References><Reference ReferenceType="i=45" IsForward="false">i=12756</Reference></References>
        <Definition Name="Union" IsUnion="true"><Field Name="Value" DataType="ns=1;i=3000" AllowSubTypes="true"/></Definition>
      </UADataType>
    )xml"));
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QCOMPARE(result.warnings.size(), 1);
    QVERIFY(TestServer::start(server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(server)), UA_STATUSCODE_GOOD);
    QStringList fields;
    QCOMPARE(client.readDefinitionFieldNames(modelId(server, 3001), fields), UA_STATUSCODE_BADATTRIBUTEIDINVALID);
    QCOMPARE(client.readDefinitionFieldNames(modelId(server, 3000), fields), UA_STATUSCODE_GOOD);
    QCOMPARE(fields, QStringList{QStringLiteral("X")});
}

QTEST_GUILESS_MAIN(TestNodeSetCompatibility)
#include "test_nodeset_compatibility.moc"
