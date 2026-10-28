#include "quanodesetwriter.h"

#include <QUuid>
#include <QXmlStreamReader>
#include <QtMath>

namespace {

const QString kNodeSetNamespace = QStringLiteral("http://opcfoundation.org/UA/2011/03/UANodeSet.xsd");
const QString kTypesNamespace   = QStringLiteral("http://opcfoundation.org/UA/2008/02/Types.xsd");

///
/// \brief Owns an open62541 copy of a NodeId for the duration of a call.
///
class UaNodeId
{
public:
	explicit UaNodeId(const QUaNodeId& nodeId) : id(nodeId) {}
	~UaNodeId() { UA_NodeId_clear(&id); }
	UaNodeId(const UaNodeId&) = delete;
	UaNodeId& operator=(const UaNodeId&) = delete;

	UA_NodeId id;
};

///
/// \brief Owns an attribute value read from the server.
///
class UaVariant
{
public:
	UaVariant() { UA_Variant_init(&value); }
	~UaVariant() { UA_Variant_clear(&value); }
	UaVariant(const UaVariant&) = delete;
	UaVariant& operator=(const UaVariant&) = delete;

	template<typename T>
	const T* scalar(const UA_DataType* type) const
	{
		return UA_Variant_hasScalarType(&value, type) ? static_cast<const T*>(value.data) : nullptr;
	}

	UA_Variant value;
};

///
/// \brief Returns the NodeSet2 element of a node class, empty for classes a NodeSet cannot hold.
///
QString elementOfNodeClass(UA_NodeClass nodeClass)
{
	switch (nodeClass)
	{
	case UA_NODECLASS_OBJECT:        return QStringLiteral("UAObject");
	case UA_NODECLASS_VARIABLE:      return QStringLiteral("UAVariable");
	case UA_NODECLASS_METHOD:        return QStringLiteral("UAMethod");
	case UA_NODECLASS_OBJECTTYPE:    return QStringLiteral("UAObjectType");
	case UA_NODECLASS_VARIABLETYPE:  return QStringLiteral("UAVariableType");
	case UA_NODECLASS_REFERENCETYPE: return QStringLiteral("UAReferenceType");
	case UA_NODECLASS_DATATYPE:      return QStringLiteral("UADataType");
	case UA_NODECLASS_VIEW:          return QStringLiteral("UAView");
	default:                         return QString();
	}
}

///
/// \brief Formats a boolean attribute value as NodeSet2 expects it.
///
QString boolText(bool value)
{
	return value ? QStringLiteral("true") : QStringLiteral("false");
}

///
/// \brief Converts an open62541 string to UTF-8 text.
///
QString stringText(const UA_String& string)
{
	return QString::fromUtf8(reinterpret_cast<const char*>(string.data), static_cast<qsizetype>(string.length));
}

///
/// \brief Formats a floating point number as the XML schema expects it, including NaN and infinities.
///
QString floatText(double value, int precision)
{
	if (qIsNaN(value))
	{
		return QStringLiteral("NaN");
	}
	if (qIsInf(value))
	{
		return value > 0 ? QStringLiteral("INF") : QStringLiteral("-INF");
	}
	return QString::number(value, 'g', precision);
}

///
/// \brief Returns the element name of a data type, e.g. "Int32" or the name of a structure.
///
QString typeName(const UA_DataType* type)
{
	// subtypes of built-in types such as Duration are encoded as the built-in type; UA_TYPES starts with them
	if (type->typeKind <= UA_DATATYPEKIND_DIAGNOSTICINFO)
	{
		type = &UA_TYPES[type->typeKind];
	}
	if (!type->typeName)
	{
		return QStringLiteral("Structure");
	}
	// types built from a description are named after their browse name, e.g. "2:Reading"
	const QString name = QString::fromUtf8(type->typeName);
	return name.mid(name.lastIndexOf(QLatin1Char(':')) + 1);
}

bool writeContent(QXmlStreamWriter& writer, const void* data, const UA_DataType* type);
bool writeVariant(QXmlStreamWriter& writer, const UA_Variant& value);

///
/// \brief Writes \a data as an element called \a name holding its content.
/// \return False when the data type cannot be encoded.
///
bool writeElement(QXmlStreamWriter& writer, const QString& name, const void* data, const UA_DataType* type)
{
	writer.writeStartElement(name);
	const bool ok = writeContent(writer, data, type);
	writer.writeEndElement();
	return ok;
}

///
/// \brief Writes the elements of an array, one per item, named after the item type; structures in a Variant are
///        wrapped in ExtensionObjects, structures in a structure field are not.
/// \return False when the item type cannot be encoded.
///
bool writeItems(QXmlStreamWriter& writer, const void* items, size_t count, const UA_DataType* type, bool inVariant)
{
	const bool structure = inVariant && type->typeKind > UA_DATATYPEKIND_DIAGNOSTICINFO &&
	                       type->typeKind != UA_DATATYPEKIND_ENUM;
	const QString name = structure ? QStringLiteral("ExtensionObject") :
	                     type->typeKind == UA_DATATYPEKIND_ENUM ? QStringLiteral("Int32") : typeName(type);
	bool ok = true;
	for (size_t i = 0; i < count && ok; i++)
	{
		const void* item = static_cast<const char*>(items) + i * type->memSize;
		if (structure)
		{
			UA_ExtensionObject wrapper;
			UA_ExtensionObject_setValue(&wrapper, const_cast<void*>(item), type);
			ok = writeElement(writer, name, &wrapper, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT]);
		}
		else
		{
			ok = writeElement(writer, name, item, type);
		}
	}
	return ok;
}

