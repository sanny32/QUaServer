#include <QSignalSpy>
#include <QTest>

#include <QUaServer>

class Pump : public QUaBaseObject
{
    Q_OBJECT
    Q_PROPERTY(QUaProperty *model READ model)
    Q_PROPERTY(QUaBaseDataVariable *speed READ speed)

public:
    Q_INVOKABLE explicit Pump(QUaServer *server)
        : QUaBaseObject(server)
    {
        model()->setValue(QStringLiteral("P-100"));
        speed()->setDataType(QMetaType::Double);
        speed()->setValue(0.0);
    }

    QUaProperty *model() { return browseChild<QUaProperty>(QStringLiteral("model")); }
    QUaBaseDataVariable *speed() { return browseChild<QUaBaseDataVariable>(QStringLiteral("speed")); }
};

class PumpRegistry : public QObject
{
public:
    void add(Pump *pump) { pumps << pump; }

    QList<Pump *> pumps;
};

class TestAddressSpace : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void childrenGetRequestedIdsAndClasses();
    void childrenAreBrowsable();
    void browsePathResolvesFromObjectsFolder();
    void childAddedIsEmitted();
    void deletingNodeRemovesItFromServer();
    void deletingParentRemovesChildren();
    void stringNodeIdOutlivesSourceString();
    void valueTypeIsDeducedFromFirstValue();
    void compatibleValueKeepsDataType();
    void forcedDataTypeConvertsValue();
    void arrayValueUsesElementType();
    void arrayRankIsExplicit();
    void valueChangedReportsLocalChange();
    void statusAndTimestampsAreStored();
    void readCallbackSuppliesValue();
    void accessLevelFlags();
    void displayNameAndDescriptionNotify();
    void enumDataTypeRequiresRegisteredEnum();
    void customTypeInstantiatesPropertyChildren();
    void customTypeTracksInstances();
    void instanceCreatedCallbackFires();
    void instanceCreatedMemberCallbackFires();
    void referencesCanBeAddedAndRemoved();
    void cloneCopiesTypeAndValues();
    void duplicateBrowseNameIsRejected();
    void duplicateNodeIdIsRejected();

private:
    QUaServer *_server = nullptr;
    QUaFolderObject *objects() const { return _server->objectsFolder(); }
};

///
/// \brief Every test gets a fresh server so address spaces never leak between tests.
///
void TestAddressSpace::init()
{
    _server = new QUaServer;
}

///
/// \brief Deletes the per-test server with its address space.
///
void TestAddressSpace::cleanup()
{
    delete _server;
    _server = nullptr;
}

///
/// \brief Each add* helper creates a node of the expected class with the requested id and browse name.
///
void TestAddressSpace::childrenGetRequestedIdsAndClasses()
{
    QUaFolderObject *folder = objects()->addFolderObject(QStringLiteral("folder"), QUaNodeId(1, QStringLiteral("folder")));
    QUaBaseObject *object = folder->addBaseObject(QStringLiteral("object"));
    QUaBaseDataVariable *variable = object->addBaseDataVariable(QStringLiteral("variable"), QUaNodeId(1, 500u));
    QUaProperty *property = variable->addProperty(QStringLiteral("property"));

    QVERIFY(folder && object && variable && property);
    QCOMPARE(folder->nodeId(), QUaNodeId(1, QStringLiteral("folder")));
    QCOMPARE(variable->nodeId(), QUaNodeId(1, 500u));
    QVERIFY(!object->nodeId().isNull());
    QCOMPARE(object->nodeClass(), QStringLiteral("OBJECT"));
    QCOMPARE(property->nodeClass(), QStringLiteral("VARIABLE"));
    QCOMPARE(variable->browseName(), QUaQualifiedName(0, QStringLiteral("variable")));
    QCOMPARE(folder->typeDefinitionNodeId(), QUaNodeId(0, quint32(UA_NS0ID_FOLDERTYPE)));
    QCOMPARE(property->typeDefinitionNodeId(), QUaNodeId(0, quint32(UA_NS0ID_PROPERTYTYPE)));
    QCOMPARE(_server->nodeById<QUaBaseDataVariable>(QUaNodeId(1, 500u)), variable);
}

