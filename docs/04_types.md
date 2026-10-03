# Types

OPC types can be extended by subtyping *BaseObjects* or *BaseDataVariables* (*Properties* cannot be subtyped). Using the *QUaServer* library, a new *BaseObject* subtype can be created by deriving from `QUaBaseObject`. Similarly, a new *BaseDataVariable* subtype can be created by deriving from `QUaBaseDataVariable`.

Subtyping is very useful to **reuse** code. For example, if multiple temperature sensors are to be exposed through the OPC UA Server, it might be worth creating a type for it. Start by sub-classing `QUaBaseObject` as follows:

In `temperaturesensor.h` :

```c++
#include <QUaBaseObject>

class TemperatureSensor : public QUaBaseObject
{
	Q_OBJECT

public:
	Q_INVOKABLE explicit TemperatureSensor(QUaServer *server);
	
};
```

In `temperaturesensor.cpp` :

```c++
#include "temperaturesensor.h"

TemperatureSensor::TemperatureSensor(QUaServer *server)
	: QUaBaseObject(server)
{
	
}
```

There are 3 **important requirements** when creating subtypes:

* Inherit from either *QUaBaseObject* or *QUaBaseDataVariable* (which in turn inherit indirectly from *QObject*). The `Q_OBJECT` macro must be set.

* Create a public constructor that receives a `QUaServer` pointer as an argument. Add the `Q_INVOKABLE` macro to such constructor.

* In the constructor implementation call the parent constructor (*QUaBaseObject*, *QUaBaseDataVariable* or derived parent constructor accordingly).

Once all this is met, elsewhere in the code it is necessary to register the new type in the server using the `registerType<T>()` method. If not registered, then when creating an instance of the new type, the type will be registered automatically by the library.

An instance of the new type is created using the `addChild<T>()` method:

```c++
#include "temperaturesensor.h"

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);

	QUaServer server;

	QUaFolderObject * objsFolder = server.objectsFolder();

	// register new type
	server.registerType<TemperatureSensor>();

	// create new type instance
	auto sensor1 = objsFolder->addChild<TemperatureSensor>("Sensor1");

	server.start();

	return a.exec(); 
}
``` 

If the new type was registered correctly, it can be observed by browsing to `/Root/Types/ObjectTypes/BaseObjectType`. There should be a new entry corresponding to the custom type.

<p align="center">
  <img src="img/04_types_01.jpg">
</p>

Note that the new *TemperatureSensor* type has a `TypeDefinitionOf` reference to the *Sensor1* instance. And the *Sensor1* instance has a `HasTypeDefinition` to the *TemperatureSensor* type.