///
/// \brief Writes the fields of a structure, each as an element named after the field.
/// \return False when a field type cannot be encoded.
///
bool writeStructure(QXmlStreamWriter& writer, const void* data, const UA_DataType* type)
{
	const bool isUnion = type->typeKind == UA_DATATYPEKIND_UNION;
	UA_UInt32 selection = 0;
	if (isUnion)
	{
		selection = *static_cast<const UA_UInt32*>(data);
		if (selection > type->membersSize)
		{
			return false;
		}
		writer.writeTextElement(QStringLiteral("SwitchField"), QString::number(selection));
	}
	uintptr_t position = reinterpret_cast<uintptr_t>(data);
	for (size_t i = 0; i < type->membersSize; i++)
	{
		if (isUnion)
		{
			if (selection != i + 1)
			{
				continue;
			}
			position = reinterpret_cast<uintptr_t>(data);
		}
		const UA_DataTypeMember& member = type->members[i];
		const UA_DataType* memberType = member.memberType;
		const QString name = member.memberName ? QString::fromLatin1(member.memberName) : QString();
		position += member.padding;
		if (member.isArray)
		{
			const size_t count = *reinterpret_cast<const size_t*>(position);
			position += sizeof(size_t);
			const void* items = *reinterpret_cast<void* const*>(position);
			position += sizeof(void*);
			writer.writeStartElement(name);
			const bool ok = writeItems(writer, items, count, memberType, false);
			writer.writeEndElement();
			if (!ok)
			{
				return false;
			}
		}
		else if (member.isOptional)
		{
			const void* field = *reinterpret_cast<void* const*>(position);
			position += sizeof(void*);
			if (field && !writeElement(writer, name, field, memberType))
			{
				return false;
			}
		}
		else
		{
			if (!writeElement(writer, name, reinterpret_cast<const void*>(position), memberType))
			{
				return false;
			}
			position += memberType->memSize;
		}
	}
	return true;
}

///
/// \brief Writes the content of an element holding \a data, as OPC UA Part 6 defines the XML encoding.
/// \return False for data types without XML encoding: DataValue, DiagnosticInfo, Decimal and bit field clusters.
///
bool writeContent(QXmlStreamWriter& writer, const void* data, const UA_DataType* type)
{
	switch (type->typeKind)
	{
	case UA_DATATYPEKIND_BOOLEAN:
		writer.writeCharacters(boolText(*static_cast<const UA_Boolean*>(data)));
		return true;
	case UA_DATATYPEKIND_SBYTE:
		writer.writeCharacters(QString::number(*static_cast<const UA_SByte*>(data)));
		return true;
	case UA_DATATYPEKIND_BYTE:
		writer.writeCharacters(QString::number(*static_cast<const UA_Byte*>(data)));
		return true;
	case UA_DATATYPEKIND_INT16:
		writer.writeCharacters(QString::number(*static_cast<const UA_Int16*>(data)));
		return true;
	case UA_DATATYPEKIND_UINT16:
		writer.writeCharacters(QString::number(*static_cast<const UA_UInt16*>(data)));
		return true;
	case UA_DATATYPEKIND_INT32:
	case UA_DATATYPEKIND_ENUM:
		writer.writeCharacters(QString::number(*static_cast<const UA_Int32*>(data)));
		return true;
	case UA_DATATYPEKIND_UINT32:
		writer.writeCharacters(QString::number(*static_cast<const UA_UInt32*>(data)));
		return true;
	case UA_DATATYPEKIND_INT64:
		writer.writeCharacters(QString::number(*static_cast<const UA_Int64*>(data)));
		return true;
	case UA_DATATYPEKIND_UINT64:
		writer.writeCharacters(QString::number(*static_cast<const UA_UInt64*>(data)));
		return true;
	case UA_DATATYPEKIND_FLOAT:
		writer.writeCharacters(floatText(*static_cast<const UA_Float*>(data), 9));
		return true;
	case UA_DATATYPEKIND_DOUBLE:
		writer.writeCharacters(floatText(*static_cast<const UA_Double*>(data), 17));
		return true;
	case UA_DATATYPEKIND_STRING:
	case UA_DATATYPEKIND_XMLELEMENT:
		writer.writeCharacters(stringText(*static_cast<const UA_String*>(data)));
		return true;
	case UA_DATATYPEKIND_DATETIME:
	{
		const QDateTime time = QUaTypesConverter::uaVariantToQVariantScalar<QDateTime, UA_DateTime>(
			static_cast<const UA_DateTime*>(data));
		writer.writeCharacters(time.toUTC().toString(Qt::ISODateWithMs));
		return true;
	}
	case UA_DATATYPEKIND_GUID:
	{
		const auto* guid = static_cast<const UA_Guid*>(data);
		const QUuid uuid(guid->data1, guid->data2, guid->data3, guid->data4[0], guid->data4[1], guid->data4[2],
		                 guid->data4[3], guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
		writer.writeTextElement(QStringLiteral("String"), uuid.toString(QUuid::WithoutBraces));
		return true;
	}
	case UA_DATATYPEKIND_BYTESTRING:
	{
		const auto* bytes = static_cast<const UA_ByteString*>(data);
		writer.writeCharacters(QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes->data),
		                                                      static_cast<qsizetype>(bytes->length)).toBase64()));
		return true;
	}
	case UA_DATATYPEKIND_NODEID:
		writer.writeTextElement(QStringLiteral("Identifier"),
		                        QUaTypesConverter::nodeIdToQString(*static_cast<const UA_NodeId*>(data)));
		return true;
	case UA_DATATYPEKIND_EXPANDEDNODEID:
		writer.writeTextElement(QStringLiteral("Identifier"),
		                        QUaTypesConverter::nodeIdToQString(static_cast<const UA_ExpandedNodeId*>(data)->nodeId));
		return true;
	case UA_DATATYPEKIND_STATUSCODE:
		writer.writeTextElement(QStringLiteral("Code"), QString::number(*static_cast<const UA_StatusCode*>(data)));
		return true;
	case UA_DATATYPEKIND_QUALIFIEDNAME:
	{
		const auto* name = static_cast<const UA_QualifiedName*>(data);
		writer.writeTextElement(QStringLiteral("NamespaceIndex"), QString::number(name->namespaceIndex));
		writer.writeTextElement(QStringLiteral("Name"), stringText(name->name));
		return true;
	}
	case UA_DATATYPEKIND_LOCALIZEDTEXT:
	{
		const auto* text = static_cast<const UA_LocalizedText*>(data);
		writer.writeTextElement(QStringLiteral("Locale"), stringText(text->locale));
		writer.writeTextElement(QStringLiteral("Text"), stringText(text->text));
		return true;
	}
	case UA_DATATYPEKIND_EXTENSIONOBJECT:
	{
		const auto* object = static_cast<const UA_ExtensionObject*>(data);
		if (object->encoding == UA_EXTENSIONOBJECT_ENCODED_NOBODY)
		{
			return true;
		}
		if (object->encoding == UA_EXTENSIONOBJECT_DECODED || object->encoding == UA_EXTENSIONOBJECT_DECODED_NODELETE)
		{
			const UA_DataType* bodyType = object->content.decoded.type;
			writer.writeStartElement(QStringLiteral("TypeId"));
			writer.writeTextElement(QStringLiteral("Identifier"), QUaTypesConverter::nodeIdToQString(
				UA_NodeId_isNull(&bodyType->xmlEncodingId) ? bodyType->typeId : bodyType->xmlEncodingId));
			writer.writeEndElement();
			writer.writeStartElement(QStringLiteral("Body"));
			const bool ok = writeElement(writer, typeName(bodyType), object->content.decoded.data, bodyType);
			writer.writeEndElement();
			return ok;
		}
		if (object->encoding != UA_EXTENSIONOBJECT_ENCODED_BYTESTRING)
		{
			return false;
		}
		writer.writeStartElement(QStringLiteral("TypeId"));
		writer.writeTextElement(QStringLiteral("Identifier"), QUaTypesConverter::nodeIdToQString(object->content.encoded.typeId));
		writer.writeEndElement();
		writer.writeStartElement(QStringLiteral("Body"));
		const bool ok = writeElement(writer, QStringLiteral("ByteString"), &object->content.encoded.body,
		                             &UA_TYPES[UA_TYPES_BYTESTRING]);
		writer.writeEndElement();
		return ok;
	}
	case UA_DATATYPEKIND_STRUCTURE:
	case UA_DATATYPEKIND_OPTSTRUCT:
	case UA_DATATYPEKIND_UNION:
		return writeStructure(writer, data, type);
	case UA_DATATYPEKIND_VARIANT:
	{
		const auto* value = static_cast<const UA_Variant*>(data);
		writer.writeStartElement(QStringLiteral("Value"));
		const bool ok = !value->type || writeVariant(writer, *value);
		writer.writeEndElement();
		return ok;
	}
	default:
		return false;
	}
}