///
/// \brief Children are found by browse name, by type and by hasChild().
///
void TestAddressSpace::childrenAreBrowsable()
{
    QUaFolderObject *folder = objects()->addFolderObject(QStringLiteral("folder"));
    QUaBaseDataVariable *first = folder->addBaseDataVariable(QStringLiteral("first"));
    folder->addBaseObject(QStringLiteral("second"));

    QCOMPARE(folder->browseChild(QStringLiteral("first")), first);
    QCOMPARE(folder->browseChild<QUaBaseDataVariable>(QStringLiteral("first")), first);
    QVERIFY(!folder->browseChild<QUaBaseObject>(QStringLiteral("first")));
    QVERIFY(!folder->browseChild(QStringLiteral("missing")));
    QVERIFY(folder->hasChild(QStringLiteral("second")));
    QCOMPARE(folder->browseChildren().count(), 2);
    QCOMPARE(folder->browseChildren<QUaBaseDataVariable>(), QList<QUaBaseDataVariable *>({ first }));
}

///
/// \brief A node's browse path starts at the objects folder and resolves back to the node.
///
void TestAddressSpace::browsePathResolvesFromObjectsFolder()
{
    QUaBaseDataVariable *leaf = objects()
        ->addFolderObject(QStringLiteral("plant"))
        ->addBaseObject(QStringLiteral("line"))
        ->addBaseDataVariable(QStringLiteral("level"));

    const QUaBrowsePath path = leaf->nodeBrowsePath();

    QCOMPARE(QUaQualifiedName::reduceName(path), QStringLiteral("Objects/plant/line/level"));
    QCOMPARE(_server->browsePath(path), leaf);
    QCOMPARE(_server->browsePath(path.mid(1)), leaf);
    QVERIFY(!_server->browsePath(QUaQualifiedName::expandName(QStringLiteral("plant/none"))));
    QVERIFY(!_server->browsePath(QUaBrowsePath()));
}

///
/// \brief Adding a child announces it on the parent.
///
void TestAddressSpace::childAddedIsEmitted()
{
    QUaFolderObject *folder = objects()->addFolderObject(QStringLiteral("folder"));
    QSignalSpy spy(folder, &QUaNode::childAdded);

    QUaBaseObject *child = folder->addBaseObject(QStringLiteral("child"));

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().value<QUaNode *>(), child);
}

///
/// \brief Deleting the C++ object deletes the OPC UA node.
///
void TestAddressSpace::deletingNodeRemovesItFromServer()
{
    const QUaNodeId nodeId(1, QStringLiteral("doomed"));
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("doomed"), nodeId);

    delete variable;

    QVERIFY(!_server->nodeById(nodeId));
    QVERIFY(!_server->isNodeIdUsed(nodeId));
    QVERIFY(!objects()->hasChild(QStringLiteral("doomed")));
}

///
/// \brief Deleting a parent takes its whole subtree along.
///
void TestAddressSpace::deletingParentRemovesChildren()
{
    QUaFolderObject *folder = objects()->addFolderObject(QStringLiteral("folder"));
    const QUaNodeId childId(1, QStringLiteral("folder.child"));
    folder->addBaseDataVariable(QStringLiteral("child"), childId);

    delete folder;

    QVERIFY(!_server->isNodeIdUsed(childId));
}

