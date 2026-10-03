#include <algorithm>

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

namespace {

///
/// \brief Accepts percentages only.
///
QUaStatusCode validatePercentage(const QVariant &value, const QUaSession *session)
{
    Q_UNUSED(session);
    const int percentage = value.toInt();
    return percentage >= 0 && percentage <= 100 ? QUaStatusCode(QUaStatus::Good)
                                                : QUaStatusCode(UA_STATUSCODE_BADOUTOFRANGE);
}

} // namespace

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
    void writeValidatorAcceptsOrRejectsClientWrites_data();
    void writeValidatorAcceptsOrRejectsClientWrites();
    void writeValidatorReceivesWritingSession();
    void writeValidatorIgnoresLocalWrites();
    void writeValidatorChecksWholeValueOfRangeWrite();
    void writeValidatorKeepsReadCallbackAndSubscriptions();
    void removingWriteValidatorKeepsValue();
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
    QUaServer *_server = nullptr;
    TestClient *_client = nullptr;
    QList<QUaNode *> _created;

    QUaFolderObject *objects() const { return _server->objectsFolder(); }
    QUaBaseDataVariable *addVariable(const QString &name);
};

///
/// \brief Starts one server for the whole suite; every test adds its own nodes.
///
void TestServicesIntegration::initTestCase()
{
    _server = new QUaServer;
    _server->addUser(QStringLiteral("admin"), QStringLiteral("admin"));
    _server->addUser(QStringLiteral("guest"), QStringLiteral("guest"));
    QVERIFY(TestServer::start(*_server));
}

///
/// \brief Stops and deletes the suite server.
///
void TestServicesIntegration::cleanupTestCase()
{
    delete _server;
    _server = nullptr;
}

///
/// \brief Connects an anonymous client for each test.
///
void TestServicesIntegration::init()
{
    _client = new TestClient;
    QCOMPARE(_client->connect(TestServer::endpointUrl(*_server)), UA_STATUSCODE_GOOD);
}

///
/// \brief Disconnects the client and removes the nodes the test created.
///
void TestServicesIntegration::cleanup()
{
    delete _client;
    _client = nullptr;
    qDeleteAll(_created);
    _created.clear();
}

///
/// \brief Adds a variable under the objects folder whose string NodeId equals \a name.
///
QUaBaseDataVariable *TestServicesIntegration::addVariable(const QString &name)
{
    QUaBaseDataVariable *variable = objects()->addBaseDataVariable(name, QUaNodeId(1, name));
    _created << variable;
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

    QCOMPARE(_client->readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);

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
        QCOMPARE(_client->readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);
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

    QCOMPARE(_client->writeValue(variable->nodeId(), 2.5), UA_STATUSCODE_GOOD);

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

    const UA_StatusCode status = _client->writeValue(variable->nodeId(), 6);

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

    const UA_StatusCode status = _client->writeValue(variable->nodeId(), QStringLiteral("text"));

    QCOMPARE(status, UA_STATUSCODE_BADTYPEMISMATCH);
    QCOMPARE(variable->value<int>(), 5);
}

///
/// \brief Regression: writing an Int32 to a variable of a custom enum type returned BadTypeMismatch.
///
void TestServicesIntegration::enumVariableAcceptsInt32()
{
    if (!_server->isEnumRegistered(QStringLiteral("Direction")))
    {
        _server->registerEnum(QStringLiteral("Direction"),
                               { { 0, { QUaLocalizedText(QStringLiteral("Forward")), QUaLocalizedText() } },
                                 { 1, { QUaLocalizedText(QStringLiteral("Reverse")), QUaLocalizedText() } } });
    }
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("direction"));
    QVERIFY(variable->setDataTypeEnum(QStringLiteral("Direction")));
    variable->setWriteAccess(true);
    variable->setValue(0);
    QUaNodeId dataType;

    QCOMPARE(_client->writeValue(variable->nodeId(), qint32(1)), UA_STATUSCODE_GOOD);

    QCOMPARE(variable->value().toInt(), 1);
    QCOMPARE(_client->readValueDataType(variable->nodeId(), dataType), UA_STATUSCODE_GOOD);
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

    QCOMPARE(_client->readValueDataType(variable->nodeId(), dataType), UA_STATUSCODE_GOOD);
    QCOMPARE(_client->readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);

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

    QCOMPARE(_client->readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);

    QCOMPARE(value.toInt(), 77);
}

