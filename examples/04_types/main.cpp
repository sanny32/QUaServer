#include <QCoreApplication>
#include <QDebug>

#include <QUaServer>

#include "temperaturesensor.h"

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);

	QUaServer server;

	// logging

	QObject::connect(&server, &QUaServer::logMessage,
	[](const QUaLog& log) {
		qDebug() << "[" << log.level << "] :" << log.message;
	});

	// register new type

	server.registerType<TemperatureSensor>();

	// create new type instances

	QUaFolderObject * objsFolder = server.objectsFolder();

	objsFolder->addChild<TemperatureSensor>("Sensor1");
	objsFolder->addChild<TemperatureSensor>("Sensor2");
	objsFolder->addChild<TemperatureSensor>("Sensor3");

	// register structured data type and use it as value

	QUaNodeId pointId = server.registerStructure("Point", {
		{ "x", QMetaType::Double },
		{ "y", QMetaType::Double }
	});
	QUaStructure point(pointId);
	point.setField("x", 1.5);
	point.setField("y", 2.5);
	auto position = objsFolder->addBaseDataVariable("Position");
	position->setWriteAccess(true);
	position->setValue(QVariant::fromValue(point));
	QObject::connect(position, &QUaBaseDataVariable::valueChanged, [](const QVariant &value) {
		QUaStructure newPoint = value.value<QUaStructure>();
		qDebug() << "New position :" << newPoint.field("x").toDouble() << newPoint.field("y").toDouble();
	});

	server.start();

	return a.exec(); 
}