///
/// \brief Writes the value of a variable as the content of its NodeSet2 Value element: a scalar, a ListOf array or a
///        Matrix; structures are wrapped in ExtensionObjects.
/// \return False when a data type of the value cannot be encoded.
///
bool writeVariant(QXmlStreamWriter& writer, const UA_Variant& value)
{
	const UA_DataType* type = value.type;
	const bool structure = type->typeKind > UA_DATATYPEKIND_DIAGNOSTICINFO && type->typeKind != UA_DATATYPEKIND_ENUM;
	if (UA_Variant_isScalar(&value))
	{
		if (!structure)
		{
			const QString name = type->typeKind == UA_DATATYPEKIND_ENUM ? QStringLiteral("Int32") : typeName(type);
			return writeElement(writer, name, value.data, type);
		}
		UA_ExtensionObject wrapper;
		UA_ExtensionObject_setValue(&wrapper, value.data, type);
		return writeElement(writer, QStringLiteral("ExtensionObject"), &wrapper, &UA_TYPES[UA_TYPES_EXTENSIONOBJECT]);
	}
	if (value.arrayDimensionsSize > 1)
	{
		writer.writeStartElement(QStringLiteral("Matrix"));
		writer.writeStartElement(QStringLiteral("Dimensions"));
		for (size_t i = 0; i < value.arrayDimensionsSize; i++)
		{
			writer.writeTextElement(QStringLiteral("Int32"), QString::number(value.arrayDimensions[i]));
		}
		writer.writeEndElement();
		writer.writeStartElement(QStringLiteral("Elements"));
		const bool ok = writeItems(writer, value.data, value.arrayLength, type, true);
		writer.writeEndElement();
		writer.writeEndElement();
		return ok;
	}
	const QString itemName = structure ? QStringLiteral("ExtensionObject") :
	                         type->typeKind == UA_DATATYPEKIND_ENUM ? QStringLiteral("Int32") : typeName(type);
	writer.writeStartElement(QStringLiteral("ListOf") + itemName);
	const bool ok = writeItems(writer, value.data, value.arrayLength, type, true);
	writer.writeEndElement();
	return ok;
}

} // namespace