void TestServicesIntegration::writeValidatorAcceptsOrRejectsClientWrites_data()
{
    QTest::addColumn<int>("written");
    QTest::addColumn<quint32>("status");
    QTest::addColumn<int>("stored");

    QTest::newRow("valid value") << 80 << quint32(UA_STATUSCODE_GOOD) << 80;
    QTest::newRow("invalid value") << 150 << quint32(UA_STATUSCODE_BADOUTOFRANGE) << 50;
}

void TestServicesIntegration::writeValidatorAcceptsOrRejectsClientWrites()
{
    QFETCH(int, written);
    QFETCH(quint32, status);
    QFETCH(int, stored);
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("percentage"));
    variable->setWriteAccess(true);
    variable->setValue(50);
    variable->setWriteValidator(&validatePercentage);
    QSignalSpy spy(variable, &QUaBaseVariable::valueChanged);
    QVariant read;

    QCOMPARE(_client->writeValue(variable->nodeId(), written), status);

    QCOMPARE(variable->value<int>(), stored);
    QCOMPARE(_client->readValue(variable->nodeId(), read), UA_STATUSCODE_GOOD);
    QCOMPARE(read.toInt(), stored);
    QCOMPARE(spy.count(), status == UA_STATUSCODE_GOOD ? 1 : 0);
    if (!spy.isEmpty())
    {
        QCOMPARE(spy.first().at(1).toBool(), true);
    }
}

void TestServicesIntegration::writeValidatorReceivesWritingSession()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("owned"));
    variable->setWriteAccess(true);
    variable->setValue(1);
    QString writer;
    variable->setWriteValidator([&writer](const QVariant &, const QUaSession *session) {
        writer = session ? session->userName() : QString();
        return QUaStatusCode(writer == QStringLiteral("admin") ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADUSERACCESSDENIED);
    });
    TestClient admin;
    QCOMPARE(admin.connectUsername(TestServer::endpointUrl(*_server), QStringLiteral("admin"), QStringLiteral("admin")),
             UA_STATUSCODE_GOOD);

    QCOMPARE(_client->writeValue(variable->nodeId(), 2), UA_STATUSCODE_BADUSERACCESSDENIED);
    QCOMPARE(admin.writeValue(variable->nodeId(), 3), UA_STATUSCODE_GOOD);

    QCOMPARE(writer, QStringLiteral("admin"));
    QCOMPARE(variable->value<int>(), 3);
}

void TestServicesIntegration::writeValidatorIgnoresLocalWrites()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("local"));
    variable->setWriteAccess(true);
    variable->setValue(50);
    int validations = 0;
    variable->setWriteValidator([&validations](const QVariant &value, const QUaSession *session) {
        ++validations;
        return validatePercentage(value, session);
    });
    QSignalSpy spy(variable, &QUaBaseVariable::valueChanged);

    variable->setValue(500);
    variable->setStatusCode(QUaStatus::UncertainLastUsableValue);

    QCOMPARE(validations, 0);
    QCOMPARE(variable->value<int>(), 500);
    QVERIFY(variable->statusCode() == QUaStatus::UncertainLastUsableValue);
    QVERIFY(!spy.isEmpty());
    QCOMPARE(spy.first().at(1).toBool(), false);
}

