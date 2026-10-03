#include <QNetworkInterface>
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

private:
    QUaServer *m_server = nullptr;
};

///
/// \brief Every test gets a fresh, stopped server.
///
void TestEndpointsIntegration::init()
{
    m_server = new QUaServer;
}

void TestEndpointsIntegration::cleanup()
{
    delete m_server;
    m_server = nullptr;
}

void TestEndpointsIntegration::emptyHostnameListensOnAllInterfaces()
{
    const QHostAddress external = nonLoopbackAddress();
    if (external.isNull())
    {
        QSKIP("This host has no non-loopback IPv4 address");
    }
    QVERIFY(TestServer::start(*m_server));

    QVERIFY(acceptsSession(QHostAddress::LocalHost, m_server->port()));
    QVERIFY(acceptsSession(external, m_server->port()));
}

void TestEndpointsIntegration::hostnameRestrictsListeningInterface()
{
    m_server->setHostname(QStringLiteral("127.0.0.1"));
    QVERIFY(TestServer::start(*m_server));

    QVERIFY(acceptsSession(QHostAddress::LocalHost, m_server->port()));
    const QHostAddress external = nonLoopbackAddress();
    if (external.isNull())
    {
        QSKIP("This host has no non-loopback IPv4 address");
    }
    QVERIFY(!acceptsSession(external, m_server->port()));
}

///
/// \brief A hostname that cannot be resolved leaves the server without a listen socket.
///
void TestEndpointsIntegration::unresolvableHostnameFailsToStart()
{
    m_server->setHostname(QStringLiteral("quaserver.invalid"));

    QVERIFY(!TestServer::start(*m_server));
    QVERIFY(!m_server->isRunning());
}

QTEST_GUILESS_MAIN(TestEndpointsIntegration)

#include "test_endpoints_integration.moc"
