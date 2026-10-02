#ifndef TESTSERVER_H
#define TESTSERVER_H

#include <QString>

class QUaServer;

namespace TestServer {

///
/// \brief Returns a TCP port that is currently not bound on this host.
///
quint16 freePort();

///
/// \brief Starts \a server on a free port.
/// \return True when the server is running.
///
bool start(QUaServer &server);

///
/// \brief Returns the opc.tcp endpoint URL of a started \a server.
///
QString endpointUrl(const QUaServer &server);

} // namespace TestServer

#endif // TESTSERVER_H