///
/// \brief Prepares an export of the address space of \a server.
///
QUaNodeSetWriter::QUaNodeSetWriter(const QUaServer* server)
	: _server(server)
	, _uaServer(server->_server)
{
}

///
/// \brief Writes the nodes of \a namespaceUris, all but namespace 0 when empty, as NodeSet2 XML to \a device.
///
QUaNodeSetExportResult QUaNodeSetWriter::write(QIODevice* device, const QStringList& namespaceUris)
{
	if (!device || !device->isWritable())
	{
		_result.errorString = QStringLiteral("The NodeSet device is not writable");
		return _result;
	}
	if (!this->selectNamespaces(namespaceUris))
	{
		return _result;
	}
	this->findHierarchicalReferenceTypes();
	this->collectNodes();
	QList<Node> nodes;
	for (const QUaNodeId& nodeId : std::as_const(_order))
	{
		UaNodeId id(nodeId);
		Node node = this->describeNode(id.id);
		if (!node.element.isEmpty())
		{
			nodes << node;
			_result.exportedNodes << nodeId;
		}
	}
	QXmlStreamWriter writer(device);
	writer.setAutoFormatting(true);
	this->writeDocument(writer, nodes);
	if (writer.hasError())
	{
		_result.errorString = QStringLiteral("Cannot write the NodeSet: %1").arg(device->errorString());
		_result.exportedNodes.clear();
	}
	return _result;
}

///
/// \brief Resolves the namespaces to export to their indexes.
/// \return False, with the error set, when a URI is not registered or names namespace 0.
///
bool QUaNodeSetWriter::selectNamespaces(const QStringList& namespaceUris)
{
	_serverNamespaces = _server->namespaces();
	if (namespaceUris.isEmpty())
	{
		for (qsizetype index = 1; index < _serverNamespaces.size(); index++)
		{
			_namespaces.insert(static_cast<quint16>(index));
		}
		return true;
	}
	for (const QString& uri : namespaceUris)
	{
		const qsizetype index = _serverNamespaces.indexOf(uri);
		if (index <= 0)
		{
			_result.errorString = index == 0 ?
				QStringLiteral("Namespace 0 cannot be exported") :
				QStringLiteral("Namespace %1 is not registered").arg(uri);
			return false;
		}
		_namespaces.insert(static_cast<quint16>(index));
	}
	return true;
}

///
/// \brief Collects HierarchicalReferences and all its subtypes.
///
void QUaNodeSetWriter::findHierarchicalReferenceTypes()
{
	const UA_NodeId hasSubtype = UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE);
	QList<QUaNodeId> pending { QUaNodeId(0, quint32(UA_NS0ID_HIERARCHICALREFERENCES)) };
	while (!pending.isEmpty())
	{
		const QUaNodeId typeId = pending.takeFirst();
		if (_hierarchicalReferenceTypes.contains(typeId))
		{
			continue;
		}
		_hierarchicalReferenceTypes.insert(typeId);
		UaNodeId id(typeId);
		const auto references = this->browse(id.id, UA_BROWSEDIRECTION_FORWARD, hasSubtype);
		for (const auto& reference : references)
		{
			pending << QUaNodeId(reference.nodeId.nodeId);
		}
	}
}

///
/// \brief Returns copies of the references of a node; \a referenceTypeId null returns all reference types.
///        The caller owns the returned descriptions and must clear them.
///
QList<UA_ReferenceDescription> QUaNodeSetWriter::browse(const UA_NodeId& nodeId, UA_BrowseDirection direction,
                                                        const UA_NodeId& referenceTypeId) const
{
	UA_BrowseDescription description;
	UA_BrowseDescription_init(&description);
	description.nodeId          = nodeId;
	description.browseDirection = direction;
	description.referenceTypeId = referenceTypeId;
	description.includeSubtypes = !UA_NodeId_isNull(&referenceTypeId);
	description.resultMask      = UA_BROWSERESULTMASK_REFERENCETYPEID | UA_BROWSERESULTMASK_ISFORWARD;
	UA_BrowseResult result = UA_Server_browse(_uaServer, 0, &description);
	QList<UA_ReferenceDescription> references;
	for (size_t i = 0; i < result.referencesSize; i++)
	{
		if (result.references[i].nodeId.serverIndex != 0)
		{
			continue;
		}
		UA_ReferenceDescription copy;
		UA_ReferenceDescription_copy(&result.references[i], &copy);
		references << copy;
	}
	UA_BrowseResult_clear(&result);
	return references;
}

///
/// \brief Finds the nodes to export: everything of the exported namespaces reachable from the Root folder,
///        following hierarchical references through other namespaces and any reference, in both directions, inside
///        exported ones. The inverse direction finds nodes nothing points to, such as DesignToolOnly instances.
///
void QUaNodeSetWriter::collectNodes()
{
	const UA_NodeId hierarchical = UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES);
	QList<QUaNodeId> pending { QUaNodeId(0, quint32(UA_NS0ID_ROOTFOLDER)) };
	QSet<QUaNodeId> visited;
	while (!pending.isEmpty())
	{
		const QUaNodeId nodeId = pending.takeFirst();
		if (visited.contains(nodeId))
		{
			continue;
		}
		visited.insert(nodeId);
		UaNodeId id(nodeId);
		const bool exported = this->isExported(id.id);
		if (exported)
		{
			_order << nodeId;
			_exported.insert(nodeId);
		}
		auto references = exported ? this->browse(id.id, UA_BROWSEDIRECTION_BOTH, UA_NODEID_NULL) :
		                             this->browse(id.id, UA_BROWSEDIRECTION_FORWARD, hierarchical);
		for (auto& reference : references)
		{
			if (reference.isForward || this->isExported(reference.nodeId.nodeId))
			{
				pending << QUaNodeId(reference.nodeId.nodeId);
			}
			UA_ReferenceDescription_clear(&reference);
		}
	}
}