///
/// \brief A partial write is validated as the array it produces, and a rejected one leaves the array intact.
///
void TestServicesIntegration::writeValidatorChecksWholeValueOfRangeWrite()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("levels"));
    variable->setWriteAccess(true);
    variable->setValue(QVariant::fromValue(QList<int>{ 10, 20, 30 }));
    QVariant seen;
    variable->setWriteValidator([&seen](const QVariant &value, const QUaSession *) {
        seen = value;
        const QVariantList levels = value.toList();
        const bool valid = std::all_of(levels.cbegin(), levels.cend(), [](const QVariant &level) { return level.toInt() >= 0; });
        return QUaStatusCode(valid ? UA_STATUSCODE_GOOD : UA_STATUSCODE_BADOUTOFRANGE);
    });

    QCOMPARE(_client->writeValueRange(variable->nodeId(), QStringLiteral("1"), QVariant::fromValue(QList<int>{ -5 })),
             UA_STATUSCODE_BADOUTOFRANGE);
    QCOMPARE(variable->value().toList(), (QVariantList{ 10, 20, 30 }));
    QCOMPARE(_client->writeValueRange(variable->nodeId(), QStringLiteral("1"), QVariant::fromValue(QList<int>{ 25 })),
             UA_STATUSCODE_GOOD);

    QCOMPARE(seen.toList(), (QVariantList{ 10, 25, 30 }));
    QCOMPARE(variable->value().toList(), (QVariantList{ 10, 25, 30 }));
}

void TestServicesIntegration::writeValidatorKeepsReadCallbackAndSubscriptions()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("computedValidated"));
    variable->setWriteAccess(true);
    variable->setValue(0);
    variable->setWriteValidator(&validatePercentage);
    variable->setReadCallback([]() { return QVariant(42); });
    QVariant value;

    QCOMPARE(_client->readValue(variable->nodeId(), value), UA_STATUSCODE_GOOD);
    QCOMPARE(_client->monitorValue(variable->nodeId()), UA_STATUSCODE_GOOD);

    QCOMPARE(value.toInt(), 42);
}

