#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

#include <QUaServer>

#include "testclient.h"
#include "testserver.h"

namespace {

///
/// \brief Returns an IPv4 address of this host that is not loopback, or a null address when there is none.
///
QHostAddress nonLoopbackAddress()
{
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses)
    {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
        {
            return address;
        }
    }
    return QHostAddress();
}

///
/// \brief Tells whether an anonymous OPC UA session can be opened on \a address and \a port.
///
bool acceptsSession(const QHostAddress &address, quint16 port)
{
    TestClient client;
    return client.connect(QStringLiteral("opc.tcp://%1:%2").arg(address.toString()).arg(port)) == UA_STATUSCODE_GOOD;
}

///
/// \brief Returns the addresses open62541 names in the log messages of its wildcard listen sockets.
/// \param logs Arguments recorded from QUaServer::logMessage.
///
QStringList wildcardListenAddresses(const QList<QList<QVariant>> &logs)
{
    static const QRegularExpression pattern(QStringLiteral("Creating listen socket for \"([^\"]*)\" \\(with local hostname"));
    QStringList addresses;
    for (const QList<QVariant> &arguments : logs)
    {
        const QRegularExpressionMatch match = pattern.match(QString::fromUtf8(arguments.at(0).value<QUaLog>().message));
        if (match.hasMatch())
        {
            addresses << match.captured(1);
        }
    }
    return addresses;
}

} // namespace

class TestEndpointsIntegration : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void emptyHostnameListensOnAllInterfaces();
    void hostnameRestrictsListeningInterface();
    void unresolvableHostnameFailsToStart();
    void listenSocketsLogNumericAddresses();

private:
    QUaServer *_server = nullptr;
};

///
/// \brief Every test gets a fresh, stopped server.
///
void TestEndpointsIntegration::init()
{
    _server = new QUaServer;
}

void TestEndpointsIntegration::cleanup()
{
    delete _server;
    _server = nullptr;
}

void TestEndpointsIntegration::emptyHostnameListensOnAllInterfaces()
{
    const QHostAddress external = nonLoopbackAddress();
    if (external.isNull())
    {
        QSKIP("This host has no non-loopback IPv4 address");
    }
    QVERIFY(TestServer::start(*_server));

    QVERIFY(acceptsSession(QHostAddress::LocalHost, _server->port()));
    QVERIFY(acceptsSession(external, _server->port()));
}

void TestEndpointsIntegration::hostnameRestrictsListeningInterface()
{
    _server->setHostname(QStringLiteral("127.0.0.1"));
    QVERIFY(TestServer::start(*_server));

    QVERIFY(acceptsSession(QHostAddress::LocalHost, _server->port()));
    const QHostAddress external = nonLoopbackAddress();
    if (external.isNull())
    {
        QSKIP("This host has no non-loopback IPv4 address");
    }
    QVERIFY(!acceptsSession(external, _server->port()));
}

///
/// \brief A hostname that cannot be resolved leaves the server without a listen socket.
///
void TestEndpointsIntegration::unresolvableHostnameFailsToStart()
{
    _server->setHostname(QStringLiteral("quaserver.invalid"));

    QVERIFY(!TestServer::start(*_server));
    QVERIFY(!_server->isRunning());
}

///
/// \brief Listen sockets are logged by numeric address: a reverse DNS lookup per address could stall start() for
///        seconds each on hosts with unresolvable adapters.
///
void TestEndpointsIntegration::listenSocketsLogNumericAddresses()
{
    QSignalSpy logs(_server, &QUaServer::logMessage);
    QVERIFY(TestServer::start(*_server));

    const QStringList addresses = wildcardListenAddresses(logs);
    QVERIFY(!addresses.isEmpty());
    for (const QString &address : addresses)
    {
        QVERIFY2(!QHostAddress(address).isNull(), qPrintable(address));
    }
}

QTEST_GUILESS_MAIN(TestEndpointsIntegration)

#include "test_endpoints_integration.moc"
