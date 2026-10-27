#ifndef TESTSERVER_H
#define TESTSERVER_H

#include <QString>

class QUaServer;

namespace TestServer {

///
/// \brief Returns an unused TCP port reserved against other test processes until this process exits.
/// \return The port, or zero if no port could be reserved.
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