Adding **child** *Variables*, *Properties* and potentially other *Objects* to the *TemperatureSensor* type is achieved through the [Qt Property System](https://doc.qt.io/qt-6/properties.html). 

Use the `Q_PROPERTY` macro to add pointers to types of desired children and the library will automatically instantiate the children once a specific instance of the *TemperatureSensor* type is created.

```c++
#include <QUaBaseObject>
#include <QUaBaseDataVariable>
#include <QUaProperty>

class TemperatureSensor : public QUaBaseObject
{
	Q_OBJECT
	// properties
	Q_PROPERTY(QUaProperty * model READ model)
	Q_PROPERTY(QUaProperty * brand READ brand)
	Q_PROPERTY(QUaProperty * units READ units)
	// variables
	Q_PROPERTY(QUaBaseDataVariable * status       READ status      )
	Q_PROPERTY(QUaBaseDataVariable * currentValue READ currentValue)
public:
	Q_INVOKABLE explicit TemperatureSensor(QUaServer *server);

	QUaProperty * model();
	QUaProperty * brand();
	QUaProperty * units();

	QUaBaseDataVariable * status      ();
	QUaBaseDataVariable * currentValue();
};
```

The *QUaServer* library automatically adds the C++ instances as [QObject children](https://doc.qt.io/qt-6/objecttrees.html) of the *TemperatureSensor* instance and assigns them the `Q_PROPERTY` name as their [QObject name](https://doc.qt.io/qt-6/qobject.html#objectName-prop). Therefore it is possible to retrieve the C++ children using the [`findChild` method](https://doc.qt.io/qt-6/qobject.html#findChild), or by their `BrowseName` using the `browseChild` method as shown below.

```c++
TemperatureSensor::TemperatureSensor(QUaServer *server)
	: QUaBaseObject(server)
{
	// set defaults
	model()->setValue("TM35");
	brand()->setValue("Texas Instruments");
	units()->setValue("C");
	status()->setValue("Off");
	currentValue()->setValue(0.0);
	currentValue()->setDataType(QMetaType::Double);
}

QUaProperty * TemperatureSensor::model()
{
	return this->browseChild<QUaProperty>("model");
}

QUaProperty * TemperatureSensor::brand()
{
	return this->browseChild<QUaProperty>("brand");
}

QUaProperty * TemperatureSensor::units()
{
	return this->browseChild<QUaProperty>("units");
}

QUaBaseDataVariable * TemperatureSensor::status()
{
	return this->browseChild<QUaBaseDataVariable>("status");
}

QUaBaseDataVariable * TemperatureSensor::currentValue()
{
	return this->browseChild<QUaBaseDataVariable>("currentValue");
}
```

Be **careful** when using the `browseChild` to provide the correct `BrowseName`, otherwise a null reference can be returned from any of the getter methods.

Note that in the *TemperatureSensor* constructor it is possible to already make use of the children instances and define some default values for them.

Now it is possible to create any number of *TemperatureSensor* instances and their children will be created and attached to them automatically.

```c++
auto sensor1 = objsFolder->addChild<TemperatureSensor>("Sensor1");
auto sensor2 = objsFolder->addChild<TemperatureSensor>("Sensor2");
auto sensor3 = objsFolder->addChild<TemperatureSensor>("Sensor3");
```

<p align="center">
  <img src="img/04_types_02.jpg">
</p>

Any `Q_PROPERTY` added to the *TemperatureSensor* declaration that **inherits** `QUaProperty`, `QUaBaseDataVariable` or `QUaBaseObject` will be exposed through OPC UA. Else the `Q_PROPERTY` will be created in the C++ instance but not exposed through OPC UA.

To add **methods** to a subtype, the `Q_INVOKABLE` macro can be used. The limitations are that only [up to 10 arguments can be used](https://doc.qt.io/qt-6/qmetamethod.html#invoke) and the argument types can only be the same supported by the `setDataType()` method (see the [Basics](01_basics.md) section).

```c++
class TemperatureSensor : public QUaBaseObject
{
	Q_OBJECT
	
	// properties, variables, objects ...

public:
	Q_INVOKABLE explicit TemperatureSensor(QUaServer *server);

	// properties, variables, objects ...

	Q_INVOKABLE void turnOn();
	Q_INVOKABLE void turnOff();
};
```

The implementation is like a normal C++ class method:

```c++
void TemperatureSensor::turnOn()
{
	status()->setValue("On");
}

void TemperatureSensor::turnOff()
{
	status()->setValue("Off");
}
```

If the `Q_INVOKABLE` macro is not used, then the method is simply not exposed through OPC UA.

<p align="center">
  <img src="img/04_types_03.jpg">
</p>

One final perk of creating subtypes is the possibility of creating custom enumerators which can be used as data types for variables. This is done using the `Q_ENUM` macro:

```c++
class TemperatureSensor : public QUaBaseObject
{
	Q_OBJECT
	
	// properties, variables, objects ...

public:
	Q_INVOKABLE explicit TemperatureSensor(QUaServer *server);

	// properties, variables, objects ...
	// methods ...

	enum Units
	{
		C = 0,
		F = 1
	};
	Q_ENUM(Units)

};
```

Then using the enumerator to set the value of a *Variable*:

```c++
TemperatureSensor::TemperatureSensor(QUaServer *server)
	: QUaBaseObject(server)
{
	// set defaults ...
	// use enum as type
	units()->setDataTypeEnum(QMetaEnum::fromType<TemperatureSensor::Units>());
	units()->setValue(Units::C);
}
```

The shorter `units()->setDataTypeEnum<TemperatureSensor::Units>()` overload is equivalent.

Then any client has knowledge of the enum options.

## Structured Data Types

A variable value can also be a structure: a data type made of named fields. Register it with its fields, each a built-in type, an enumeration or a structure registered before, optionally as an array (third argument) or optional (fourth argument):

```c++
QUaNodeId pointId = server.registerStructure("Point", {
	{ "x", QMetaType::Double },
	{ "y", QMetaType::Double }
});
QUaNodeId segmentId = server.registerStructure("Segment", {
	{ "start"  , pointId },
	{ "end"    , pointId },
	{ "tags"   , QMetaType::QString, true },
	{ "comment", QMetaType::QString, false, true }
});
```

The data type gets the NodeId `ns=1;s=<name>` unless one is passed as third argument, and a null NodeId is returned when the NodeId is already used, a field name is empty or repeated, or a field type is unknown. Clients learn the layout from the *DataTypeDefinition* attribute of the data type, and use it to decode the values.

Values are `QUaStructure` objects: the NodeId of the type and the fields by name. A field of a structure type holds a `QUaStructure`, an array field a list:

```c++
QUaStructure start(pointId);
start.setField("x", 1.5);
start.setField("y", 2.5);

QUaStructure segment(segmentId);
segment.setField("start", QVariant::fromValue(start));
segment.setField("tags", QStringList{ "a", "b" });

auto varSegment = objsFolder->addBaseDataVariable("segment");
varSegment->setValue(QVariant::fromValue(segment));

QUaStructure value = varSegment->value().value<QUaStructure>();
double x = value.field("start").value<QUaStructure>().field("x").toDouble();
```

* Fields that are not set take their default value, `end` above is the point (0, 0). Optional fields that are not set are left out of the value, `hasField()` tells whether a value has them.
* A value whose type is not registered, or whose fields do not convert to their type, is ignored by `setValue()`.
* A list of structures is an array of them. The variable `dataType()` is `QMetaType_Structure`, and `dataTypeNodeId()` gives the NodeId of the structure.
* Unions, structures with fields of more than one dimension and structures inheriting the fields of another structure are not supported.

## Types Example

Build and run the [04_types](../examples/04_types/main.cpp) example to learn more.
