#include <QTest>

#include <QUaServer>

class Sensor : public QUaBaseObject
{
    Q_OBJECT
    Q_PROPERTY(QUaBaseDataVariable *reading READ reading)

public:
    Q_INVOKABLE explicit Sensor(QUaServer *server)
        : QUaBaseObject(server)
    {
        reading()->setDataType(QMetaType::Double);
        reading()->setValue(0.0);
    }

    QUaBaseDataVariable *reading() { return browseChild<QUaBaseDataVariable>(QStringLiteral("reading")); }
};

///
/// \brief Serializer that keeps every written node in memory, keyed by NodeId.
///
class MemorySerializer
{
public:
    struct Node
    {
        QString typeName;
        QMap<QString, QVariant> attrs;
        QList<QUaForwardReference> forwardRefs;
    };

    QHash<QUaNodeId, Node> nodes;
    bool started = false;
    bool ended = false;
    bool deserializeStarted = false;
    bool deserializeEnded = false;
    bool failReads = false;
    bool failStart = false;

    bool serializeStart(QQueue<QUaLog> &logOut)
    {
        Q_UNUSED(logOut);
        if (failStart)
        {
            return false;
        }
        nodes.clear();
        started = true;
        return true;
    }

    bool serializeEnd(QQueue<QUaLog> &logOut)
    {
        Q_UNUSED(logOut);
        ended = true;
        return true;
    }

    bool writeInstance(const QUaNodeId &nodeId,
                       const QString &typeName,
                       const QMap<QString, QVariant> &attrs,
                       const QList<QUaForwardReference> &forwardRefs,
                       QQueue<QUaLog> &logOut)
    {
        Q_UNUSED(logOut);
        nodes.insert(nodeId, { typeName, attrs, forwardRefs });
        return true;
    }

    bool deserializeStart(QQueue<QUaLog> &logOut)
    {
        Q_UNUSED(logOut);
        deserializeStarted = true;
        return true;
    }

    bool deserializeEnd(QQueue<QUaLog> &logOut)
    {
        Q_UNUSED(logOut);
        deserializeEnded = true;
        return true;
    }

    bool readInstance(const QUaNodeId &nodeId,
                      const QString &typeName,
                      QMap<QString, QVariant> &attrs,
                      QList<QUaForwardReference> &forwardRefs,
                      QQueue<QUaLog> &logOut)
    {
        Q_UNUSED(typeName);
        if (failReads || !nodes.contains(nodeId))
        {
            logOut << QUaLog(QStringLiteral("Missing node %1").arg(nodeId.toXmlString()),
                             QUaLogLevel::Error, QUaLogCategory::Serialization);
            return false;
        }
        attrs = nodes.value(nodeId).attrs;
        forwardRefs = nodes.value(nodeId).forwardRefs;
        return true;
    }
};

///
/// \brief Serializer that implements only the required writeInstance(), none of the optional hooks.
///
class MinimalSerializer
{
public:
    int written = 0;

    bool writeInstance(const QUaNodeId &nodeId,
                       const QString &typeName,
                       const QMap<QString, QVariant> &attrs,
                       const QList<QUaForwardReference> &forwardRefs,
                       QQueue<QUaLog> &logOut)
    {
        Q_UNUSED(nodeId);
        Q_UNUSED(typeName);
        Q_UNUSED(attrs);
        Q_UNUSED(forwardRefs);
        Q_UNUSED(logOut);
        ++written;
        return true;
    }
};

class TestSerialization : public QObject
{
    Q_OBJECT

private slots:
    void serializeWritesWholeSubtree();
    void serializerWithoutHooksWorks();
    void failingSerializeStartAborts();
    void roundTripRestoresNodesAndValues();
    void roundTripRestoresCustomReferences();
    void roundTripRestoresMatrices();
    void failedReadIsReported();
};

namespace {

///
/// \brief Builds the tree used by the tests: plant/sensor (Sensor) and plant/setpoint.
///
QUaFolderObject *buildPlant(QUaServer &server)
{
    QUaFolderObject *plant = server.objectsFolder()->addFolderObject(QStringLiteral("plant"),
                                                                     QUaNodeId(1, QStringLiteral("plant")));
    Sensor *sensor = plant->addChild<Sensor>(QStringLiteral("sensor"), QUaNodeId(1, QStringLiteral("plant.sensor")));
    sensor->reading()->setValue(21.5);
    QUaBaseDataVariable *setpoint = plant->addBaseDataVariable(QStringLiteral("setpoint"),
                                                               QUaNodeId(1, QStringLiteral("plant.setpoint")));
    setpoint->setValue(40);
    setpoint->setWriteAccess(true);
    return plant;
}

} // namespace

///
/// \brief Serializing a node writes it and all of its descendants, wrapped in start/end.
///
void TestSerialization::serializeWritesWholeSubtree()
{
    QUaServer server;
    QUaFolderObject *plant = buildPlant(server);
    MemorySerializer serializer;
    QQueue<QUaLog> logOut;

    QVERIFY(plant->serialize(serializer, logOut));

    QVERIFY(serializer.started);
    QVERIFY(serializer.ended);
    QVERIFY(logOut.isEmpty());
    QVERIFY(serializer.nodes.contains(QUaNodeId(1, QStringLiteral("plant"))));
    QVERIFY(serializer.nodes.contains(QUaNodeId(1, QStringLiteral("plant.sensor"))));
    QVERIFY(serializer.nodes.contains(QUaNodeId(1, QStringLiteral("plant.setpoint"))));
    QCOMPARE(serializer.nodes.value(QUaNodeId(1, QStringLiteral("plant.sensor"))).typeName, QStringLiteral("Sensor"));
}