void TestServicesIntegration::removingWriteValidatorKeepsValue()
{
    QUaBaseDataVariable *variable = addVariable(QStringLiteral("unvalidated"));
    variable->setWriteAccess(true);
    variable->setWriteValidator(&validatePercentage);
    variable->setValue(42);
    QSignalSpy spy(variable, &QUaBaseVariable::valueChanged);

    variable->setWriteValidator();

    QCOMPARE(variable->value<int>(), 42);
    QCOMPARE(_client->writeValue(variable->nodeId(), 150), UA_STATUSCODE_GOOD);
    QCOMPARE(variable->value<int>(), 150);
    QTRY_COMPARE(spy.count(), 1);
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
    QCOMPARE(admin.connectUsername(TestServer::endpointUrl(*_server), QStringLiteral("admin"), QStringLiteral("admin")),
             UA_STATUSCODE_GOOD);
    QCOMPARE(guest.connectUsername(TestServer::endpointUrl(*_server), QStringLiteral("guest"), QStringLiteral("guest")),
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
    _created << calculator;
    const QUaNodeId methodId(1, QStringLiteral("calculator.add"));
    calculator->addMethod(QStringLiteral("add"), [](int a, int b) { return a + b; }, methodId);
    QVariantList outputs;

    QCOMPARE(_client->call(calculator->nodeId(), methodId, { 2, 3 }, outputs), UA_STATUSCODE_GOOD);

    QCOMPARE(outputs.count(), 1);
    QCOMPARE(outputs.first().toInt(), 5);
}

void TestServicesIntegration::methodRejectsWrongArguments_data()
{
    QTest::addColumn<QVariantList>("inputs");

    QTest::newRow("missing") << QVariantList{ 1 };
    QTest::newRow("too many") << QVariantList{ 1, 2, 3 };
    QTest::newRow("wrong type") << QVariantList{ 1, QStringLiteral("two") };
}

///
/// \brief Calls whose arguments do not match the signature fail without running the callable.
///
void TestServicesIntegration::methodRejectsWrongArguments()
{
    QFETCH(QVariantList, inputs);
    QUaBaseObject *calculator = objects()->addBaseObject(QStringLiteral("calculator"));
    _created << calculator;
    const QUaNodeId methodId(1, QStringLiteral("calculator.multiply.%1").arg(QString::fromLatin1(QTest::currentDataTag())));
    int calls = 0;
    calculator->addMethod(QStringLiteral("multiply"), [&calls](int a, int b) { ++calls; return a * b; }, methodId);
    QVariantList outputs;

    const UA_StatusCode status = _client->call(calculator->nodeId(), methodId, inputs, outputs);

    QVERIFY(status != UA_STATUSCODE_GOOD);
    QCOMPARE(calls, 0);
}

///
/// \brief A method without return value runs and returns no outputs.
///
void TestServicesIntegration::voidMethodRuns()
{
    QUaBaseObject *device = objects()->addBaseObject(QStringLiteral("device"));
    _created << device;
    const QUaNodeId methodId(1, QStringLiteral("device.reset"));
    QString lastReason;
    device->addMethod(QStringLiteral("reset"), [&lastReason](QString reason) { lastReason = reason; }, methodId);
    QVariantList outputs;

    QCOMPARE(_client->call(device->nodeId(), methodId, { QStringLiteral("maintenance") }, outputs), UA_STATUSCODE_GOOD);

    QVERIFY(outputs.isEmpty());
    QCOMPARE(lastReason, QStringLiteral("maintenance"));
}

///
/// \brief Regression: each client input reaches the parameter at the same position, whatever the argument evaluation order.
///
void TestServicesIntegration::methodReceivesArgumentsInOrder()
{
    QUaBaseObject *device = objects()->addBaseObject(QStringLiteral("device"));
    _created << device;
    const QUaNodeId methodId(1, QStringLiteral("device.describe"));
    device->addMethod(QStringLiteral("describe"), [](QString name, int count, double scale) {
        return QStringLiteral("%1:%2:%3").arg(name).arg(count).arg(scale);
    }, methodId);
    QVariantList outputs;

    QCOMPARE(_client->call(device->nodeId(), methodId, { QStringLiteral("pump"), 3, 0.5 }, outputs), UA_STATUSCODE_GOOD);

    QCOMPARE(outputs.count(), 1);
    QCOMPARE(outputs.first().toString(), QStringLiteral("pump:3:0.5"));
}

void TestServicesIntegration::voidMethodReceivesArgumentsInOrder()
{
    QUaBaseObject *device = objects()->addBaseObject(QStringLiteral("device"));
    _created << device;
    const QUaNodeId methodId(1, QStringLiteral("device.configure"));
    QString lastName;
    int lastCount = 0;
    device->addMethod(QStringLiteral("configure"), [&lastName, &lastCount](QString name, int count) {
        lastName = name;
        lastCount = count;
    }, methodId);
    QVariantList outputs;

    QCOMPARE(_client->call(device->nodeId(), methodId, { QStringLiteral("valve"), 7 }, outputs), UA_STATUSCODE_GOOD);

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
    _created << calculator;
    const QUaNodeId methodId(1, QStringLiteral("calculator.subtract"));
    calculator->addMethod(QStringLiteral("subtract"), &subtract, methodId);
    QVariantList outputs;

    QCOMPARE(_client->call(calculator->nodeId(), methodId, { 10, 4 }, outputs), UA_STATUSCODE_GOOD);

    QCOMPARE(outputs.count(), 1);
    QCOMPARE(outputs.first().toInt(), 6);
}

///
/// \brief List parameters and results travel as one-dimensional arrays.
///
void TestServicesIntegration::methodTakesAndReturnsLists()
{
    QUaBaseObject *calculator = objects()->addBaseObject(QStringLiteral("calculator"));
    _created << calculator;
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

    QCOMPARE(_client->call(calculator->nodeId(), methodId, { QVariant::fromValue(QList<int>{ 1, 2, 3 }), 10 }, outputs),
             UA_STATUSCODE_GOOD);

    QCOMPARE(outputs.count(), 1);
    QCOMPARE(outputs.first().value<QList<int>>(), QList<int>({ 10, 20, 30 }));
}

QTEST_GUILESS_MAIN(TestServicesIntegration)

#include "test_services_integration.moc"
