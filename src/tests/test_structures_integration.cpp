#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

namespace {

const QUaNodeId kColor(1, QStringLiteral("Color"));

QUaStructure point(double x, double y)
{
    QUaStructure value(QUaNodeId(1, QStringLiteral("Point")));
    value.setField(QStringLiteral("x"), x);
    value.setField(QStringLiteral("y"), y);
    return value;
}

} // namespace

class TestStructuresIntegration : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void registerStructureAddsDataType();
    void invalidStructuresAreRejected_data();
    void invalidStructuresAreRejected();
    void variableTakesStructureValue();
    void unsetFieldsHaveDefaultValues();
    void nestedStructuresArraysAndEnumerations();
    void optionalFieldsMayBeLeftOut();
    void arraysOfStructures();
    void valueNotMatchingItsTypeIsIgnored();
    void clientsReadDefinitionAndValues();
    void clientsWriteStructures();

private:
    QUaServer *m_server = nullptr;
    QUaNodeId m_pointId;
    QUaNodeId m_segmentId;
    QUaNodeId m_taggedId;
};

///
/// \brief Every test gets a server with a Point, a Segment of two points with arrays and an enumeration, and a
///        Tagged structure with optional fields.
///
void TestStructuresIntegration::init()
{
    m_server = new QUaServer;
    QUaEnumMap colors;
    colors.insert(0, { QUaLocalizedText(QString(), QStringLiteral("Red")), QUaLocalizedText() });
    colors.insert(1, { QUaLocalizedText(QString(), QStringLiteral("Green")), QUaLocalizedText() });
    m_server->registerEnum(QStringLiteral("Color"), colors, kColor);
    m_pointId = m_server->registerStructure(QStringLiteral("Point"), {
        { QStringLiteral("x"), QMetaType::Double },
        { QStringLiteral("y"), QMetaType::Double }
    });
    m_segmentId = m_server->registerStructure(QStringLiteral("Segment"), {
        { QStringLiteral("start"), m_pointId },
        { QStringLiteral("end"), m_pointId },
        { QStringLiteral("color"), kColor },
        { QStringLiteral("tags"), QMetaType::QString, true },
        { QStringLiteral("weights"), QMetaType::Double, true }
    }, QUaNodeId(1, quint32(5000)));
    m_taggedId = m_server->registerStructure(QStringLiteral("Tagged"), {
        { QStringLiteral("id"), QMetaType::UInt },
        { QStringLiteral("comment"), QMetaType::QString, false, true },
        { QStringLiteral("limits"), QMetaType::Double, true, true }
    });
    QVERIFY(!m_pointId.isNull());
    QVERIFY(!m_segmentId.isNull());
    QVERIFY(!m_taggedId.isNull());
}

void TestStructuresIntegration::cleanup()
{
    delete m_server;
    m_server = nullptr;
}

void TestStructuresIntegration::registerStructureAddsDataType()
{
    QCOMPARE(m_pointId, QUaNodeId(1, QStringLiteral("Point")));
    QCOMPARE(m_segmentId, QUaNodeId(1, quint32(5000)));
    QVERIFY(m_server->isNodeIdUsed(m_pointId));
    QCOMPARE(m_server->structureFields(m_segmentId).count(), 5);
    QCOMPARE(m_server->structureFields(m_segmentId).at(3),
             QUaStructureField(QStringLiteral("tags"), QMetaType::QString, true));
    QVERIFY(m_server->structureFields(QUaNodeId(0, quint32(UA_NS0ID_DOUBLE))).isEmpty());
}

void TestStructuresIntegration::invalidStructuresAreRejected_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QList<QUaStructureField>>("fields");

    QTest::newRow("used NodeId") << QStringLiteral("Point")
                                 << QList<QUaStructureField>{ { QStringLiteral("x"), QMetaType::Double } };
    QTest::newRow("empty field name") << QStringLiteral("Unnamed")
                                      << QList<QUaStructureField>{ { QString(), QMetaType::Double } };
    QTest::newRow("repeated field") << QStringLiteral("Twice")
                                    << QList<QUaStructureField>{ { QStringLiteral("a"), QMetaType::Double },
                                                                 { QStringLiteral("a"), QMetaType::Int } };
    QTest::newRow("unknown field type") << QStringLiteral("Unknown")
                                        << QList<QUaStructureField>{ { QStringLiteral("a"), QUaNodeId(1, quint32(99999)) } };
}

