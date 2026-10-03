# Basics

To start using *QUaServer* it is necessary to include the `QUaServer` header as follows:

```c++
#include <QUaServer>
```

To create a server simply create a `QUaServer` instance and call the `start()` method:

```c++
int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);

	// create server
	QUaServer server;
	// start server
	server.start();

	return a.exec(); 
}
```

Note it is necessary to create a `QCoreApplication` and execute it, because `QUaServer` makes use of [Qt's event loop](https://wiki.qt.io/Threads_Events_QObjects).

By default the *QUaServer* listens on port **4840** which is the [IANA assigned port](https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml?search=4840) for OPC UA applications. To change the listening port, call the `setPort` method **before** starting the server:

```c++
server.setPort(8080);
```

To start creating OPC *Objects* and *Variables* it is necessary to get the *Objects Folder* of the server and start adding instances to it:

```c++
int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);

	QUaServer server;

	// get objects folder
	QUaFolderObject * objsFolder = server.objectsFolder();

	// add some instances to the objects folder
	QUaBaseDataVariable * varBaseData = objsFolder->addBaseDataVariable("my_variable");
	QUaProperty         * varProp     = objsFolder->addProperty("my_property");
	QUaBaseObject       * objBase     = objsFolder->addBaseObject("my_object");
	QUaFolderObject     * objFolder   = objsFolder->addFolderObject("my_folder");

	// set values to variables
	varBaseData->setValue(1);
	varProp->setValue("hola");

	server.start();

	return a.exec(); 
}
```

Instances must only be added using the *QUaServer* API, by using the following methods:

* `addProperty` : Adds a `QUaProperty` instance. [*Properties*](https://reference.opcfoundation.org/v104/Core/docs/Part3/4.4.2/) are the **leaves** of the Address Space tree and cannot have other children. They are used to characterise what its parent represents and their value do not change often. For example, an *engineering unit* or a *brand name*.

* `addBaseDataVariable` : Adds a `QUaBaseDataVariable` instance. [*BaseDataVariables*](https://reference.opcfoundation.org/v104/Core/docs/Part3/5.6.4/) are used to hold data which might change often and can have children (*Objects*, *Properties*, other *BaseDataVariables*). An example is the *current value* of a temperature sensor.

* `addBaseObject` : Adds a `QUaBaseObject` instance. [*BaseObjects*](https://reference.opcfoundation.org/v104/Core/docs/Part3/5.5.1/) can have children and are used to organize other *Objects*, *Properties*, *BaseDataVariables*, etc. The purpose of objects is to **model** a real device. For example a temperature sensor which has *engineering unit* and *brand name* as properties and *current value* as a variable.

* `addFolderObject` : Adds a `QUaFolderObject` instance. [*FolderObjects*](https://reference.opcfoundation.org/v104/Core/docs/Part3/5.5.3/) derive from *BaseObjects* and can do the same, but are typically used to organize a collection of objects. The so called **Objects Folder** is a `QUaFolderObject` instance that always exists on the server to serve as a container for all the user instances.

Once connected to the server, the [address space](https://reference.opcfoundation.org/v104/Core/docs/Part1/6.3.4/) should look something like this:

<p align="center">
  <img src="../res/img/01_basics_02.jpg">
</p>

The string argument passed to these methods defines both the node's initial [`DisplayName`](https://reference.opcfoundation.org/v104/Core/docs/Part3/5.2.5/) and [`BrowseName`](https://reference.opcfoundation.org/v104/Core/docs/Part3/5.2.4/). The `DisplayName` is the name that is displayed to the user by client applications, it should be a human-friendly name. The `BrowseName` is the name that is used programmatically by client applications to find children nodes easily in hierarchical node structures. The `BrowseName` is **immutable** once a node instance has been created, and must be **unique** with respect to its parent, so one parent node cannot have multiple children with the same `BrowseName`. The `DisplayName` has no restrictions, so it can be changed programmatically.

The `Value` is also set for the variables defined in the example above. The `DataType` of the `Value` is inferred automatically by `QUaServer`, but it can also be set explicitly as it will be shown later.

The `DisplayName`, `BrowseName`, `Value` and `DataType` are [**OPC Attributes**](https://reference.opcfoundation.org/v104/Robotics/docs/3.4.3/). Depending on the type of the instance (*Properties*, *BaseDataVariables*, etc.) it is possible to set different attributes. All OPC instance types derive from the [**Node**](https://reference.opcfoundation.org/v104/Core/docs/Part3/4.3.1/) type. Similarly, in *QUaServer*, all the types derive directly or indirectly from the C++ `QUaNode` abstract class. 

The *QUaServer* API allows to read and write the instances attributes with the following methods:

## For all Types

The *QUaNode* API provides the following methods to access attributes:

```c++
QUaLocalizedText displayName   () const;
void             setDisplayName(const QUaLocalizedText &displayName);

QUaLocalizedText description   () const;
void             setDescription(const QUaLocalizedText &description);

quint32 writeMask     () const;
void    setWriteMask  (const quint32 &writeMask);

QUaNodeId nodeId() const;

QString nodeClass() const;

QUaQualifiedName browseName() const;
```

The `nodeId()` method returns an object containing the [**NodeId**](https://reference.opcfoundation.org/v104/Core/docs/Part3/5.2.2/), which is a unique identifier of the node. This is the only **unique** identifier of a node within a server, because neither the `BrowseName` nor `DisplayName` attributes are unique.

By default a random `NodeId` is assigned automatically when creating a node instance. It is possible to define a custom `NodeId` upon instantiation by passing the string *XML notation* as the *second* argument to the respective method. If the NodeId is invalid or already exists, creating the instance will fail returning `nullptr`. For example:

```c++
QUaProperty * varProp = objsFolder->addProperty("my_property", "ns=1;s=my_prop");
if(!varProp)
{
	qDebug() << "Creating instance failed!";
}
```

To notify changes, the *QUaNode* API provides the following *Qt signals*:

```c++
void displayNameChanged(const QUaLocalizedText &displayName);
void descriptionChanged(const QUaLocalizedText &description);
void writeMaskChanged  (const quint32 &writeMask);
```

Furthermore, the API also notifies when a child is added to an *QUaBaseObject* or *QUaBaseDataVariable* instance:

```c++
void childAdded(QUaNode * childNode);
```

## For Variable Types

Both `QUaBaseDataVariable` and `QUaProperty` derive from the abstract C++ class `QUaBaseVariable` which provides the following methods to access the variable's [**attributes**](https://reference.opcfoundation.org/v104/Core/docs/Part3/5.6.2/):

```c++
QVariant          value() const;
void              setValue(const QVariant &value);

QDateTime         sourceTimestamp() const;
void              setSourceTimestamp(const QDateTime& sourceTimestamp);

QDateTime         serverTimestamp() const;
void              setServerTimestamp(const QDateTime& serverTimestamp);

QUaStatusCode     statusCode() const;
void              setStatusCode(const QUaStatusCode& statusCode);

QMetaType::Type   dataType() const;
void              setDataType(const QMetaType::Type &dataType);

qint32            valueRank() const;
void              setValueRank(const qint32 &valueRank);
QVector<quint32>  arrayDimensions() const; 

quint8            accessLevel() const;
void              setAccessLevel(const quint8 &accessLevel);

double            minimumSamplingInterval() const;
void              setMinimumSamplingInterval(const double &minimumSamplingInterval);

bool              historizing() const;
```

The `value`, `statusCode`, `sourceTimestamp`, `serverTimestamp` and optionally the `dataType` can be set in a single call:

```c++
void setValue(
	const QVariant        &value,
	const QUaStatusCode   &statusCode      = QUaStatus::Good,
	const QDateTime       &sourceTimestamp = QDateTime(),
	const QDateTime       &serverTimestamp = QDateTime(),
	const QMetaType::Type &newDataType     = QMetaType::UnknownType
);
```

The `setDataType()` can be used to *force* a data type on the variable value. The following [Qt types](https://doc.qt.io/qt-6/qmetatype.html#Type-enum) are supported, as well as their `QList<T>` and `QVector<T>` types:

```c++
QMetaType::Bool
QMetaType::Char
QMetaType::SChar
QMetaType::UChar
QMetaType::Short
QMetaType::UShort
QMetaType::Int
QMetaType::UInt
QMetaType::Long
QMetaType::LongLong
QMetaType::ULong
QMetaType::ULongLong
QMetaType::Float
QMetaType::Double
QMetaType::QString
QMetaType::QDateTime
QMetaType::QUuid
QMetaType::QByteArray
```

The `setAccessLevel()` method allows to set a bit mask to define the overall variable read and write access. Nevertheless, the `QUaBaseVariable` API provides a couple of helper methods that allow to define the access more easily without needing to deal with bit masks:

```c++
// Default : read access true
bool readAccess() const;
void setReadAccess(const bool &readAccess);
// Default : write access false
bool writeAccess() const;
void setWriteAccess(const bool &writeAccess);
```

Using such methods we could set a variable to be **writable**, for example:

```c++
QUaBaseDataVariable * varBaseData = objsFolder->addBaseDataVariable("my_variable");
varBaseData->setWriteAccess(true);
```

When a variable is written from a client, on the server notifications are provided by the `void QUaBaseVariable::valueChanged(const QVariant &value, const bool &networkChange)` Qt signal.

```c++
QObject::connect(varBaseData, &QUaBaseDataVariable::valueChanged, [](const QVariant &value, const bool &networkChange) {
	qDebug() << "New value :" << value << (networkChange ? "(Network)" : "(Server Logic)");
});
```

The `networkChange` argument specifies if the value was changed through the network by an OPC client or programmatically by the server logic.

## For Object Types

The API provides the following methods to access attributes:

```c++
quint8 eventNotifier() const;
void   setEventNotifier(const quint8 &eventNotifier);

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
bool subscribeToEvents() const;
void setSubscribeToEvents(const bool& subscribeToEvents);
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
```

The usage of these methods is described in detail in the [Events](08_events.md) section.

## Basics Example

Build and run the [01_basics](../examples/01_basics/main.cpp) example to learn more.