///
/// \brief Regression: a node built from a temporary string NodeId kept a dangling identifier.
///
void TestAddressSpace::stringNodeIdOutlivesSourceString()
{
    QUaBaseDataVariable *variable = nullptr;
    {
        const QString temporary = QStringLiteral("ns=1;s=Temporary.%1").arg(42);
        variable = objects()->addBaseDataVariable(QStringLiteral("temporary"), temporary);
    }

    QCOMPARE(variable->nodeId().stringId(), QStringLiteral("Temporary.42"));
    QCOMPARE(_server->nodeById(QUaNodeId(1, QStringLiteral("Temporary.42"))), variable);
}

///
/// \brief The first value fixes the data type of a variable created without one.
///
void TestAddressSpace::valueTypeIsDeducedFromFirstValue()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("var"));

    variable->setValue(QStringLiteral("text"));

    QCOMPARE(variable->dataType(), QMetaType::QString);
    QCOMPARE(variable->value().toString(), QStringLiteral("text"));
    QCOMPARE(variable->dataTypeNodeId(), QStringLiteral("ns=0;i=%1").arg(UA_NS0ID_STRING));
}

///
/// \brief A value convertible to the current type is stored in that type.
///
void TestAddressSpace::compatibleValueKeepsDataType()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("var"));
    variable->setValue(10);

    variable->setValue(QStringLiteral("25"));

    QCOMPARE(variable->dataType(), QMetaType::Int);
    QCOMPARE(variable->value<int>(), 25);
}

///
/// \brief An explicit data type wins over the type of the value.
///
void TestAddressSpace::forcedDataTypeConvertsValue()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("var"));
    variable->setDataType(QMetaType::Double);

    variable->setValue(3);

    QCOMPARE(variable->dataType(), QMetaType::Double);
    QCOMPARE(variable->value().metaType().id(), int(QMetaType::Double));
    QCOMPARE(variable->value<double>(), 3.0);
}

///
/// \brief An array value takes its element type as the variable's data type.
///
void TestAddressSpace::arrayValueUsesElementType()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("array"));

    variable->setValue(QList<quint16>({ 1, 2, 3, 4 }));

    QCOMPARE(variable->dataType(), QMetaType::UShort);
    QCOMPARE(variable->value<QList<quint16>>(), QList<quint16>({ 1, 2, 3, 4 }));
}

///
/// \brief The value rank derived from a value can be applied to the variable explicitly.
///
void TestAddressSpace::arrayRankIsExplicit()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("array"));
    const QVariant value = QVariant::fromValue(QList<double>({ 0.5, 1.5, 2.5 }));

    variable->setValueRank(QUaBaseVariable::GetValueRankFromQVariant(value));
    variable->setValue(value);

    QCOMPARE(variable->valueRank(), UA_VALUERANK_ONE_DIMENSION);
    QCOMPARE(QUaBaseVariable::GetArrayDimensionsFromQVariant(value), QVector<quint32>({ 3 }));
    QCOMPARE(QUaBaseVariable::GetValueRankFromQVariant(QVariant(1)), UA_VALUERANK_SCALAR);
    QCOMPARE(QUaBaseVariable::GetValueRankFromQVariant(QVariant()), UA_VALUERANK_ANY);
    QVERIFY(QUaBaseVariable::GetArrayDimensionsFromQVariant(QVariant(1)).isEmpty());
}

///
/// \brief Local writes are reported with networkChange set to false.
///
void TestAddressSpace::valueChangedReportsLocalChange()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("var"));
    QSignalSpy spy(variable, &QUaBaseVariable::valueChanged);

    variable->setValue(7);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 7);
    QCOMPARE(spy.first().at(1).toBool(), false);
}

///
/// \brief Status code and source timestamp passed with a value are kept with it.
///
void TestAddressSpace::statusAndTimestampsAreStored()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("var"));
    const QDateTime source(QDate(2026, 1, 2), QTime(3, 4, 5), QTimeZone::UTC);

    variable->setValue(1.5, QUaStatus::UncertainLastUsableValue, source);

    QVERIFY(variable->statusCode() == QUaStatus::UncertainLastUsableValue);
    QCOMPARE(variable->sourceTimestamp(), source);
    QVERIFY(variable->serverTimestamp().isValid());
}

