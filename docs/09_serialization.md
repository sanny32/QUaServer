# Serialization

The *QUaServer* supports creating and destroying nodes at *runtime*. Therefore it is possible for a client to modify the server's *Address Space* remotely. This can be achieved, for example, through methods:

```c++
QUaFolderObject * objsFolder = server.objectsFolder();

objsFolder->addMethod("CreateVariable", [objsFolder](QString strVariableName) {
	if (objsFolder->browseChild(strVariableName))
	{
		return QString("Error : Variable %1 already exists.").arg(strVariableName);
	}
	auto newVar = objsFolder->addBaseDataVariable(strVariableName);
	return QString("Success : Variable %1 created.").arg(strVariableName);
});

objsFolder->addMethod("DestroyVariable", [objsFolder](QString strVariableName) {
	auto var = objsFolder->browseChild(strVariableName);
	if (!var)
	{
		return QString("Error : Variable %1 does not exist.").arg(strVariableName);
	}
	delete var;
	return QString("Success : Variable %1 destroyed.").arg(strVariableName);
});
```

Note it is possible to make *UaExpert* refresh the *Address Space* automatically, by compiling the snippet above with the `QUASERVER_EVENTS` CMake option.

All the nodes created at *runtime* exist only in memory, so if the server program is restarted, all those nodes will be lost.

To solve this issue, `QUaNode` provides a *serialization* API to help saving the current state of the *Address Space* to disk, and also to be able to restore it from disk.

To save to disk, `QUaNode` provides the `serialize` method:

```c++
template<typename T>
bool serialize(T& serializer, QQueue<QUaLog> &logOut);
```

Where `T` is any C++ *type* implementing the following interface:

```c++
// required API for QUaNode::serialize
bool writeInstance(
	const QUaNodeId& nodeId,
	const QString& typeName,
	const QMap<QString, QVariant>& attrs,
	const QList<QUaForwardReference>& forwardRefs,
	QQueue<QUaLog>& logOut
);
```

When the `serialize` method is called over a node instance, it will call the `serializer`'s `writeInstance` method recursively, starting with the node instance that called it and all its descendants. If the `writeInstance` method returns `false`, the recursion stops, and the call to `serialize` also returns `false`. Any useful log messages should be added to the `logOut` queue.

It is then responsibility of the `writeInstance` implementation to save to disk the node's information (`nodeId`, `attrs` and `forwardRefs`) in a sensible manner. For example, in the [09_serialization](../examples/09_serialization) example, the `QUaXmlSerializer` class implements serialization to XML in the following format:

```xml
<?xml version='1.0' encoding='UTF-8'?>
<nodes>
 <n nodeId="ns=0;i=85" browseName="Objects" description="" eventNotifier="0" displayName="Objects" writeMask="0">
  <r forwardName="Organizes" targetType="QUaBaseObject" targetNodeId="ns=1;s=my_obj" inverseName="OrganizedBy"/>
  <r forwardName="Organizes" targetType="QUaFolderObject" targetNodeId="ns=0;i=1592406929" inverseName="OrganizedBy"/>
  <r forwardName="Organizes" targetType="QUaBaseDataVariable" targetNodeId="ns=0;i=2501818547" inverseName="OrganizedBy"/>
  <r forwardName="Organizes" targetType="QUaProperty" targetNodeId="ns=1;s=my_prop" inverseName="OrganizedBy"/>
 </n>
 <n nodeId="ns=1;s=my_obj" browseName="my_object" description="" eventNotifier="0" displayName="my_object" writeMask="0">
  <r forwardName="FriendOf" targetType="TemperatureSensor" targetNodeId="ns=0;i=2070436686" inverseName="FriendOf"/>
  <r forwardName="HasProperty" targetType="QUaProperty" targetNodeId="ns=0;i=2687773104" inverseName="PropertyOf"/>
  <r forwardName="HasOrderedComponent" targetType="QUaBaseObject" targetNodeId="ns=0;i=4261035154" inverseName="OrderedComponentOf"/>
  <r forwardName="HasOrderedComponent" targetType="QUaFolderObject" targetNodeId="ns=0;i=2452012465" inverseName="OrderedComponentOf"/>
 </n>
 <!-- more nodes ... -->
</nodes>
```

