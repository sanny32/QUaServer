#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

namespace {

const QString kMethodsUri = QStringLiteral("http://quaserver.test/methods/");

QString nodeSetPath(const QString &fileName)
{
    return QStringLiteral(QUASERVER_TEST_NODESET_DIR "/") + fileName;
}

} // namespace

class TestMethodsIntegration : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void readsArgumentsOfLoadedMethods();
    void handlerAnswersClientCalls();
    void handlerTellsObjectsSharingTheMethodApart();
    void missingOutputsGetDefaultValues();
    void handlerStatusReachesTheClient();
    void unconvertibleOutputFailsTheCall();
    void emptyHandlerRestoresNotImplemented();
    void refusesNodesWithoutHandlerSupport();

private:
    QUaNodeId methodsNodeId(quint32 numericId) const;
    QUaNodeId doseId() const;
    QUaNodeId station1Id() const;

    QUaServer *m_server = nullptr;
};

///
/// \brief Every test gets a fresh server with the methods NodeSet loaded.
///
void TestMethodsIntegration::init()
{
    m_server = new QUaServer;
    const QUaNodeSetResult result = m_server->loadNodeSet(nodeSetPath(QStringLiteral("methods.NodeSet2.xml")));
    QVERIFY2(result.isOk(), qPrintable(result.errorString));
    QVERIFY2(result.warnings.isEmpty(), qPrintable(result.warnings.join(QLatin1Char('\n'))));
}

void TestMethodsIntegration::cleanup()
{
    delete m_server;
    m_server = nullptr;
}

QUaNodeId TestMethodsIntegration::methodsNodeId(quint32 numericId) const
{
    return QUaNodeId(static_cast<quint16>(m_server->namespaces().indexOf(kMethodsUri)), numericId);
}

QUaNodeId TestMethodsIntegration::doseId() const
{
    return this->methodsNodeId(5001);
}

QUaNodeId TestMethodsIntegration::station1Id() const
{
    return this->methodsNodeId(5000);
}

void TestMethodsIntegration::readsArgumentsOfLoadedMethods()
{
    const QList<QUaMethodArgument> inputs = m_server->inputArguments(this->doseId());
    QCOMPARE(inputs.count(), 1);
    QCOMPARE(inputs.at(0).name, QStringLiteral("Volume"));
    QCOMPARE(inputs.at(0).dataType, QUaNodeId(0, UA_NS0ID_DOUBLE));
    QCOMPARE(inputs.at(0).valueRank, -1);
    QVERIFY(inputs.at(0).arrayDimensions.isEmpty());
    QCOMPARE(inputs.at(0).description.text(), QStringLiteral("Volume to dispense, l"));

    const QList<QUaMethodArgument> outputs = m_server->outputArguments(this->doseId());
    QCOMPARE(outputs.count(), 4);
    QCOMPARE(outputs.at(0).dataType, QUaNodeId(0, UA_NS0ID_DURATION));
    QCOMPARE(outputs.at(2).dataType, this->methodsNodeId(3001));
    QCOMPARE(outputs.at(3).name, QStringLiteral("Counts"));
    QCOMPARE(outputs.at(3).valueRank, 1);
    QCOMPARE(outputs.at(3).arrayDimensions, QVector<quint32>{ 0 });

    QVERIFY(m_server->inputArguments(this->methodsNodeId(5010)).isEmpty());
    QVERIFY(m_server->outputArguments(this->station1Id()).isEmpty());
}

///
/// \brief Outputs of other Qt types are converted to the data types of the arguments, enumerations to Int32.
///
void TestMethodsIntegration::handlerAnswersClientCalls()
{
    QVariantList receivedInputs;
    QVERIFY(m_server->setMethodHandler(this->doseId(),
        [&receivedInputs](const QUaNodeId &, const QVariantList &inputs, QVariantList &outputs) {
            receivedInputs = inputs;
            outputs = { 3, QStringLiteral("done"), 1, QVariantList{ 4, 5 } };
            return UA_STATUSCODE_GOOD;
        }));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    QCOMPARE(client.call(this->station1Id(), this->doseId(), { 2.5 }, outputs), UA_STATUSCODE_GOOD);

    QCOMPARE(receivedInputs, QVariantList{ 2.5 });
    QCOMPARE(outputs.count(), 4);
    QCOMPARE(outputs.at(0), QVariant(3.0));
    QCOMPARE(outputs.at(1), QVariant(QStringLiteral("done")));
    QCOMPARE(outputs.at(2), QVariant(qint32(1)));
    QCOMPARE(outputs.at(3).toList(), (QVariantList{ quint32(4), quint32(5) }));
}