///
/// \brief With a read callback installed, reads return what the callback produces.
///
void TestAddressSpace::readCallbackSuppliesValue()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("var"));
    variable->setValue(0);
    int calls = 0;

    variable->setReadCallback([&calls]() { return QVariant(++calls * 100); });

    QCOMPARE(variable->value<int>(), 100);
    QCOMPARE(variable->value<int>(), 200);

    variable->setReadCallback();
    QCOMPARE(variable->value<int>(), 200);
}

///
/// \brief Read and write access map onto the access level bits.
///
void TestAddressSpace::accessLevelFlags()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("var"));

    QVERIFY(variable->readAccess());
    QVERIFY(!variable->writeAccess());

    variable->setWriteAccess(true);
    QVERIFY(variable->accessLevel() & UA_ACCESSLEVELMASK_WRITE);

    variable->setReadAccess(false);
    QVERIFY(!(variable->accessLevel() & UA_ACCESSLEVELMASK_READ));
    QVERIFY(variable->writeAccess());
}

///
/// \brief Display name and description are stored and announced.
///
void TestAddressSpace::displayNameAndDescriptionNotify()
{
    QUaBaseObject *object = objects()->addBaseObject(QStringLiteral("object"));
    QSignalSpy nameSpy(object, &QUaNode::displayNameChanged);
    QSignalSpy descriptionSpy(object, &QUaNode::descriptionChanged);

    object->setDisplayName(QStringLiteral("Shown"));
    object->setDescription(QStringLiteral("Described"));

    QCOMPARE(object->displayName().text(), QStringLiteral("Shown"));
    QCOMPARE(object->description().text(), QStringLiteral("Described"));
    QCOMPARE(nameSpy.count(), 1);
    QCOMPARE(descriptionSpy.count(), 1);
    QCOMPARE(object->browseName().name(), QStringLiteral("object"));
}

///
/// \brief Enum data types can only be assigned once the enum is registered.
///
void TestAddressSpace::enumDataTypeRequiresRegisteredEnum()
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(QStringLiteral("mode"));
    _server->registerEnum(QStringLiteral("Mode"), { { 0, { QUaLocalizedText(QStringLiteral("Off")), QUaLocalizedText() } },
                                                     { 1, { QUaLocalizedText(QStringLiteral("On")), QUaLocalizedText() } } });

    QVERIFY(!variable->setDataTypeEnum(QStringLiteral("Unknown")));
    QVERIFY(variable->setDataTypeEnum(QStringLiteral("Mode")));
    variable->setValue(1);

    QCOMPARE(variable->value().toInt(), 1);
    QCOMPARE(variable->dataTypeNodeId(), QStringLiteral("ns=0;i=%1").arg(UA_NS0ID_INT32));
}

///
/// \brief Q_PROPERTY children of a custom type exist on every instance with their defaults.
///
void TestAddressSpace::customTypeInstantiatesPropertyChildren()
{
    Pump *pump = objects()->addChild<Pump>(QStringLiteral("pump"));

    QVERIFY(pump);
    QVERIFY(pump->model());
    QVERIFY(pump->speed());
    QCOMPARE(pump->model()->value().toString(), QStringLiteral("P-100"));
    QCOMPARE(pump->speed()->dataType(), QMetaType::Double);
    QCOMPARE(pump->typeDefinitionBrowseName().name(), QStringLiteral("Pump"));
    QVERIFY(_server->isTypeNameRegistered(QStringLiteral("Pump")));
}

