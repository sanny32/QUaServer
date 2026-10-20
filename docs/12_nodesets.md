# NodeSets

Information models such as the OPC UA companion specifications are published as *NodeSet2* XML files. Instead of creating their types and instances in C++, load the file into the *Address Space*:

```c++
QUaNodeSetResult result = server.loadNodeSet("machines.NodeSet2.xml");
if (!result.isOk())
{
	qCritical() << result.errorString;
}
for (const QString &warning : result.warnings)
{
	qWarning() << warning;
}
```

A file that cannot be read or is not valid XML adds nothing and sets `errorString`. Otherwise the nodes are added in the order their parents, types and data types require, wherever they appear in the file. Nodes that cannot be added, such as a node whose parent is missing, are skipped with a warning, and so are references and values that cannot be resolved. `addedNodes` lists the nodes that were added.

A NodeSet may depend on others: load them in order, e.g. the *DI* specification before a model built on it. Companion specifications usually also build on types that only the full namespace zero has, such as `FileType` or the alarm types, so build the library with `QUASERVER_NAMESPACE_FULL` (see [Build options](../README.md#build-options)); otherwise the nodes using them are skipped with a warning. Loading a NodeSet before or after `start()` makes no difference. Nodes that already exist are skipped, so loading the same file twice adds nothing.

## Namespaces

The namespace indexes in the file refer to its `NamespaceUris`. Each URI is added to the server and gets the next free index, unless the server already has it, so look the index up by URI:

```c++
quint16 ns = server.namespaces().indexOf("http://quaserver.example/machines/");
auto lathe = server.nodeById<QUaBaseObject>(QUaNodeId(ns, 5001));
```

## C++ Instances

Loaded objects and variables get a C++ instance of the closest type registered in *QUaServer*, so they are used like any other node: `QUaFolderObject` for folders, `QUaProperty` for properties, and for the types of the NodeSet the class of their supertype, typically `QUaBaseObject` or `QUaBaseDataVariable`. Children are reachable with `browseChild()`:

```c++
auto speed = lathe->browseChild<QUaBaseDataVariable>(QUaQualifiedName(ns, "Speed"));
speed->setValue(1200.0);
```

Mandatory children that an instance of the NodeSet leaves out are created from its type, as when instantiating the type in C++.

Non-hierarchical reference types of the NodeSet can be used with `addReference()` and `findReferences()`, named by their *BrowseName* and *InverseName*:

```c++
QList<QUaNode*> fed = machine->findReferences({ "Feeds", "FedBy" });
```

Types of the NodeSet have no C++ instance, so `nodeById()` does not find them. `browseName()` reads the *BrowseName* of any node, and `isSubtypeOf()` follows the `HasSubtype` references, e.g. to tell an enumeration, whose values are `Int32`, from other data types:

```c++
QUaNodeId dataType(mode->dataTypeNodeId());
QString name = server.browseName(dataType).name();
bool enumeration = server.isSubtypeOf(dataType, QUaNodeId(0, UA_NS0ID_ENUMERATION));
```

`references()` lists the references of any node in both directions, so the type hierarchy can be walked down from a root such as *BaseObjectType*; `description()` and `isAbstract()` read the attributes of a type:

```c++
for (const QUaNodeReference& reference : server.references(QUaNodeId(0, UA_NS0ID_BASEOBJECTTYPE)))
{
    if (reference.isForward && reference.referenceTypeId == QUaNodeId(0, UA_NS0ID_HASSUBTYPE))
    {
        qDebug() << server.browseName(reference.targetNodeId).name() << server.isAbstract(reference.targetNodeId);
    }
}
```

## Limitations

* Methods of the NodeSet have no implementation, so calling them fails with `BadNotImplemented`.
* Structured data types and enumerations are registered from their `Definition`, so their values are read as `QUaStructure` (see [Structured Data Types](04_types.md#structured-data-types)) and their *DataTypeDefinition* can be read by clients. Unions are not registered. Values in the NodeSet of structures with optional fields, and enumeration fields written as `Name_Value` rather than a number, cannot be decoded by *open62541* and are left without Qt value.
* Objects and variables of event types, and of types derived from abstract types such as `BaseVariableType`, have no C++ instance.
* The models a NodeSet requires (`RequiredModel`) are not checked.

## Exporting

`exportNodeSet()` writes the nodes of the given namespaces, or of every namespace but 0, to a NodeSet2 file that `loadNodeSet()` reads back, for instance to save nodes created in C++ or by clients and serve them again later:

```c++
QUaNodeSetExportResult result = server.exportNodeSet("machines.NodeSet2.xml",
                                                     { "http://quaserver.example/machines/" });
if (!result.isOk())
{
	qWarning() << result.errorString;
}
```

The file holds the attributes, references and values of the nodes and the `Definition` of their structures and enumerations. Nodes of the exported namespaces are found from the *Root* folder and through any reference between them, so also nodes nothing organizes, such as encodings and `DesignToolOnly` instances. The file lists all namespaces of the server, so its namespace indexes are the server ones. Values whose data type cannot be written as XML, such as `DataValue`, `Variant`, `DiagnosticInfo` and unions, are left out with a warning; `exportedNodes` lists the nodes that were written.

## NodeSets Example

Build and run the [12_nodesets](../examples/12_nodesets/main.cpp) example to learn more.
