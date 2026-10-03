#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

class TestServicesIntegration : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void clientReadsServerValue();
    void requestsAreAnsweredPromptly();
    void clientWriteUpdatesValueAndNotifies();
    void readOnlyVariableRejectsWrite();
    void writeOfIncompatibleTypeIsRejected();
    void enumVariableAcceptsInt32();
    void byteArrayVariableIsByteString();
    void readCallbackAnswersClientReads();
    void userAccessLevelIsCheckedPerUser();
    void methodReturnsResult();
    void methodRejectsWrongArguments_data();
    void methodRejectsWrongArguments();
    void voidMethodRuns();
    void methodReceivesArgumentsInOrder();
    void voidMethodReceivesArgumentsInOrder();
    void functionPointerMethodRuns();
    void methodTakesAndReturnsLists();

private:
    QUaServer *m_server = nullptr;
    TestClient *m_client = nullptr;
    QList<QUaNode *> m_created;

    QUaFolderObject *objects() const { return m_server->objectsFolder(); }
    QUaBaseDataVariable *addVariable(const QString &name);
};

///
/// \brief Starts one server for the whole suite; every test adds its own nodes.
///
void TestServicesIntegration::initTestCase()
{
    m_server = new QUaServer;
    m_server->addUser(QStringLiteral("admin"), QStringLiteral("admin"));
    m_server->addUser(QStringLiteral("guest"), QStringLiteral("guest"));
    QVERIFY(TestServer::start(*m_server));
}

///
/// \brief Stops and deletes the suite server.
///
void TestServicesIntegration::cleanupTestCase()
{
    delete m_server;
    m_server = nullptr;
}

///
/// \brief Connects an anonymous client for each test.
///
void TestServicesIntegration::init()
{
    m_client = new TestClient;
    QCOMPARE(m_client->connect(TestServer::endpointUrl(*m_server)), UA_STATUSCODE_GOOD);
}

///
/// \brief Disconnects the client and removes the nodes the test created.
///
void TestServicesIntegration::cleanup()
{
    delete m_client;
    m_client = nullptr;
    qDeleteAll(m_created);
    m_created.clear();
}

///
/// \brief Adds a variable under the objects folder whose string NodeId equals \a name.
///
QUaBaseDataVariable *TestServicesIntegration::addVariable(const QString &name)
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(name, QUaNodeId(1, name));
    m_created << variable;
    return variable;
}

///
/// \brief A value set on the server is what a client reads.
///
void TestServicesIntegration::clientReadsServerValue()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("read"));
    variable->setValue(QStringLiteral("hello"));
    QVariant value;

    QCOMPARE(m_client->readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);

    QCOMPARE(value.toString(), QStringLiteral("hello"));
}

///
/// \brief Regression: every request waited for the 500 ms server iteration timeout of open62541 >= 1.4.
///
void TestServicesIntegration::requestsAreAnsweredPromptly()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("fast"));
    variable->setValue(1);
    QElapsedTimer elapsed;
    QVariant value;

    elapsed.start();
    for (int i = 0; i < 20; ++i)
    {
        QCOMPARE(m_client->readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);
    }

    QVERIFY2(elapsed.elapsed() < 2000, qPrintable(QStringLiteral("20 reads took %1 ms").arg(elapsed.elapsed())));
}

///
/// \brief A client write changes the value and is reported with networkChange set.
///
void TestServicesIntegration::clientWriteUpdatesValueAndNotifies()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("write"));
    variable->setWriteAccess(true);
    variable->setValue(1.0);
    QSignalSpy spy(variable, &QUaBaseVariable::valueChanged);

    QCOMPARE(m_client->writeValue(variable->nodeId(), 2.5), UA_STATUSCODE_GOOD);

    QCOMPARE(variable->value<double>(), 2.5);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toDouble(), 2.5);
    QCOMPARE(spy.first().at(1).toBool(), true);
}

///
/// \brief Variables are read-only unless write access is granted.
///
void TestServicesIntegration::readOnlyVariableRejectsWrite()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("readonly"));
    variable->setValue(5);

    const UA_StatusCode status = m_client->writeValue(variable->nodeId(), 6);

    QVERIFY(status != UA_STATUSCODE_GOOD);
    QCOMPARE(variable->value<int>(), 5);
}

///
/// \brief The data type of a variable is enforced on client writes.
///
void TestServicesIntegration::writeOfIncompatibleTypeIsRejected()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("typed"));
    variable->setWriteAccess(true);
    variable->setValue(5);

    const UA_StatusCode status = m_client->writeValue(variable->nodeId(), QStringLiteral("text"));

    QCOMPARE(status, UA_STATUSCODE_BADTYPEMISMATCH);
    QCOMPARE(variable->value<int>(), 5);
}