void TestMethodsIntegration::handlerTellsObjectsSharingTheMethodApart()
{
    QList<QUaNodeId> objects;
    QVERIFY(m_server->setMethodHandler(this->doseId(),
        [&objects](const QUaNodeId &objectId, const QVariantList &, QVariantList &) {
            objects.append(objectId);
            return UA_STATUSCODE_GOOD;
        }));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    QCOMPARE(client.call(this->station1Id(), this->doseId(), { 1.0 }, outputs), UA_STATUSCODE_GOOD);
    QCOMPARE(client.call(this->methodsNodeId(5100), this->doseId(), { 1.0 }, outputs), UA_STATUSCODE_GOOD);

    QCOMPARE(objects, (QList<QUaNodeId>{ this->station1Id(), this->methodsNodeId(5100) }));
}

void TestMethodsIntegration::missingOutputsGetDefaultValues()
{
    QVERIFY(m_server->setMethodHandler(this->doseId(),
        [](const QUaNodeId &, const QVariantList &, QVariantList &outputs) {
            outputs = { 7.5 };
            return UA_STATUSCODE_GOOD;
        }));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    QCOMPARE(client.call(this->station1Id(), this->doseId(), { 1.0 }, outputs), UA_STATUSCODE_GOOD);

    QCOMPARE(outputs.count(), 4);
    QCOMPARE(outputs.at(0), QVariant(7.5));
    QCOMPARE(outputs.at(1).toString(), QString());
    QCOMPARE(outputs.at(2), QVariant(qint32(0)));
    QVERIFY(outputs.at(3).toList().isEmpty());
}

void TestMethodsIntegration::handlerStatusReachesTheClient()
{
    QVERIFY(m_server->setMethodHandler(this->methodsNodeId(5010),
        [](const QUaNodeId &, const QVariantList &, QVariantList &) {
            return UA_STATUSCODE_BADINVALIDSTATE;
        }));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    QCOMPARE(client.call(this->station1Id(), this->methodsNodeId(5010), {}, outputs), UA_STATUSCODE_BADINVALIDSTATE);
}

void TestMethodsIntegration::unconvertibleOutputFailsTheCall()
{
    QVERIFY(m_server->setMethodHandler(this->doseId(),
        [](const QUaNodeId &, const QVariantList &, QVariantList &outputs) {
            outputs = { QStringLiteral("soon") };
            return UA_STATUSCODE_GOOD;
        }));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    QCOMPARE(client.call(this->station1Id(), this->doseId(), { 1.0 }, outputs), UA_STATUSCODE_BADINTERNALERROR);
}

void TestMethodsIntegration::emptyHandlerRestoresNotImplemented()
{
    QVERIFY(m_server->setMethodHandler(this->doseId(),
        [](const QUaNodeId &, const QVariantList &, QVariantList &) { return UA_STATUSCODE_GOOD; }));
    QVERIFY(m_server->setMethodHandler(this->doseId(), QUaMethodHandler()));
    QVERIFY(TestServer::start(*m_server));
    TestClient client;
    QCOMPARE(client.connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
    QVariantList outputs;

    QCOMPARE(client.call(this->station1Id(), this->doseId(), { 1.0 }, outputs), UA_STATUSCODE_BADNOTIMPLEMENTED);
}

///
/// \brief Handlers never replace C++ methods or the methods of namespace 0, and need a method node.
///
void TestMethodsIntegration::refusesNodesWithoutHandlerSupport()
{
    const QUaMethodHandler handler = [](const QUaNodeId &, const QVariantList &, QVariantList &) {
        return UA_STATUSCODE_GOOD;
    };
    const QUaNodeId cppMethodId(1, QStringLiteral("Ping"));
    m_server->objectsFolder()->addMethod(QUaQualifiedName(1, QStringLiteral("Ping")), []() { return 1; }, cppMethodId);

    QVERIFY(!m_server->setMethodHandler(cppMethodId, handler));
    QVERIFY(!m_server->setMethodHandler(QUaNodeId(0, UA_NS0ID_SERVER_GETMONITOREDITEMS), handler));
    QVERIFY(!m_server->setMethodHandler(this->station1Id(), handler));
    QVERIFY(!m_server->setMethodHandler(this->methodsNodeId(9999), handler));
}

QTEST_GUILESS_MAIN(TestMethodsIntegration)

#include "test_methods_integration.moc"
