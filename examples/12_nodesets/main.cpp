#include <QCoreApplication>
#include <QDebug>
#include <QRandomGenerator>
#include <QTimer>

#include <QUaServer>

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);

	QUaServer server;

	QObject::connect(&server, &QUaServer::logMessage,
	[](const QUaLog &log) {
		qDebug() << "[" << log.level << "] :" << log.message;
	});

	// load the information model
	QUaNodeSetResult result = server.loadNodeSet("machines.NodeSet2.xml");
	if (!result.isOk())
	{
		qCritical() << result.errorString;
		return 1;
	}
	for (const QString &warning : result.warnings)
	{
		qWarning() << warning;
	}
	qDebug() << "Loaded" << result.addedNodes.count() << "nodes";

	// the NodeSet namespace gets the next free index of the server
	quint16 ns = server.namespaces().indexOf("http://quaserver.example/machines/");

	// loaded nodes have C++ instances, used like any other node
	auto lathe = server.nodeById<QUaBaseObject>(QUaNodeId(ns, 5001));
	auto serial = lathe->browseChild<QUaProperty>(QUaQualifiedName(ns, "SerialNumber"));
	qDebug() << "Serial number" << serial->value().toString();

	// Speed is mandatory in MachineType, so it was instantiated although the NodeSet left it out
	auto speed = lathe->browseChild<QUaBaseDataVariable>(QUaQualifiedName(ns, "Speed"));
	QTimer timer;
	QObject::connect(&timer, &QTimer::timeout, speed, [speed]() {
		speed->setValue(1000.0 + QRandomGenerator::global()->bounded(100.0));
	});
	timer.start(1000);

	server.start();

	return a.exec();
}