void TestStructuresIntegration::invalidStructuresAreRejected()
{
    QFETCH(QString, name);
    QFETCH(QList<QUaStructureField>, fields);

    QVERIFY(m_server->registerStructure(name, fields).isNull());
    if (name != QStringLiteral("Point"))
    {
        QVERIFY(!m_server->isNodeIdUsed(QUaNodeId(1, name)));
    }
}

void TestStructuresIntegration::variableTakesStructureValue()
{
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("position"));

    variable->setValue(QVariant::fromValue(point(1.5, -2.5)));

    QCOMPARE(variable->dataType(), QMetaType_Structure);
    QCOMPARE(QUaNodeId(variable->dataTypeNodeId()), m_pointId);
    const QUaStructure value = variable->value().value<QUaStructure>();
    QCOMPARE(value, point(1.5, -2.5));
    QCOMPARE(value.fieldNames(), QStringList({ QStringLiteral("x"), QStringLiteral("y") }));
}

void TestStructuresIntegration::unsetFieldsHaveDefaultValues()
{
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("position"));
    QUaStructure value(m_pointId);
    value.setField(QStringLiteral("x"), 3);

    variable->setValue(QVariant::fromValue(value));

    QCOMPARE(variable->value().value<QUaStructure>(), point(3.0, 0.0));
}

void TestStructuresIntegration::nestedStructuresArraysAndEnumerations()
{
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("segment"));
    QUaStructure segment(m_segmentId);
    segment.setField(QStringLiteral("start"), QVariant::fromValue(point(0, 0)));
    segment.setField(QStringLiteral("end"), QVariant::fromValue(point(4, 3)));
    segment.setField(QStringLiteral("color"), 1);
    segment.setField(QStringLiteral("tags"), QStringList({ QStringLiteral("a"), QStringLiteral("b") }));
    segment.setField(QStringLiteral("weights"), QVariantList{ 0.25, 0.75 });

    variable->setValue(QVariant::fromValue(segment));

    const QUaStructure value = variable->value().value<QUaStructure>();
    QCOMPARE(value.field(QStringLiteral("end")).value<QUaStructure>(), point(4, 3));
    QCOMPARE(value.field(QStringLiteral("color")).toInt(), 1);
    QCOMPARE(value.field(QStringLiteral("tags")).value<QList<QString>>(), QList<QString>({ QStringLiteral("a"), QStringLiteral("b") }));
    QCOMPARE(value.field(QStringLiteral("weights")).value<QList<double>>(), QList<double>({ 0.25, 0.75 }));
}

void TestStructuresIntegration::optionalFieldsMayBeLeftOut()
{
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("tagged"));
    QUaStructure tagged(m_taggedId);
    tagged.setField(QStringLiteral("id"), 7);

    variable->setValue(QVariant::fromValue(tagged));
    const QUaStructure withoutOptional = variable->value().value<QUaStructure>();
    tagged.setField(QStringLiteral("comment"), QStringLiteral("checked"));
    tagged.setField(QStringLiteral("limits"), QVariantList{ 1.0, 2.0 });
    variable->setValue(QVariant::fromValue(tagged));
    const QUaStructure withOptional = variable->value().value<QUaStructure>();

    QCOMPARE(withoutOptional.fieldNames(), QStringList({ QStringLiteral("id") }));
    QCOMPARE(withoutOptional.field(QStringLiteral("id")).toUInt(), 7u);
    QCOMPARE(withOptional.field(QStringLiteral("comment")).toString(), QStringLiteral("checked"));
    QCOMPARE(withOptional.field(QStringLiteral("limits")).value<QList<double>>(), QList<double>({ 1.0, 2.0 }));
}

void TestStructuresIntegration::arraysOfStructures()
{
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("path"));

    variable->setValue(QVariantList{ QVariant::fromValue(point(1, 1)), QVariant::fromValue(point(2, 2)) });

    const QVariantList path = variable->value().value<QVariantList>();
    QCOMPARE(path.count(), 2);
    QCOMPARE(path.at(1).value<QUaStructure>(), point(2, 2));
}