///
/// \brief typeInstances() lists live instances only.
///
void TestAddressSpace::customTypeTracksInstances()
{
    _server->registerType<Pump>(QUaNodeId(1, QStringLiteral("PumpType")));
    Pump *first = objects()->addChild<Pump>(QStringLiteral("first"));
    Pump *second = objects()->addChild<Pump>(QStringLiteral("second"));

    QCOMPARE(_server->typeInstances<Pump>().count(), 2);
    QCOMPARE(first->typeDefinitionNodeId(), QUaNodeId(1, QStringLiteral("PumpType")));

    delete first;
    QCOMPARE(_server->typeInstances<Pump>(), QList<Pump *>({ second }));
}

///
/// \brief instanceCreated() callbacks run, queued, for each new instance of the type.
///
void TestAddressSpace::instanceCreatedCallbackFires()
{
    QList<Pump *> created;
    _server->instanceCreated<Pump>([&created](Pump *pump) { created << pump; });

    Pump *pump = objects()->addChild<Pump>(QStringLiteral("pump"));

    QTRY_COMPARE(created, QList<Pump *>({ pump }));
}

void TestAddressSpace::instanceCreatedMemberCallbackFires()
{
    PumpRegistry registry;
    _server->instanceCreated<Pump>(&registry, &PumpRegistry::add);

    Pump *pump = objects()->addChild<Pump>(QStringLiteral("pump"));

    QTRY_COMPARE(registry.pumps, QList<Pump *>({ pump }));
}

///
/// \brief Custom non-hierarchical references are visible from both ends until removed.
///
void TestAddressSpace::referencesCanBeAddedAndRemoved()
{
    const QUaReferenceType feeds = { QStringLiteral("Feeds"), QStringLiteral("FedBy") };
    _server->registerReferenceType(feeds);
    QUaBaseObject *tank = objects()->addBaseObject(QStringLiteral("tank"));
    QUaBaseObject *pump = objects()->addBaseObject(QStringLiteral("pump"));
    QSignalSpy addedSpy(tank, &QUaNode::referenceAdded);

    tank->addReference(feeds, pump);

    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(tank->findReferences(feeds), QList<QUaNode *>({ pump }));
    QCOMPARE(pump->findReferences(feeds, false), QList<QUaNode *>({ tank }));

    tank->removeReference(feeds, pump);
    QVERIFY(tank->findReferences(feeds).isEmpty());
    QVERIFY(pump->findReferences(feeds, false).isEmpty());
}

///
/// \brief A clone has the same type and child values under its own browse name.
///
void TestAddressSpace::cloneCopiesTypeAndValues()
{
    Pump *pump = objects()->addChild<Pump>(QStringLiteral("pump"));
    pump->speed()->setValue(12.5);

    auto clone = qobject_cast<Pump *>(pump->cloneNode(objects(), QStringLiteral("clone")));

    QVERIFY(clone);
    QVERIFY(clone != pump);
    QCOMPARE(clone->browseName().name(), QStringLiteral("clone"));
    QCOMPARE(clone->speed()->value<double>(), 12.5);
    QCOMPARE(clone->model()->value().toString(), QStringLiteral("P-100"));
}

///
/// \brief Two siblings cannot share a browse name.
///
void TestAddressSpace::duplicateBrowseNameIsRejected()
{
#ifndef QT_NO_DEBUG
    QSKIP("The library asserts on this path in debug builds");
#else
    objects()->addBaseObject(QStringLiteral("twin"));

    QVERIFY(!objects()->addBaseObject(QStringLiteral("twin")));
#endif
}

///
/// \brief A NodeId that is already in use cannot be reused.
///
void TestAddressSpace::duplicateNodeIdIsRejected()
{
#ifndef QT_NO_DEBUG
    QSKIP("The library asserts on this path in debug builds");
#else
    const QUaNodeId nodeId(1, QStringLiteral("unique"));
    objects()->addBaseObject(QStringLiteral("first"), nodeId);

    QVERIFY(!objects()->addBaseObject(QStringLiteral("second"), nodeId));
#endif
}

QTEST_GUILESS_MAIN(TestAddressSpace)

#include "test_addressspace.moc"