///
/// \brief Tells whether a node belongs to an exported namespace.
///
bool QUaNodeSetWriter::isExported(const UA_NodeId& nodeId) const
{
	return _namespaces.contains(nodeId.namespaceIndex);
}

///
/// \brief Reads one attribute of a node.
/// \return False when the node has no such attribute.
///
bool QUaNodeSetWriter::readAttribute(const UA_NodeId& nodeId, UA_AttributeId attributeId, UA_Variant& value) const
{
	UA_ReadValueId readValueId;
	UA_ReadValueId_init(&readValueId);
	readValueId.nodeId      = nodeId;
	readValueId.attributeId = attributeId;
	UA_DataValue dataValue = UA_Server_read(_uaServer, &readValueId, UA_TIMESTAMPSTORETURN_NEITHER);
	const bool ok = dataValue.hasValue && (!dataValue.hasStatus || dataValue.status == UA_STATUSCODE_GOOD);
	if (ok)
	{
		value = dataValue.value;
		UA_Variant_init(&dataValue.value);
	}
	UA_DataValue_clear(&dataValue);
	return ok;
}

///
/// \brief Reads the attributes, references, value and definition of a node.
/// \return The node, with an empty element when its class cannot be exported.
///
QUaNodeSetWriter::Node QUaNodeSetWriter::describeNode(const UA_NodeId& nodeId)
{
	Node node;
	UaVariant nodeClass;
	this->readAttribute(nodeId, UA_ATTRIBUTEID_NODECLASS, nodeClass.value);
	const UA_NodeClass* classValue = nodeClass.scalar<UA_NodeClass>(&UA_TYPES[UA_TYPES_NODECLASS]);
	node.element = classValue ? elementOfNodeClass(*classValue) : QString();
	if (node.element.isEmpty())
	{
		this->warn(nodeId, QStringLiteral("skipped, its node class cannot be exported"));
		return node;
	}
	const UA_NodeClass cls = *classValue;
	const bool isType = cls == UA_NODECLASS_OBJECTTYPE || cls == UA_NODECLASS_VARIABLETYPE ||
	                    cls == UA_NODECLASS_REFERENCETYPE || cls == UA_NODECLASS_DATATYPE;

	node.attributes << qMakePair(QStringLiteral("NodeId"), this->nodeIdText(nodeId));
	UaVariant browseName;
	if (this->readAttribute(nodeId, UA_ATTRIBUTEID_BROWSENAME, browseName.value))
	{
		if (const auto* name = browseName.scalar<UA_QualifiedName>(&UA_TYPES[UA_TYPES_QUALIFIEDNAME]))
		{
			node.attributes << qMakePair(QStringLiteral("BrowseName"), this->browseNameText(*name));
		}
	}
	UaVariant displayName;
	if (this->readAttribute(nodeId, UA_ATTRIBUTEID_DISPLAYNAME, displayName.value))
	{
		if (const auto* text = displayName.scalar<UA_LocalizedText>(&UA_TYPES[UA_TYPES_LOCALIZEDTEXT]))
		{
			node.displayName << QUaLocalizedText(*text);
		}
	}
	UaVariant description;
	if (this->readAttribute(nodeId, UA_ATTRIBUTEID_DESCRIPTION, description.value))
	{
		if (const auto* text = description.scalar<UA_LocalizedText>(&UA_TYPES[UA_TYPES_LOCALIZEDTEXT]))
		{
			node.description = QUaLocalizedText(*text);
		}
	}

	if (isType)
	{
		UaVariant isAbstract;
		this->readAttribute(nodeId, UA_ATTRIBUTEID_ISABSTRACT, isAbstract.value);
		const auto* abstractValue = isAbstract.scalar<UA_Boolean>(&UA_TYPES[UA_TYPES_BOOLEAN]);
		if (abstractValue && *abstractValue)
		{
			node.attributes << qMakePair(QStringLiteral("IsAbstract"), boolText(true));
		}
	}
	switch (cls)
	{
	case UA_NODECLASS_OBJECT:
	case UA_NODECLASS_VIEW:
	{
		UaVariant eventNotifier;
		this->readAttribute(nodeId, UA_ATTRIBUTEID_EVENTNOTIFIER, eventNotifier.value);
		const auto* notifier = eventNotifier.scalar<UA_Byte>(&UA_TYPES[UA_TYPES_BYTE]);
		if (notifier && *notifier != 0)
		{
			node.attributes << qMakePair(QStringLiteral("EventNotifier"), QString::number(*notifier));
		}
		break;
	}
	case UA_NODECLASS_VARIABLE:
	case UA_NODECLASS_VARIABLETYPE:
		this->describeVariable(nodeId, cls == UA_NODECLASS_VARIABLETYPE, node);
		break;
	case UA_NODECLASS_METHOD:
	{
		UaVariant executable;
		this->readAttribute(nodeId, UA_ATTRIBUTEID_EXECUTABLE, executable.value);
		const auto* value = executable.scalar<UA_Boolean>(&UA_TYPES[UA_TYPES_BOOLEAN]);
		if (value && !*value)
		{
			node.attributes << qMakePair(QStringLiteral("Executable"), boolText(false));
		}
		break;
	}
	case UA_NODECLASS_REFERENCETYPE:
	{
		UaVariant symmetric;
		this->readAttribute(nodeId, UA_ATTRIBUTEID_SYMMETRIC, symmetric.value);
		const auto* value = symmetric.scalar<UA_Boolean>(&UA_TYPES[UA_TYPES_BOOLEAN]);
		if (value && *value)
		{
			node.attributes << qMakePair(QStringLiteral("Symmetric"), boolText(true));
		}
		UaVariant inverseName;
		this->readAttribute(nodeId, UA_ATTRIBUTEID_INVERSENAME, inverseName.value);
		if (const auto* text = inverseName.scalar<UA_LocalizedText>(&UA_TYPES[UA_TYPES_LOCALIZEDTEXT]))
		{
			node.inverseName = QUaLocalizedText(*text);
		}
		break;
	}
	case UA_NODECLASS_DATATYPE:
		this->describeDefinition(nodeId, node);
		break;
	default:
		break;
	}
	this->describeReferences(nodeId, isType, node);
	return node;
}