void TestSerialization::serializerWithoutHooksWorks()
{
    QUaServer server;
    QUaFolderObject *plant = buildPlant(server);
    MinimalSerializer serializer;
    QQueue<QUaLog> logOut;

    QVERIFY(plant->serialize(serializer, logOut));

    QVERIFY(serializer.written >= 3);
}

void TestSerialization::failingSerializeStartAborts()
{
    QUaServer server;
    QUaFolderObject *plant = buildPlant(server);
    MemorySerializer serializer;
    serializer.failStart = true;
    QQueue<QUaLog> logOut;

    QVERIFY(!plant->serialize(serializer, logOut));

    QVERIFY(serializer.nodes.isEmpty());
    QVERIFY(!serializer.ended);
}

///
/// \brief Deserializing into an empty server recreates the tree with its values and access.
///
void TestSerialization::roundTripRestoresNodesAndValues()
{
    MemorySerializer serializer;
    QQueue<QUaLog> logOut;
    {
        QUaServer source;
        source.registerType<Sensor>();
        QVERIFY(buildPlant(source)->serialize(serializer, logOut));
    }

    QUaServer target;
    target.registerType<Sensor>();
    QUaFolderObject *plant = target.objectsFolder()->addFolderObject(QStringLiteral("plant"),
                                                                     QUaNodeId(1, QStringLiteral("plant")));
    QVERIFY(plant->deserialize(serializer, logOut));

    QVERIFY(serializer.deserializeStarted);
    QVERIFY(serializer.deserializeEnded);
    auto sensor = target.nodeById<Sensor>(QUaNodeId(1, QStringLiteral("plant.sensor")));
    auto setpoint = target.nodeById<QUaBaseDataVariable>(QUaNodeId(1, QStringLiteral("plant.setpoint")));
    QVERIFY(sensor);
    QVERIFY(setpoint);
    QCOMPARE(sensor->reading()->value<double>(), 21.5);
    QCOMPARE(setpoint->value<int>(), 40);
    QVERIFY(setpoint->writeAccess());
}

///
/// \brief A matrix variable is restored with its shape and its ValueRank, which open62541 only accepts before
///        the value is written.
///
void TestSerialization::roundTripRestoresMatrices()
{
    MemorySerializer serializer;
    QQueue<QUaLog> logOut;
    {
        QUaServer source;
        QUaFolderObject *plant = buildPlant(source);
        QUaBaseDataVariable *matrix = plant->addBaseDataVariable(QStringLiteral("matrix"),
                                                                 QUaNodeId(1, QStringLiteral("plant.matrix")));
        matrix->setValueRank(2);
        matrix->setValue(QVariantList{ QVariantList{ 1, 2, 3 }, QVariantList{ 4, 5, 6 } });
        QVERIFY(plant->serialize(serializer, logOut));
    }

    QUaServer target;
    QUaFolderObject *plant = target.objectsFolder()->addFolderObject(QStringLiteral("plant"),
                                                                     QUaNodeId(1, QStringLiteral("plant")));
    QVERIFY(plant->deserialize(serializer, logOut));

    auto matrix = target.nodeById<QUaBaseDataVariable>(QUaNodeId(1, QStringLiteral("plant.matrix")));
    QVERIFY(matrix);
    QCOMPARE(matrix->valueRank(), 2);
    const QVariantList rows = matrix->value().value<QVariantList>();
    QCOMPARE(rows.count(), 2);
    QCOMPARE(rows.at(1).value<QList<int>>(), QList<int>({ 4, 5, 6 }));
}

///
/// \brief Non-hierarchical references survive the round trip.
///
void TestSerialization::roundTripRestoresCustomReferences()
{
    const QUaReferenceType controls = { QStringLiteral("Controls"), QStringLiteral("ControlledBy") };
    MemorySerializer serializer;
    QQueue<QUaLog> logOut;
    {
        QUaServer source;
        source.registerType<Sensor>();
        source.registerReferenceType(controls);
        QUaFolderObject *plant = buildPlant(source);
        auto setpoint = plant->browseChild(QStringLiteral("setpoint"));
        auto sensor = plant->browseChild(QStringLiteral("sensor"));
        setpoint->addReference(controls, sensor);
        QVERIFY(plant->serialize(serializer, logOut));
    }

    QUaServer target;
    target.registerType<Sensor>();
    target.registerReferenceType(controls);
    QUaFolderObject *plant = target.objectsFolder()->addFolderObject(QStringLiteral("plant"),
                                                                     QUaNodeId(1, QStringLiteral("plant")));
    QVERIFY(plant->deserialize(serializer, logOut));

    auto setpoint = target.nodeById(QUaNodeId(1, QStringLiteral("plant.setpoint")));
    auto sensor = target.nodeById(QUaNodeId(1, QStringLiteral("plant.sensor")));
    QVERIFY(setpoint && sensor);
    QCOMPARE(setpoint->findReferences(controls), QList<QUaNode *>({ sensor }));
}

///
/// \brief A serializer that cannot provide a node makes deserialize() fail with its log.
///
void TestSerialization::failedReadIsReported()
{
    QUaServer server;
    QUaFolderObject *plant = buildPlant(server);
    MemorySerializer serializer;
    serializer.failReads = true;
    QQueue<QUaLog> logOut;

    QVERIFY(!plant->deserialize(serializer, logOut));

    QVERIFY(!logOut.isEmpty());
    QCOMPARE(logOut.first().category, QUaLogCategory::Serialization);
}

QTEST_GUILESS_MAIN(TestSerialization)

#include "test_serialization.moc"