///
/// \brief Regression: writing an Int32 to a variable of a custom enum type returned BadTypeMismatch.
///
void TestServicesIntegration::enumVariableAcceptsInt32()
{
    if (!m_server->isEnumRegistered(QStringLiteral("Direction")))
    {
        m_server->registerEnum(QStringLiteral("Direction"),
                               { { 0, { QUaLocalizedText(QStringLiteral("Forward")), QUaLocalizedText() } },
                                 { 1, { QUaLocalizedText(QStringLiteral("Reverse")), QUaLocalizedText() } } });
    }
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("direction"));
    QVERIFY(variable->setDataTypeEnum(QStringLiteral("Direction")));
    variable->setWriteAccess(true);
    variable->setValue(0);
    QUaNodeId dataType;

    QCOMPARE(m_client->writeValue(variable->nodeId(), qint32(1)), UA_STATUSCODE_GOOD);

    QCOMPARE(variable->value().toInt(), 1);
    QCOMPARE(m_client->readValueDataType(variable->nodeId(), dataType), UA_STATUSCODE_GOOD);
    QCOMPARE(dataType, QUaNodeId(1, QStringLiteral("Direction")));
}

///
/// \brief Regression: QByteArray variables were exposed as ImagePNG instead of ByteString.
///
void TestServicesIntegration::byteArrayVariableIsByteString()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("bytes"));
    variable->setValue(QByteArray("\x01\x02", 2));
    QUaNodeId dataType;
    QVariant value;

    QCOMPARE(m_client->readValueDataType(variable->nodeId(), dataType), UA_STATUSCODE_GOOD);
    QCOMPARE(m_client->readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);

    QCOMPARE(dataType, QUaNodeId(0, quint32(UA_NS0ID_BYTESTRING)));
    QCOMPARE(value.toByteArray(), QByteArray("\x01\x02", 2));
}

///
/// \brief Client reads are answered by the read callback.
///
void TestServicesIntegration::readCallbackAnswersClientReads()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("computed"));
    variable->setValue(0);
    variable->setReadCallback([]() { return QVariant(77); });
    QVariant value;

    QCOMPARE(m_client->readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);

    QCOMPARE(value.toInt(), 77);
}

///
/// \brief The user access level callback restricts writes for some users only.
///
void TestServicesIntegration::userAccessLevelIsCheckedPerUser()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("guarded"));
    variable->setWriteAccess(true);
    variable->setValue(1);
    variable->setUserAccessLevelCallback([](const QString &userName) {
        QUaAccessLevel access;
        access.bits.bWrite = userName == QStringLiteral("admin");
        return access;
    });
    TestClient admin;
    TestClient guest;
    QCOMPARE(admin.connectUsername(TestServer::endpointUrl(*m_server), QStringLiteral("admin"), QStringLiteral("admin")),
             UA_STATUSCODE_GOOD);
    QCOMPARE(guest.connectUsername(TestServer::endpointUrl(*m_server), QStringLiteral("guest"), QStringLiteral("guest")),
             UA_STATUSCODE_GOOD);

    QVERIFY(guest.writeValue(variable->nodeId(), 2) != UA_STATUSCODE_GOOD);
    QCOMPARE(admin.writeValue(variable->nodeId(), 3), UA_STATUSCODE_GOOD);

    QCOMPARE(variable->value<int>(), 3);
}

///
/// \brief A method added with a callable returns the callable's result to the client.
///
void TestServicesIntegration::methodReturnsResult()
{
    QUaBaseObject *calculator = objects()->addBaseObject(QStringLiteral("calculator"));
    m_created << calculator;
    const QUaNodeId methodId(1, QStringLiteral("calculator.add"));
    calculator->addMethod(QStringLiteral("add"), [](int a, int b) { return a + b; }, methodId);
    QVariantList outputs;

    QCOMPARE(m_client->call(calculator->nodeId(), methodId, { 2, 3 }, outputs), UA_STATUSCODE_GOOD);

    QCOMPARE(outputs.count(), 1);
    QCOMPARE(outputs.first().toInt(), 5);
}

void TestServicesIntegration::methodRejectsWrongArguments_data()
{
    QTest::addColumn<QVariantList>("inputs");

    QTest::newRow("missing") << QVariantList({ 1 });
    QTest::newRow("too many") << QVariantList({ 1, 2, 3 });
    QTest::newRow("wrong type") << QVariantList({ 1, QStringLiteral("two") });
}