///
/// \brief Lists the references of a node: all forward ones, the inverse one to its parent, and inverse ones from
///        nodes that are not exported, which would be lost otherwise. Sets the ParentNodeId of instances.
///
void QUaNodeSetWriter::describeReferences(const UA_NodeId& nodeId, bool isType, Node& node)
{
	auto references = this->browse(nodeId, UA_BROWSEDIRECTION_BOTH, UA_NODEID_NULL);
	bool hasParent = false;
	for (auto& reference : references)
	{
		const QUaNodeId referenceTypeId(reference.referenceTypeId);
		const QUaNodeId targetId(reference.nodeId.nodeId);
		const bool isParent = !reference.isForward && !hasParent &&
			(isType ? referenceTypeId == QUaNodeId(0, quint32(UA_NS0ID_HASSUBTYPE)) :
			          _hierarchicalReferenceTypes.contains(referenceTypeId));
		if (reference.isForward || isParent || !_exported.contains(targetId))
		{
			node.references << Reference { this->nodeIdText(reference.referenceTypeId),
			                               this->nodeIdText(reference.nodeId.nodeId),
			                               static_cast<bool>(reference.isForward) };
		}
		if (isParent)
		{
			hasParent = true;
			if (!isType)
			{
				node.attributes << qMakePair(QStringLiteral("ParentNodeId"), this->nodeIdText(reference.nodeId.nodeId));
			}
		}
		UA_ReferenceDescription_clear(&reference);
	}
}

///
/// \brief Reads the data type, value rank, array dimensions, access and value of a variable or variable type.
///
void QUaNodeSetWriter::describeVariable(const UA_NodeId& nodeId, bool isType, Node& node)
{
	UaVariant dataType;
	if (this->readAttribute(nodeId, UA_ATTRIBUTEID_DATATYPE, dataType.value))
	{
		if (const auto* id = dataType.scalar<UA_NodeId>(&UA_TYPES[UA_TYPES_NODEID]))
		{
			node.attributes << qMakePair(QStringLiteral("DataType"), this->nodeIdText(*id));
		}
	}
	UaVariant valueRank;
	this->readAttribute(nodeId, UA_ATTRIBUTEID_VALUERANK, valueRank.value);
	const auto* rank = valueRank.scalar<UA_Int32>(&UA_TYPES[UA_TYPES_INT32]);
	if (rank && *rank != UA_VALUERANK_SCALAR)
	{
		node.attributes << qMakePair(QStringLiteral("ValueRank"), QString::number(*rank));
	}
	UaVariant arrayDimensions;
	if (this->readAttribute(nodeId, UA_ATTRIBUTEID_ARRAYDIMENSIONS, arrayDimensions.value) &&
	    arrayDimensions.value.type == &UA_TYPES[UA_TYPES_UINT32] && arrayDimensions.value.arrayLength > 0)
	{
		QStringList dimensions;
		const auto* values = static_cast<const UA_UInt32*>(arrayDimensions.value.data);
		for (size_t i = 0; i < arrayDimensions.value.arrayLength; i++)
		{
			dimensions << QString::number(values[i]);
		}
		node.attributes << qMakePair(QStringLiteral("ArrayDimensions"), dimensions.join(QLatin1Char(',')));
	}
	if (!isType)
	{
		UaVariant access;
		this->readAttribute(nodeId, UA_ATTRIBUTEID_ACCESSLEVEL, access.value);
		const auto* level = access.scalar<UA_Byte>(&UA_TYPES[UA_TYPES_BYTE]);
		if (level && *level != UA_ACCESSLEVELMASK_READ)
		{
			node.attributes << qMakePair(QStringLiteral("AccessLevel"), QString::number(*level));
		}
		UaVariant interval;
		this->readAttribute(nodeId, UA_ATTRIBUTEID_MINIMUMSAMPLINGINTERVAL, interval.value);
		const auto* sampling = interval.scalar<UA_Double>(&UA_TYPES[UA_TYPES_DOUBLE]);
		if (sampling && *sampling != 0.0)
		{
			node.attributes << qMakePair(QStringLiteral("MinimumSamplingInterval"), QString::number(*sampling));
		}
		UaVariant historizing;
		this->readAttribute(nodeId, UA_ATTRIBUTEID_HISTORIZING, historizing.value);
		const auto* history = historizing.scalar<UA_Boolean>(&UA_TYPES[UA_TYPES_BOOLEAN]);
		if (history && *history)
		{
			node.attributes << qMakePair(QStringLiteral("Historizing"), boolText(true));
		}
	}
	UaVariant value;
	if (this->readAttribute(nodeId, UA_ATTRIBUTEID_VALUE, value.value) && value.value.type)
	{
		node.value = this->encodeValue(value.value, nodeId);
	}
}

