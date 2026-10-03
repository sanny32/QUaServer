#include <QPointer>
#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

namespace {

const QUaNodeId kObjectsFolder(0, quint32(UA_NS0ID_OBJECTSFOLDER));
const QUaNodeId kBaseObjectType(0, quint32(UA_NS0ID_BASEOBJECTTYPE));
const QUaNodeId kGeneratesEvent(0, quint32(UA_NS0ID_GENERATESEVENT));

} // namespace

class TestNodeManagementIntegration : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void clientsManageNodesWithoutCallbacks();
    void addNodeCallbackDecides_data();
    void addNodeCallbackDecides();
    void deleteNodeCallbackDecides_data();
    void deleteNodeCallbackDecides();
    void referenceCallbacksDecide_data();
    void referenceCallbacksDecide();
    void callbacksReceiveClientSession();

private:
    QUaServer *m_server = nullptr;
    TestClient *m_client = nullptr;
};

///
/// \brief Every test gets a fresh, running server and an anonymous client.
///
void TestNodeManagementIntegration::init()
{
    m_server = new QUaServer;
    m_server->addUser(QStringLiteral("admin"), QStringLiteral("admin"));
    QVERIFY(TestServer::start(*m_server));
    m_client = new TestClient;
    QCOMPARE(m_client->connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
}

void TestNodeManagementIntegration::cleanup()
{
    delete m_client;
    m_client = nullptr;
    delete m_server;
    m_server = nullptr;
}

///
/// \brief Without callbacks the previous behaviour stays: clients add and delete nodes, mirrored in the C++ tree.
///
void TestNodeManagementIntegration::clientsManageNodesWithoutCallbacks()
{
    QCOMPARE(m_client->addObject(kObjectsFolder, QStringLiteral("added")), UA_STATUSCODE_GOOD);
    QPointer<QUaNode> added = m_server->nodeById(QUaNodeId(1, QStringLiteral("added")));
    QVERIFY(added);

    QCOMPARE(m_client->deleteNode(added->nodeId()), UA_STATUSCODE_GOOD);

    QTRY_VERIFY(added.isNull());
    QVERIFY(!m_server->nodeById(QUaNodeId(1, QStringLiteral("added"))));
}

void TestNodeManagementIntegration::addNodeCallbackDecides_data()
{
    QTest::addColumn<bool>("allowed");

    QTest::newRow("allowed") << true;
    QTest::newRow("denied") << false;
}

void TestNodeManagementIntegration::addNodeCallbackDecides()
{
    QFETCH(bool, allowed);
    QUaNodeId parent;
    QUaQualifiedName browseName;
    QUaNodeId typeDefinition;
    m_server->setAddNodeCallback([&](const QUaSession *, const QUaNodeId &parentNodeId,
                                     const QUaQualifiedName &name, const QUaNodeId &typeDefinitionNodeId) {
        parent = parentNodeId;
        browseName = name;
        typeDefinition = typeDefinitionNodeId;
        return allowed;
    });

    const UA_StatusCode status = m_client->addObject(kObjectsFolder, QStringLiteral("requested"));

    QCOMPARE(status, allowed ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADUSERACCESSDENIED);
    QCOMPARE(parent, kObjectsFolder);
    QCOMPARE(browseName, QUaQualifiedName(1, QStringLiteral("requested")));
    QCOMPARE(typeDefinition, kBaseObjectType);
    QCOMPARE(m_server->nodeById(QUaNodeId(1, QStringLiteral("requested"))) != nullptr, allowed);
}

void TestNodeManagementIntegration::deleteNodeCallbackDecides_data()
{
    QTest::addColumn<bool>("allowed");

    QTest::newRow("allowed") << true;
    QTest::newRow("denied") << false;
}

void TestNodeManagementIntegration::deleteNodeCallbackDecides()
{
    QFETCH(bool, allowed);
    QPointer<QUaBaseObject> object = m_server->objectsFolder()->addBaseObject(QStringLiteral("doomed"));
    const QUaNodeId objectId = object->nodeId();
    QUaNodeId requested;
    m_server->setDeleteNodeCallback([&](const QUaSession *, const QUaNodeId &nodeId) {
        requested = nodeId;
        return allowed;
    });

    const UA_StatusCode status = m_client->deleteNode(objectId);

    QCOMPARE(status, allowed ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADUSERACCESSDENIED);
    QCOMPARE(requested, objectId);
    if (allowed)
    {
        QTRY_VERIFY(object.isNull());
    }
    else
    {
        QTest::qWait(100);
        QVERIFY(!object.isNull());
    }
}

void TestNodeManagementIntegration::referenceCallbacksDecide_data()
{
    QTest::addColumn<bool>("allowed");

    QTest::newRow("allowed") << true;
    QTest::newRow("denied") << false;
}

void TestNodeManagementIntegration::referenceCallbacksDecide()
{
    QFETCH(bool, allowed);
    QUaBaseObject *source = m_server->objectsFolder()->addBaseObject(QStringLiteral("source"));
    QUaBaseObject *target = m_server->objectsFolder()->addBaseObject(QStringLiteral("target"));
    QList<QUaNodeId> added;
    QList<QUaNodeId> deleted;
    bool forward = false;
    m_server->setAddReferenceCallback([&](const QUaSession *, const QUaNodeId &sourceNodeId, const QUaNodeId &referenceTypeId,
                                          const QUaNodeId &targetNodeId, bool isForward) {
        added = { sourceNodeId, referenceTypeId, targetNodeId };
        forward = isForward;
        return allowed;
    });
    m_server->setDeleteReferenceCallback([&](const QUaSession *, const QUaNodeId &sourceNodeId, const QUaNodeId &referenceTypeId,
                                             const QUaNodeId &targetNodeId, bool) {
        deleted = { sourceNodeId, referenceTypeId, targetNodeId };
        return allowed;
    });
    const UA_StatusCode expected = allowed ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADUSERACCESSDENIED;
    const QList<QUaNodeId> reference{ source->nodeId(), kGeneratesEvent, target->nodeId() };

    QCOMPARE(m_client->addReference(source->nodeId(), kGeneratesEvent, target->nodeId()), expected);
    QCOMPARE(m_client->deleteReference(source->nodeId(), kGeneratesEvent, target->nodeId()), expected);

    QCOMPARE(added, reference);
    QVERIFY(forward);
    QCOMPARE(deleted, reference);
}

///
/// \brief The callbacks get the requesting session, so permissions can depend on the user.
///
void TestNodeManagementIntegration::callbacksReceiveClientSession()
{
    m_server->setAddNodeCallback([](const QUaSession *session, const QUaNodeId &, const QUaQualifiedName &, const QUaNodeId &) {
        return session && session->userName() == QStringLiteral("admin");
    });
    TestClient admin;
    QCOMPARE(admin.connectUsername(TestServer::endpointUrl(*m_server), QStringLiteral("admin"), QStringLiteral("admin")),
             UA_STATUSCODE_GOOD);

    QCOMPARE(m_client->addObject(kObjectsFolder, QStringLiteral("byAnonymous")), UA_STATUSCODE_BADUSERACCESSDENIED);
    QCOMPARE(admin.addObject(kObjectsFolder, QStringLiteral("byAdmin")), UA_STATUSCODE_GOOD);
}

QTEST_GUILESS_MAIN(TestNodeManagementIntegration)

#include "test_nodemanagement_integration.moc"