It simply writes down a list of nodes, each node with the `<n>` *XML tag* and all the node's attributes as *XML attributes*. Each `<n>` contains a list of `<r>` *XML tags* as children listing the *forward references* for that node. This is all the information required to *serialize* the state of the *Address Space*.

Note that the `typeName` is not serialized as an *XML attribute*. The `typeName` is passed to the `writeInstance` method to allow the user to organize the data by type if desired. In the *XML* serialization this is not necessary, but if serializing to *SQL*, knowing the `typeName` might be useful to store all instance of a type in their own table.

The type `T` can *optionally* implement the following interface:

```c++
// optional API for QUaNode::serialize
bool serializeStart(QQueue<QUaLog>& logOut);

// optional API for QUaNode::serialize
bool serializeEnd(QQueue<QUaLog>& logOut);
```

Implementing such methods can be useful to perform initialization tasks such as opening a file for writing, and to perform clean up tasks such as closing the file or release any other resources.

To serialize all node instances in the *Address Space*, `serialize` should be called over the *Objects Folder* of the server, for example:

```c++
objsFolder->addMethod("Serialize", [objsFolder](QString strFileName) {
	QUaXmlSerializer serializer;
	QQueue<QUaLog> logOut;
	if (!serializer.setXmlFileName(strFileName, logOut))
	{
		return QString("Error in file name.");
	}
	if (!objsFolder->serialize(serializer, logOut))
	{
		return QString("Error serializing. Check the logOut.");
	}
	return QString("Success : Serialized to %1 file.").arg(strFileName);
});
```

To restore the *Address Space* from disk, `QUaNode` provides the `deserialize` method:

```c++
template<typename T>
bool deserialize(T& deserializer, QQueue<QUaLog>& logOut);
```

Where `T` is any C++ *type* implementing the following interface:

```c++
// required API for QUaNode::deserialize
bool readInstance(
	const QUaNodeId &nodeId,
	const QString &typeName,
	QMap<QString, QVariant> &attrs,
	QList<QUaForwardReference> &forwardRefs,
	QQueue<QUaLog> &logOut
);

// optional API for QUaNode::deserialize
bool deserializeStart(QQueue<QUaLog>& logOut);

// optional API for QUaNode::deserialize
bool deserializeEnd(QQueue<QUaLog>& logOut);
```

Which work in a similar fashion as the serializing interface (`writeInstance`, `serializeStart` and `serializeEnd` respectively).

When deserializing, it is responsibility of the `readInstance` implementation to return the node's information (`typeName`, `attrs` and `forwardRefs`) for a given `nodeId` and `typeName`.

The `deserialize` call then takes care of restoring the *Address Space* instantiating any missing nodes or overwriting the attributes of any existing nodes.

To deserialize all node instances in the *Address Space*, `deserialize` should be called over the *Objects Folder* of the server, for example:

```c++
objsFolder->addMethod("Deserialize", [objsFolder](QString strFileName) {
	QUaXmlSerializer serializer;
	QQueue<QUaLog> logOut;
	if (!serializer.setXmlFileName(strFileName, logOut))
	{
		return QString("Error in file name.");
	}
	if (!objsFolder->deserialize(serializer, logOut))
	{
		return QString("Error deserializing. Check the logOut.");
	}
	return QString("Success : Deserialized from %1 file.").arg(strFileName);
});
```

Note that the `readInstance` interface requires the underlying data source to be **queryable** by the `nodeId`. This is not the case for an XML file, therefore the `QUaXmlSerializer` example loads all the contents of the XML into a *queryable* structure in memory. As the number of nodes scale, loading all the contents from disk to memory might not be feasible. Then serializing to a *queryable* database might be a better alternative. In the [09_serialization](../examples/09_serialization) example, the `QUaSqliteSerializer` class implements serialization to a *queryable* *Sqlite* database.

Both `QUaXmlSerializer` and `QUaSqliteSerializer` classes provided in the [09_serialization](../examples/09_serialization) example are just to demonstrate the use of the serialization API. They are by no means the best or most efficient way to serialize the *Address Space*, the user should provide their own *serializer* implementation.

## Serialization Example

Build and run the [09_serialization](../examples/09_serialization/main.cpp) example to learn more.