///
/// \brief Reads the fields of a structure or the values of an enumeration from the DataTypeDefinition attribute.
///
void QUaNodeSetWriter::describeDefinition(const UA_NodeId& nodeId, Node& node)
{
	UaVariant definition;
	if (!this->readAttribute(nodeId, UA_ATTRIBUTEID_DATATYPEDEFINITION, definition.value))
	{
		return;
	}
	UaVariant browseName;
	this->readAttribute(nodeId, UA_ATTRIBUTEID_BROWSENAME, browseName.value);
	if (const auto* name = browseName.scalar<UA_QualifiedName>(&UA_TYPES[UA_TYPES_QUALIFIEDNAME]))
	{
		node.definitionName = this->browseNameText(*name);
	}
	if (const auto* structure = definition.scalar<UA_StructureDefinition>(&UA_TYPES[UA_TYPES_STRUCTUREDEFINITION]))
	{
		node.hasDefinition = true;
		node.isUnion = structure->structureType == UA_STRUCTURETYPE_UNION;
		for (size_t i = 0; i < structure->fieldsSize; i++)
		{
			const UA_StructureField& source = structure->fields[i];
			Field field;
			field.attributes << qMakePair(QStringLiteral("Name"), QUaTypesConverter::uaStringToQString(source.name));
			field.attributes << qMakePair(QStringLiteral("DataType"), this->nodeIdText(source.dataType));
			if (source.valueRank != UA_VALUERANK_SCALAR)
			{
				field.attributes << qMakePair(QStringLiteral("ValueRank"), QString::number(source.valueRank));
			}
			if (source.isOptional)
			{
				field.attributes << qMakePair(QStringLiteral("IsOptional"), boolText(true));
			}
			field.description = QUaLocalizedText(source.description);
			node.definition << field;
		}
	}
	else if (const auto* enumeration = definition.scalar<UA_EnumDefinition>(&UA_TYPES[UA_TYPES_ENUMDEFINITION]))
	{
		node.hasDefinition = true;
		for (size_t i = 0; i < enumeration->fieldsSize; i++)
		{
			const UA_EnumField& source = enumeration->fields[i];
			Field field;
			field.attributes << qMakePair(QStringLiteral("Name"), QUaTypesConverter::uaStringToQString(source.name));
			field.attributes << qMakePair(QStringLiteral("Value"), QString::number(source.value));
			field.description = QUaLocalizedText(source.description);
			node.definition << field;
		}
	}
}

///
/// \brief Formats a NodeId as NodeSet2 expects it; namespace 0 data and reference types get an alias.
///
QString QUaNodeSetWriter::nodeIdText(const UA_NodeId& nodeId)
{
	QString text = QUaTypesConverter::nodeIdToQString(nodeId);
	if (nodeId.namespaceIndex != 0)
	{
		return text;
	}
	text.remove(0, QStringLiteral("ns=0;").size());
	UaVariant nodeClass;
	this->readAttribute(nodeId, UA_ATTRIBUTEID_NODECLASS, nodeClass.value);
	const auto* cls = nodeClass.scalar<UA_NodeClass>(&UA_TYPES[UA_TYPES_NODECLASS]);
	if (!cls || (*cls != UA_NODECLASS_DATATYPE && *cls != UA_NODECLASS_REFERENCETYPE))
	{
		return text;
	}
	UaVariant browseName;
	this->readAttribute(nodeId, UA_ATTRIBUTEID_BROWSENAME, browseName.value);
	const auto* name = browseName.scalar<UA_QualifiedName>(&UA_TYPES[UA_TYPES_QUALIFIEDNAME]);
	if (!name)
	{
		return text;
	}
	const QString alias = QUaTypesConverter::uaStringToQString(name->name);
	if (_aliases.value(alias, text) != text)
	{
		return text;
	}
	_aliases.insert(alias, text);
	return alias;
}

///
/// \brief Formats a browse name as "index:name", omitting index 0.
///
QString QUaNodeSetWriter::browseNameText(const UA_QualifiedName& browseName) const
{
	const QString name = QUaTypesConverter::uaStringToQString(browseName.name);
	return browseName.namespaceIndex == 0 ? name : QStringLiteral("%1:%2").arg(browseName.namespaceIndex).arg(name);
}

///
/// \brief Encodes a value as the XML of a NodeSet2 Value element.
/// \return The encoded XML, empty when a data type of the value cannot be encoded (a warning is recorded then).
///
QByteArray QUaNodeSetWriter::encodeValue(const UA_Variant& value, const UA_NodeId& nodeId)
{
	// UA_encodeXml of open62541 1.4 neither escapes text nor encodes structures, so the value is written here
	QByteArray xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartElement(QStringLiteral("Value"));
	const bool ok = writeVariant(writer, value);
	writer.writeEndElement();
	if (!ok)
	{
		this->warn(nodeId, QStringLiteral("value not exported, its data type %1 cannot be encoded as XML")
			.arg(typeName(value.type)));
		return QByteArray();
	}
	QXmlStreamReader check(xml);
	while (!check.atEnd())
	{
		check.readNext();
	}
	if (check.hasError())
	{
		this->warn(nodeId, QStringLiteral("value not exported, its XML is not well-formed: %1").arg(check.errorString()));
		return QByteArray();
	}
	return xml;
}

