#include "testserver.h"

#include <QTcpServer>

#include <QUaServer>

namespace TestServer {

///
/// \brief Returns a TCP port that is currently not bound on this host.
///
quint16 freePort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::Any, 0))
    {
        return 0;
    }
    return probe.serverPort();
}

///
/// \brief Starts \a server on a free port.
/// \return True when the server is running.
///
bool start(QUaServer &server)
{
    const quint16 port = freePort();
    if (port == 0)
    {
        return false;
    }
    server.setPort(port);
    return server.start();
}

///
/// \brief Returns the opc.tcp endpoint URL of a started \a server.
///
QString endpointUrl(const QUaServer &server)
{
    return QStringLiteral("opc.tcp://127.0.0.1:%1").arg(server.port());
}

} // namespace TestServer