///
/// \brief Calls whose arguments do not match the signature fail without running the callable.
///
void TestServicesIntegration::methodRejectsWrongArguments()
{
    QFETCH(QVariantList, inputs);
    QUaBaseObject *calculator = objects()->addBaseObject(QStringLiteral("calculator"));
    m_created << calculator;
    const QUaNodeId methodId(1, QStringLiteral("calculator.multiply.%1").arg(QString::fromLatin1(QTest::currentDataTag())));
    int calls = 0;
    calculator->addMethod(QStringLiteral("multiply"), [&calls](int a, int b) { ++calls; return a * b; }, methodId);
    QVariantList outputs;

    const UA_StatusCode status = m_client->call(calculator->nodeId(), methodId, inputs, outputs);

    QVERIFY(status != UA_STATUSCODE_GOOD);
    QCOMPARE(calls, 0);
}

///
/// \brief A method without return value runs and returns no outputs.
///
void TestServicesIntegration::voidMethodRuns()
{
    QUaBaseObject *device = objects()->addBaseObject(QStringLiteral("device"));
    m_created << device;
    const QUaNodeId methodId(1, QStringLiteral("device.reset"));
    QString lastReason;
    device->addMethod(QStringLiteral("reset"), [&lastReason](QString reason) { lastReason = reason; }, methodId);
    QVariantList outputs;

    QCOMPARE(m_client->call(device->nodeId(), methodId, { QStringLiteral("maintenance") }, outputs), UA_STATUSCODE_GOOD);

    QVERIFY(outputs.isEmpty());
    QCOMPARE(lastReason, QStringLiteral("maintenance"));
}

///
/// \brief Regression: each client input reaches the parameter at the same position, whatever the argument evaluation order.
///
void TestServicesIntegration::methodReceivesArgumentsInOrder()
{
    QUaBaseObject *device = objects()->addBaseObject(QStringLiteral("device"));
    m_created << device;
    const QUaNodeId methodId(1, QStringLiteral("device.describe"));
    device->addMethod(QStringLiteral("describe"), [](QString name, int count, double scale) {
        return QStringLiteral("%1:%2:%3").arg(name).arg(count).arg(scale);
    }, methodId);
    QVariantList outputs;

    QCOMPARE(m_client->call(device->nodeId(), methodId, { QStringLiteral("pump"), 3, 0.5 }, outputs), UA_STATUSCODE_GOOD);

    QCOMPARE(outputs.count(), 1);
    QCOMPARE(outputs.first().toString(), QStringLiteral("pump:3:0.5"));
}

void TestServicesIntegration::voidMethodReceivesArgumentsInOrder()
{
    QUaBaseObject *device = objects()->addBaseObject(QStringLiteral("device"));
    m_created << device;
    const QUaNodeId methodId(1, QStringLiteral("device.configure"));
    QString lastName;
    int lastCount = 0;
    device->addMethod(QStringLiteral("configure"), [&lastName, &lastCount](QString name, int count) {
        lastName = name;
        lastCount = count;
    }, methodId);
    QVariantList outputs;

    QCOMPARE(m_client->call(device->nodeId(), methodId, { QStringLiteral("valve"), 7 }, outputs), UA_STATUSCODE_GOOD);

    QVERIFY(outputs.isEmpty());
    QCOMPARE(lastName, QStringLiteral("valve"));
    QCOMPARE(lastCount, 7);
}

namespace {

int subtract(int minuend, int subtrahend)
{
    return minuend - subtrahend;
}

} // namespace

void TestServicesIntegration::functionPointerMethodRuns()
{
    QUaBaseObject *calculator = objects()->addBaseObject(QStringLiteral("calculator"));
    m_created << calculator;
    const QUaNodeId methodId(1, QStringLiteral("calculator.subtract"));
    calculator->addMethod(QStringLiteral("subtract"), &subtract, methodId);
    QVariantList outputs;

    QCOMPARE(m_client->call(calculator->nodeId(), methodId, { 10, 4 }, outputs), UA_STATUSCODE_GOOD);

    QCOMPARE(outputs.count(), 1);
    QCOMPARE(outputs.first().toInt(), 6);
}

///
/// \brief List parameters and results travel as one-dimensional arrays.
///
void TestServicesIntegration::methodTakesAndReturnsLists()
{
    QUaBaseObject *calculator = objects()->addBaseObject(QStringLiteral("calculator"));
    m_created << calculator;
    const QUaNodeId methodId(1, QStringLiteral("calculator.scale"));
    calculator->addMethod(QStringLiteral("scale"), [](QList<int> values, int factor) {
        QList<int> scaled;
        for (int value : values)
        {
            scaled << value * factor;
        }
        return scaled;
    }, methodId);
    QVariantList outputs;

    QCOMPARE(m_client->call(calculator->nodeId(), methodId, { QVariant::fromValue(QList<int>{ 1, 2, 3 }), 10 }, outputs),
             UA_STATUSCODE_GOOD);

    QCOMPARE(outputs.count(), 1);
    QCOMPARE(outputs.first().value<QList<int>>(), QList<int>({ 10, 20, 30 }));
}

QTEST_GUILESS_MAIN(TestServicesIntegration)

#include "test_services_integration.moc"