///
/// \brief Writes the UANodeSet root with the namespace URIs, the aliases and the nodes.
///
void QUaNodeSetWriter::writeDocument(QXmlStreamWriter& writer, const QList<Node>& nodes) const
{
	writer.writeStartDocument();
	writer.writeDefaultNamespace(kNodeSetNamespace);
	writer.writeNamespace(kTypesNamespace, QStringLiteral("uax"));
	writer.writeStartElement(kNodeSetNamespace, QStringLiteral("UANodeSet"));

	writer.writeStartElement(kNodeSetNamespace, QStringLiteral("NamespaceUris"));
	for (qsizetype index = 1; index < _serverNamespaces.size(); index++)
	{
		writer.writeTextElement(kNodeSetNamespace, QStringLiteral("Uri"), _serverNamespaces.at(index));
	}
	writer.writeEndElement();

	writer.writeStartElement(kNodeSetNamespace, QStringLiteral("Aliases"));
	QStringList aliases = _aliases.keys();
	aliases.sort();
	for (const QString& alias : std::as_const(aliases))
	{
		writer.writeStartElement(kNodeSetNamespace, QStringLiteral("Alias"));
		writer.writeAttribute(QStringLiteral("Alias"), alias);
		writer.writeCharacters(_aliases.value(alias));
		writer.writeEndElement();
	}
	writer.writeEndElement();

	for (const Node& node : nodes)
	{
		this->writeNode(writer, node);
	}
	writer.writeEndElement();
	writer.writeEndDocument();
}

///
/// \brief Writes one node element.
///
void QUaNodeSetWriter::writeNode(QXmlStreamWriter& writer, const Node& node) const
{
	writer.writeStartElement(kNodeSetNamespace, node.element);
	for (const auto& attribute : node.attributes)
	{
		writer.writeAttribute(attribute.first, attribute.second);
	}
	for (const QUaLocalizedText& text : node.displayName)
	{
		writeLocalizedText(writer, QStringLiteral("DisplayName"), text);
	}
	if (!node.description.text().isEmpty())
	{
		writeLocalizedText(writer, QStringLiteral("Description"), node.description);
	}
	if (!node.inverseName.text().isEmpty())
	{
		writeLocalizedText(writer, QStringLiteral("InverseName"), node.inverseName);
	}
	writer.writeStartElement(kNodeSetNamespace, QStringLiteral("References"));
	for (const Reference& reference : node.references)
	{
		writer.writeStartElement(kNodeSetNamespace, QStringLiteral("Reference"));
		writer.writeAttribute(QStringLiteral("ReferenceType"), reference.referenceType);
		if (!reference.isForward)
		{
			writer.writeAttribute(QStringLiteral("IsForward"), boolText(false));
		}
		writer.writeCharacters(reference.target);
		writer.writeEndElement();
	}
	writer.writeEndElement();
	if (!node.value.isEmpty())
	{
		writeValue(writer, node.value);
	}
	if (node.hasDefinition)
	{
		writer.writeStartElement(kNodeSetNamespace, QStringLiteral("Definition"));
		writer.writeAttribute(QStringLiteral("Name"), node.definitionName);
		if (node.isUnion)
		{
			writer.writeAttribute(QStringLiteral("IsUnion"), boolText(true));
		}
		for (const Field& field : node.definition)
		{
			writer.writeStartElement(kNodeSetNamespace, QStringLiteral("Field"));
			for (const auto& attribute : field.attributes)
			{
				writer.writeAttribute(attribute.first, attribute.second);
			}
			if (!field.description.text().isEmpty())
			{
				writeLocalizedText(writer, QStringLiteral("Description"), field.description);
			}
			writer.writeEndElement();
		}
		writer.writeEndElement();
	}
	writer.writeEndElement();
}

///
/// \brief Writes a localized text element with its locale, when it has one.
///
void QUaNodeSetWriter::writeLocalizedText(QXmlStreamWriter& writer, const QString& name, const QUaLocalizedText& text)
{
	writer.writeStartElement(kNodeSetNamespace, name);
	if (!text.locale().isEmpty())
	{
		writer.writeAttribute(QStringLiteral("Locale"), text.locale());
	}
	writer.writeCharacters(text.text());
	writer.writeEndElement();
}

///
/// \brief Copies the XML open62541 encoded for a value into a Value element, with the value in the Types namespace.
///
void QUaNodeSetWriter::writeValue(QXmlStreamWriter& writer, const QByteArray& value)
{
	writer.writeStartElement(kNodeSetNamespace, QStringLiteral("Value"));
	QXmlStreamReader reader(value);
	bool root = true;
	int depth = 0;
	while (!reader.atEnd())
	{
		switch (reader.readNext())
		{
		case QXmlStreamReader::StartElement:
			// open62541 wraps the encoded value in a Value element of its own
			if (root && reader.name() == QLatin1String("Value"))
			{
				root = false;
				break;
			}
			root = false;
			writer.writeStartElement(kTypesNamespace, reader.name().toString());
			for (const auto& attribute : reader.attributes())
			{
				writer.writeAttribute(attribute.name().toString(), attribute.value().toString());
			}
			++depth;
			break;
		case QXmlStreamReader::EndElement:
			if (depth > 0)
			{
				writer.writeEndElement();
				--depth;
			}
			break;
		case QXmlStreamReader::Characters:
			if (depth > 0)
			{
				writer.writeCharacters(reader.text().toString());
			}
			break;
		default:
			break;
		}
	}
	writer.writeEndElement();
}

///
/// \brief Records a warning about a node.
///
void QUaNodeSetWriter::warn(const UA_NodeId& nodeId, const QString& message)
{
	_result.warnings << QStringLiteral("%1: %2").arg(QUaTypesConverter::nodeIdToQString(nodeId), message);
}
