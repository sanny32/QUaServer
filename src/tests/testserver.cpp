#include "testserver.h"

#include <memory>
#include <mutex>
#include <vector>

#include <QDir>
#include <QLockFile>
#include <QTcpServer>

#include <QUaServer>

namespace TestServer {

///
/// \brief Finds an unused port and reserves it against other test processes until this process exits.
///
quint16 freePort()
{
    static std::mutex mutex;
    static std::vector<std::unique_ptr<QLockFile>> reservations;
    const std::lock_guard<std::mutex> guard(mutex);

    for (int attempt = 0; attempt < 100; ++attempt)
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::Any, 0))
        {
            return 0;
        }
        const quint16 port = probe.serverPort();
        auto reservation = std::make_unique<QLockFile>(QDir::temp().filePath(
            QStringLiteral("quaserver-test-port-%1.lock").arg(port)));
        // Keep reservations through server restarts and long tests. Dead processes
        // can still be detected by QLockFile; age alone must not invalidate a lock.
        reservation->setStaleLockTime(0);
        if (!reservation->tryLock(0))
        {
            continue;
        }
        reservations.push_back(std::move(reservation));
        return port;
    }
    return 0;
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