///
/// \brief A field that cannot be converted to its type, or a structure of an unknown type, leaves the value as is.
///
void TestStructuresIntegration::valueNotMatchingItsTypeIsIgnored()
{
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("position"));
    variable->setValue(QVariant::fromValue(point(1, 2)));
    QUaStructure wrongField(m_pointId);
    wrongField.setField(QStringLiteral("x"), QStringLiteral("not a number"));
    QUaStructure unknownType(QUaNodeId(1, QStringLiteral("Nothing")));
    QUaStructure wrongNested(m_segmentId);
    wrongNested.setField(QStringLiteral("start"), QVariant::fromValue(unknownType));

    variable->setValue(QVariant::fromValue(wrongField));
    variable->setValue(QVariant::fromValue(unknownType));
    variable->setValue(QVariant::fromValue(wrongNested));

    QCOMPARE(variable->value().value<QUaStructure>(), point(1, 2));
}

///
/// \brief A client learns the layouts from the DataTypeDefinition attribute, enumerations included, and decodes the
///        values with them. The open62541 client only builds structures from them, hence a route without enumeration.
///
void TestStructuresIntegration::clientsReadDefinitionAndValues()
{
    const QUaNodeId routeId = m_server->registerStructure(QStringLiteral("Route"), {
        { QStringLiteral("name"), QMetaType::QString },
        { QStringLiteral("start"), m_pointId },
        { QStringLiteral("waypoints"), m_pointId, true }
    });
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("route"));
    QUaStructure route(routeId);
    route.setField(QStringLiteral("name"), QStringLiteral("home"));
    route.setField(QStringLiteral("start"), QVariant::fromValue(point(4, 3)));
    route.setField(QStringLiteral("waypoints"), QVariantList{ QVariant::fromValue(point(5, 5)), QVariant::fromValue(point(6, 6)) });
    variable->setValue(QVariant::fromValue(route));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QStringList segmentFields;
    QStringList colors;

    QCOMPARE(client.readDefinitionFieldNames(m_segmentId, segmentFields), UA_STATUSCODE_GOOD);
    QCOMPARE(client.readDefinitionFieldNames(kColor, colors), UA_STATUSCODE_GOOD);
    QCOMPARE(client.loadServerDataTypes({ m_pointId, routeId }), UA_STATUSCODE_GOOD);
    QVariant value;
    QCOMPARE(client.readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);

    QCOMPARE(segmentFields, QStringList({ QStringLiteral("start"), QStringLiteral("end"), QStringLiteral("color"),
                                          QStringLiteral("tags"), QStringLiteral("weights") }));
    QCOMPARE(colors, QStringList({ QStringLiteral("Red"), QStringLiteral("Green") }));
    const QUaStructure read = value.value<QUaStructure>();
    QCOMPARE(read.typeId(), routeId);
    QCOMPARE(read.field(QStringLiteral("name")).toString(), QStringLiteral("home"));
    QCOMPARE(read.field(QStringLiteral("start")).value<QUaStructure>(), point(4, 3));
    QCOMPARE(read.field(QStringLiteral("waypoints")).value<QVariantList>().at(1).value<QUaStructure>(), point(6, 6));
}

void TestStructuresIntegration::clientsWriteStructures()
{
    QUaBaseDataVariable *variable = m_server->objectsFolder()->addBaseDataVariable(QStringLiteral("position"));
    variable->setWriteAccess(true);
    variable->setValue(QVariant::fromValue(point(0, 0)));
    QVariant validated;
    variable->setWriteValidator([&validated](const QVariant &value, const QUaSession *) {
        validated = value;
        return QUaStatusCode(QUaStatus::Good);
    });
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QCOMPARE(client.loadServerDataTypes({ m_pointId }), UA_STATUSCODE_GOOD);

    QCOMPARE(client.writeValue(variable->nodeId(), QVariant::fromValue(point(5, 6))), UA_STATUSCODE_GOOD);

    QCOMPARE(validated.value<QUaStructure>(), point(5, 6));
    QCOMPARE(variable->value().value<QUaStructure>(), point(5, 6));
}

QTEST_GUILESS_MAIN(TestStructuresIntegration)

#include "test_structures_integration.moc"
