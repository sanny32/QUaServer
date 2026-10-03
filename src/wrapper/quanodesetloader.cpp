#include "quanodesetloader.h"

#include <QRegularExpression>
#include <QXmlStreamWriter>

namespace {

const UA_UInt32 kHierarchicalReferences = UA_NS0ID_HIERARCHICALREFERENCES;

///
/// \brief Owns the open62541 copies of the attributes shared by every node class.
///
class UaNodeHead
{
public:
	UaNodeHead(const QUaNodeId& nodeId, const QUaNodeId& parentNodeId, const QUaNodeId& parentReferenceTypeId,
	           const QUaQualifiedName& browseName, const QUaLocalizedText& displayName,
	           const QUaLocalizedText& description)
		: nodeId(nodeId)
		, parentNodeId(parentNodeId)
		, parentReferenceTypeId(parentReferenceTypeId)
		, browseName(browseName)
		, displayName(displayName)
		, description(description)
	{
	}

	~UaNodeHead()
	{
		UA_NodeId_clear(&nodeId);
		UA_NodeId_clear(&parentNodeId);
		UA_NodeId_clear(&parentReferenceTypeId);
		UA_QualifiedName_clear(&browseName);
		UA_LocalizedText_clear(&displayName);
		UA_LocalizedText_clear(&description);
	}

	UaNodeHead(const UaNodeHead&) = delete;
	UaNodeHead& operator=(const UaNodeHead&) = delete;

	UA_NodeId        nodeId;
	UA_NodeId        parentNodeId;
	UA_NodeId        parentReferenceTypeId;
	UA_QualifiedName browseName;
	UA_LocalizedText displayName;
	UA_LocalizedText description;
};

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
/// \brief Maps a NodeSet2 element name to its node class, UA_NODECLASS_UNSPECIFIED for other elements.
///
UA_NodeClass nodeClassFromElement(QStringView name)
{
	if (name == u"UAObject")        { return UA_NODECLASS_OBJECT; }
	if (name == u"UAVariable")      { return UA_NODECLASS_VARIABLE; }
	if (name == u"UAMethod")        { return UA_NODECLASS_METHOD; }
	if (name == u"UAView")          { return UA_NODECLASS_VIEW; }
	if (name == u"UAObjectType")    { return UA_NODECLASS_OBJECTTYPE; }
	if (name == u"UAVariableType")  { return UA_NODECLASS_VARIABLETYPE; }
	if (name == u"UAReferenceType") { return UA_NODECLASS_REFERENCETYPE; }
	if (name == u"UADataType")      { return UA_NODECLASS_DATATYPE; }
	return UA_NODECLASS_UNSPECIFIED;
}

///
/// \brief Tells whether nodes of \a nodeClass hang from their supertype with a HasSubtype reference.
///
bool isTypeNodeClass(UA_NodeClass nodeClass)
{
	return nodeClass == UA_NODECLASS_OBJECTTYPE || nodeClass == UA_NODECLASS_VARIABLETYPE ||
	       nodeClass == UA_NODECLASS_REFERENCETYPE || nodeClass == UA_NODECLASS_DATATYPE;
}

///
/// \brief Tells whether nodes of \a nodeClass are added in two steps, so their constructors run once the whole
///        NodeSet is in place and the children they declare are not instantiated a second time from their type.
///
bool isAddedInTwoSteps(UA_NodeClass nodeClass)
{
	return nodeClass == UA_NODECLASS_OBJECT || nodeClass == UA_NODECLASS_VARIABLE ||
	       nodeClass == UA_NODECLASS_VARIABLETYPE;
}

///
/// \brief Implements the methods of a NodeSet, which open62541 would otherwise answer with BadInternalError.
///
UA_StatusCode notImplementedMethod(UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void*, const UA_NodeId*,
                                   void*, size_t, const UA_Variant*, size_t, UA_Variant*)
{
	return UA_STATUSCODE_BADNOTIMPLEMENTED;
}

///
/// \brief Returns the trimmed value of an XML attribute, or \a defaultValue when it is absent.
///
QString attribute(const QHash<QString, QString>& attributes, const QString& name, const QString& defaultValue)
{
	return attributes.value(name, defaultValue).trimmed();
}

///
/// \brief Reads an xs:boolean XML attribute, or returns \a defaultValue when it is absent.
///
bool boolAttribute(const QHash<QString, QString>& attributes, const QString& name, bool defaultValue)
{
	const QString value = attribute(attributes, name, QString());
	if (value.isEmpty())
	{
		return defaultValue;
	}
	return value == QLatin1String("true") || value == QLatin1String("1");
}

///
/// \brief Parses the comma separated ArrayDimensions attribute.
///
QVector<UA_UInt32> arrayDimensions(const QHash<QString, QString>& attributes)
{
	QVector<UA_UInt32> dimensions;
	const QString value = attribute(attributes, QStringLiteral("ArrayDimensions"), QString());
	const auto parts = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
	for (const auto& part : parts)
	{
		dimensions << part.trimmed().toUInt();
	}
	return dimensions;
}

} // namespace

///
/// \brief Prepares a loader adding nodes to \a server; use one loader per NodeSet.
///
QUaNodeSetLoader::QUaNodeSetLoader(QUaServer* server)
	: _server(server)
	, _uaServer(server->_server)
{
}

///
/// \brief Parses the whole NodeSet before touching the server, so a malformed file adds nothing.
///
QUaNodeSetResult QUaNodeSetLoader::load(QIODevice* device)
{
	if (!device || !device->isReadable())
	{
		_result.errorString = QStringLiteral("The NodeSet device is not readable");
		return _result;
	}
	QXmlStreamReader reader(device);
	if (!this->readNodeSet(reader))
	{
		_result.errorString = QStringLiteral("Invalid NodeSet at line %1, column %2: %3")
			.arg(reader.lineNumber()).arg(reader.columnNumber()).arg(reader.errorString());
		return _result;
	}
	this->mapNamespaces();
	this->resolveNodes();
	const QList<int> sorted = this->sortNodes();
	QSet<int> added;
	for (int index : sorted)
	{
		const Node& node = _nodes.at(index);
		if (_server->isNodeIdUsed(node.nodeId))
		{
			this->warn(node, QStringLiteral("skipped, the node already exists"));
			continue;
		}
		const UA_StatusCode status = this->addNode(node);
		if (status != UA_STATUSCODE_GOOD)
		{
			this->warn(node, QStringLiteral("not added: %1").arg(QString::fromLatin1(UA_StatusCode_name(status))));
			continue;
		}
		added.insert(index);
		_result.addedNodes << node.nodeId;
		if (node.nodeClass == UA_NODECLASS_REFERENCETYPE && !_hierarchicalReferenceTypes.contains(node.nodeId))
		{
			this->registerReferenceType(node);
		}
	}
	for (int index : sorted)
	{
		if (added.contains(index))
		{
			this->addReferences(_nodes.at(index));
		}
	}
	for (int index : sorted)
	{
		const Node& node = _nodes.at(index);
		if (added.contains(index) &&
		    (node.nodeClass == UA_NODECLASS_OBJECTTYPE || node.nodeClass == UA_NODECLASS_VARIABLETYPE))
		{
			UaNodeId typeNodeId(node.nodeId);
			_server->bindNodeSetType(typeNodeId.id);
		}
	}
	this->finishNodes(sorted, added);
	return _result;
}

///
/// \brief Reads the namespaces, aliases and nodes of the UANodeSet root element, skipping the other elements.
/// \return False when the XML is malformed or is not a NodeSet; the reader then holds the error.
///
bool QUaNodeSetLoader::readNodeSet(QXmlStreamReader& reader)
{
	if (!reader.readNextStartElement())
	{
		return false;
	}
	if (reader.name() != u"UANodeSet")
	{
		reader.raiseError(QStringLiteral("The root element is not UANodeSet"));
		return false;
	}
	while (reader.readNextStartElement())
	{
		const QStringView name = reader.name();
		if (name == u"NamespaceUris")
		{
			this->readNamespaceUris(reader);
			continue;
		}
		if (name == u"Aliases")
		{
			this->readAliases(reader);
			continue;
		}
		const UA_NodeClass nodeClass = nodeClassFromElement(name);
		if (nodeClass == UA_NODECLASS_UNSPECIFIED)
		{
			reader.skipCurrentElement();
			continue;
		}
		this->readNode(reader, nodeClass);
	}
	return !reader.hasError();
}

///
/// \brief Reads the NamespaceUris element; its first URI is namespace index 1 of the NodeSet.
///
void QUaNodeSetLoader::readNamespaceUris(QXmlStreamReader& reader)
{
	while (reader.readNextStartElement())
	{
		if (reader.name() == u"Uri")
		{
			_namespaceUris << reader.readElementText().trimmed();
			continue;
		}
		reader.skipCurrentElement();
	}
}

///
/// \brief Reads the Aliases element, which names NodeIds used in attributes and references.
///
void QUaNodeSetLoader::readAliases(QXmlStreamReader& reader)
{
	while (reader.readNextStartElement())
	{
		if (reader.name() == u"Alias")
		{
			const QString alias = reader.attributes().value(QStringLiteral("Alias")).toString();
			_aliases.insert(alias, reader.readElementText().trimmed());
			continue;
		}
		reader.skipCurrentElement();
	}
}

///
/// \brief Reads a node element; only the first DisplayName and Description are kept when several locales are given.
///
void QUaNodeSetLoader::readNode(QXmlStreamReader& reader, UA_NodeClass nodeClass)
{
	Node node;
	node.nodeClass = nodeClass;
	const auto attributes = reader.attributes();
	for (const auto& xmlAttribute : attributes)
	{
		node.attributes.insert(xmlAttribute.name().toString(), xmlAttribute.value().toString());
	}
	while (reader.readNextStartElement())
	{
		const QStringView name = reader.name();
		if (name == u"DisplayName" || name == u"Description" || name == u"InverseName")
		{
			QUaLocalizedText& target = name == u"DisplayName" ? node.displayName :
			                           name == u"Description" ? node.description : node.inverseName;
			const QUaLocalizedText text = QUaNodeSetLoader::readLocalizedText(reader);
			if (target.text().isEmpty())
			{
				target = text;
			}
		}
		else if (name == u"References")
		{
			node.references = QUaNodeSetLoader::readReferences(reader);
		}
		else if (name == u"Value")
		{
			node.value = QUaNodeSetLoader::readValue(reader);
		}
		else
		{
			reader.skipCurrentElement();
		}
	}
	_nodes << node;
}

///
/// \brief Reads a LocalizedText element with its optional Locale attribute.
///
QUaLocalizedText QUaNodeSetLoader::readLocalizedText(QXmlStreamReader& reader)
{
	const QString locale = reader.attributes().value(QStringLiteral("Locale")).toString();
	return QUaLocalizedText(locale, reader.readElementText());
}

///
/// \brief Reads the References element of a node; references are forward unless IsForward is false.
///
QList<QUaNodeSetLoader::Reference> QUaNodeSetLoader::readReferences(QXmlStreamReader& reader)
{
	QList<Reference> references;
	while (reader.readNextStartElement())
	{
		if (reader.name() != u"Reference")
		{
			reader.skipCurrentElement();
			continue;
		}
		Reference reference;
		reference.referenceType = reader.attributes().value(QStringLiteral("ReferenceType")).toString().trimmed();
		reference.isForward     = reader.attributes().value(QStringLiteral("IsForward")).trimmed() != u"false";
		reference.target        = reader.readElementText().trimmed();
		references << reference;
	}
	return references;
}

///
/// \brief Copies a Value element as XML for UA_decodeXml, which ignores namespace prefixes and expects the Value
///        element itself as the content of a Variant.
///
QByteArray QUaNodeSetLoader::readValue(QXmlStreamReader& reader)
{
	QByteArray xml;
	QXmlStreamWriter writer(&xml);
	writer.writeStartElement(QStringLiteral("Value"));
	int depth = 0;
	while (!reader.atEnd())
	{
		switch (reader.readNext())
		{
		case QXmlStreamReader::StartElement:
		{
			writer.writeStartElement(reader.name().toString());
			const auto attributes = reader.attributes();
			for (const auto& xmlAttribute : attributes)
			{
				writer.writeAttribute(xmlAttribute.name().toString(), xmlAttribute.value().toString());
			}
			++depth;
			break;
		}
		case QXmlStreamReader::EndElement:
			writer.writeEndElement();
			if (depth == 0)
			{
				return xml;
			}
			--depth;
			break;
		case QXmlStreamReader::Characters:
			writer.writeCharacters(reader.text().toString());
			break;
		default:
			break;
		}
	}
	return xml;
}

///
/// \brief Adds the NodeSet namespaces to the server; index 0 of the NodeSet is always namespace zero.
///
void QUaNodeSetLoader::mapNamespaces()
{
	_remoteToLocal = { 0 };
	for (const auto& uri : std::as_const(_namespaceUris))
	{
		const QByteArray utf8 = uri.toUtf8();
		_remoteToLocal << UA_Server_addNamespace(_uaServer, utf8.constData());
	}
	const QStringList localUris = _server->namespaces();
	for (const auto& uri : localUris)
	{
		_localNamespaceUris << uri.toUtf8();
	}
	for (auto& utf8 : _localNamespaceUris)
	{
		UA_String uaString;
		uaString.length = static_cast<size_t>(utf8.size());
		uaString.data   = reinterpret_cast<UA_Byte*>(utf8.data());
		_localNamespaceStrings << uaString;
	}
}

///
/// \brief Resolves an alias or a NodeId string of the NodeSet to a server NodeId.
///
bool QUaNodeSetLoader::resolveNodeId(const QString& text, QUaNodeId& nodeId) const
{
	const QString trimmed = text.trimmed();
	QByteArray utf8 = _aliases.value(trimmed, trimmed).toUtf8();
	UA_String uaString;
	uaString.length = static_cast<size_t>(utf8.size());
	uaString.data   = reinterpret_cast<UA_Byte*>(utf8.data());
	UA_NodeId uaNodeId;
	if (utf8.isEmpty() || UA_NodeId_parse(&uaNodeId, uaString) != UA_STATUSCODE_GOOD)
	{
		return false;
	}
	const bool isKnownNamespace = uaNodeId.namespaceIndex < _remoteToLocal.size();
	if (isKnownNamespace)
	{
		uaNodeId.namespaceIndex = _remoteToLocal.at(uaNodeId.namespaceIndex);
		nodeId = uaNodeId;
	}
	UA_NodeId_clear(&uaNodeId);
	return isKnownNamespace;
}

///
/// \brief Resolves a "<namespace index>:<name>" BrowseName of the NodeSet; without the prefix it is in namespace zero.
///
QUaQualifiedName QUaNodeSetLoader::resolveBrowseName(const QString& text) const
{
	static const QRegularExpression prefix(QStringLiteral("^(\\d+):(.*)$"),
	                                       QRegularExpression::DotMatchesEverythingOption);
	const QRegularExpressionMatch match = prefix.match(text);
	if (!match.hasMatch())
	{
		return QUaQualifiedName(0, text);
	}
	const int index = match.captured(1).toInt();
	const quint16 namespaceIndex = index < _remoteToLocal.size() ? _remoteToLocal.at(index) : 0;
	return QUaQualifiedName(namespaceIndex, match.captured(2));
}

///
/// \brief Resolves the NodeIds of all nodes and indexes them; nodes declared twice keep their first declaration.
///
void QUaNodeSetLoader::resolveNodes()
{
	for (int i = 0; i < _nodes.count(); i++)
	{
		Node& node = _nodes[i];
		if (!this->resolveNode(node))
		{
			continue;
		}
		if (_nodeIndexes.contains(node.nodeId))
		{
			this->warn(node, QStringLiteral("skipped, the NodeId is declared twice"));
			node.nodeId.clear();
			continue;
		}
		_nodeIndexes.insert(node.nodeId, i);
	}
	this->findHierarchicalReferenceTypes();
	this->resolveParents();
}

///
/// \brief Resolves the NodeIds of a node and its references, dropping the references that cannot be resolved.
/// \return False, leaving the NodeId null, when the node itself cannot be resolved.
///
bool QUaNodeSetLoader::resolveNode(Node& node)
{
	if (!this->resolveNodeId(node.attributes.value(QStringLiteral("NodeId")), node.nodeId))
	{
		this->warn(node, QStringLiteral("skipped, invalid NodeId"));
		node.nodeId.clear();
		return false;
	}
	node.browseName = this->resolveBrowseName(node.attributes.value(QStringLiteral("BrowseName")));
	if (node.displayName.text().isEmpty())
	{
		node.displayName = QUaLocalizedText(QString(), node.browseName.name());
	}
	static const QUaNodeId hasTypeDefinition(0, quint32(UA_NS0ID_HASTYPEDEFINITION));
	QList<Reference> resolved;
	for (auto& reference : node.references)
	{
		if (!this->resolveNodeId(reference.referenceType, reference.referenceTypeId) ||
		    !this->resolveNodeId(reference.target, reference.targetId))
		{
			this->warn(node, QStringLiteral("reference %1 to %2 ignored, it cannot be resolved")
				.arg(reference.referenceType, reference.target));
			continue;
		}
		if (reference.isForward && reference.referenceTypeId == hasTypeDefinition)
		{
			node.typeDefinitionId = reference.targetId;
		}
		resolved << reference;
	}
	node.references = resolved;
	if (node.nodeClass == UA_NODECLASS_VARIABLE || node.nodeClass == UA_NODECLASS_VARIABLETYPE)
	{
		const QString dataType = attribute(node.attributes, QStringLiteral("DataType"), QStringLiteral("i=24"));
		if (!this->resolveNodeId(dataType, node.dataTypeId))
		{
			this->warn(node, QStringLiteral("unknown DataType %1, BaseDataType is used").arg(dataType));
			node.dataTypeId = QUaNodeId(0, quint32(UA_NS0ID_BASEDATATYPE));
		}
	}
	if (node.typeDefinitionId.isNull() && node.nodeClass == UA_NODECLASS_OBJECT)
	{
		node.typeDefinitionId = QUaNodeId(0, quint32(UA_NS0ID_BASEOBJECTTYPE));
	}
	if (node.typeDefinitionId.isNull() && node.nodeClass == UA_NODECLASS_VARIABLE)
	{
		node.typeDefinitionId = QUaNodeId(0, quint32(UA_NS0ID_BASEDATAVARIABLETYPE));
	}
	return true;
}

///
/// \brief Collects the hierarchical reference types of the server and of the NodeSet, to tell parents apart.
///
void QUaNodeSetLoader::findHierarchicalReferenceTypes()
{
	_hierarchicalReferenceTypes.insert(QUaNodeId(0, quint32(kHierarchicalReferences)));
	UA_BrowseDescription description;
	UA_BrowseDescription_init(&description);
	description.nodeId          = UA_NODEID_NUMERIC(0, kHierarchicalReferences);
	description.referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE);
	description.browseDirection = UA_BROWSEDIRECTION_FORWARD;
	description.includeSubtypes = true;
	size_t resultsSize = 0;
	UA_ExpandedNodeId* results = nullptr;
	if (UA_Server_browseRecursive(_uaServer, &description, &resultsSize, &results) == UA_STATUSCODE_GOOD)
	{
		for (size_t i = 0; i < resultsSize; i++)
		{
			_hierarchicalReferenceTypes.insert(QUaNodeId(results[i].nodeId));
		}
		UA_Array_delete(results, resultsSize, &UA_TYPES[UA_TYPES_EXPANDEDNODEID]);
	}
	static const QUaNodeId hasSubtype(0, quint32(UA_NS0ID_HASSUBTYPE));
	bool isChanged = true;
	while (isChanged)
	{
		isChanged = false;
		for (const auto& node : std::as_const(_nodes))
		{
			if (node.nodeClass != UA_NODECLASS_REFERENCETYPE || node.nodeId.isNull() ||
			    _hierarchicalReferenceTypes.contains(node.nodeId))
			{
				continue;
			}
			for (const auto& reference : node.references)
			{
				if (!reference.isForward && reference.referenceTypeId == hasSubtype &&
				    _hierarchicalReferenceTypes.contains(reference.targetId))
				{
					_hierarchicalReferenceTypes.insert(node.nodeId);
					isChanged = true;
					break;
				}
			}
		}
	}
}

///
/// \brief Picks the parent of every node: types hang from their supertype, other nodes from the source of their
///        first inverse hierarchical reference, or of a forward one declared on another node of the NodeSet.
///
void QUaNodeSetLoader::resolveParents()
{
	static const QUaNodeId hasSubtype(0, quint32(UA_NS0ID_HASSUBTYPE));
	QHash<QUaNodeId, QPair<QUaNodeId, QUaNodeId>> forwardParents;
	for (const auto& node : std::as_const(_nodes))
	{
		if (node.nodeId.isNull())
		{
			continue;
		}
		for (const auto& reference : node.references)
		{
			if (reference.isForward && _hierarchicalReferenceTypes.contains(reference.referenceTypeId) &&
			    !forwardParents.contains(reference.targetId))
			{
				forwardParents.insert(reference.targetId, qMakePair(node.nodeId, reference.referenceTypeId));
			}
		}
	}
	for (auto& node : _nodes)
	{
		if (node.nodeId.isNull())
		{
			continue;
		}
		const bool isType = isTypeNodeClass(node.nodeClass);
		for (const auto& reference : std::as_const(node.references))
		{
			const bool isParent = isType ? reference.referenceTypeId == hasSubtype :
			                               _hierarchicalReferenceTypes.contains(reference.referenceTypeId);
			if (!reference.isForward && isParent)
			{
				node.parentNodeId          = reference.targetId;
				node.parentReferenceTypeId = reference.referenceTypeId;
				break;
			}
		}
		if (node.parentNodeId.isNull() && forwardParents.contains(node.nodeId))
		{
			const auto parent = forwardParents.value(node.nodeId);
			if (!isType || parent.second == hasSubtype)
			{
				node.parentNodeId          = parent.first;
				node.parentReferenceTypeId = parent.second;
			}
		}
	}
}

///
/// \brief Orders the nodes so that parents, types, data types and reference types come before the nodes using them.
///
QList<int> QUaNodeSetLoader::sortNodes() const
{
	QList<int> sorted;
	QVector<char> states(_nodes.count(), 0);
	for (int i = 0; i < _nodes.count(); i++)
	{
		if (!_nodes.at(i).nodeId.isNull())
		{
			this->visitNode(i, states, sorted);
		}
	}
	return sorted;
}

///
/// \brief Appends the dependencies of a node before the node itself; a dependency cycle is broken where it is found.
///
void QUaNodeSetLoader::visitNode(int index, QVector<char>& states, QList<int>& sorted) const
{
	enum : char { Unvisited, Visiting, Visited };
	if (states.at(index) != Unvisited)
	{
		return;
	}
	states[index] = Visiting;
	const Node& node = _nodes.at(index);
	const QUaNodeId dependencies[] = {
		node.parentNodeId, node.parentReferenceTypeId, node.typeDefinitionId, node.dataTypeId
	};
	for (const auto& dependency : dependencies)
	{
		const int dependencyIndex = _nodeIndexes.value(dependency, -1);
		if (dependencyIndex >= 0)
		{
			this->visitNode(dependencyIndex, states, sorted);
		}
	}
	states[index] = Visited;
	sorted << index;
}

///
/// \brief Adds a node with the attributes of its NodeSet element; objects, variables and variable types are only
///        begun, see finishNodes().
///
UA_StatusCode QUaNodeSetLoader::addNode(const Node& node)
{
	const auto& attributes = node.attributes;
	UaNodeHead head(node.nodeId, node.parentNodeId, node.parentReferenceTypeId, node.browseName,
	                node.displayName, node.description);
	const UA_UInt32 writeMask = attribute(attributes, QStringLiteral("WriteMask"), QStringLiteral("0")).toUInt();
	switch (node.nodeClass)
	{
	case UA_NODECLASS_OBJECT:
	{
		UA_ObjectAttributes attr = UA_ObjectAttributes_default;
		attr.displayName   = head.displayName;
		attr.description   = head.description;
		attr.writeMask     = writeMask;
		attr.eventNotifier = static_cast<UA_Byte>(attribute(attributes, QStringLiteral("EventNotifier"), QStringLiteral("0")).toUInt());
		UaNodeId typeDefinitionId(node.typeDefinitionId);
		return UA_Server_addNode_begin(_uaServer, UA_NODECLASS_OBJECT, head.nodeId, head.parentNodeId,
			head.parentReferenceTypeId, head.browseName, typeDefinitionId.id, &attr,
			&UA_TYPES[UA_TYPES_OBJECTATTRIBUTES], nullptr, nullptr);
	}
	case UA_NODECLASS_VARIABLE:
	{
		UA_VariableAttributes attr = UA_VariableAttributes_default;
		attr.displayName = head.displayName;
		attr.description = head.description;
		attr.writeMask   = writeMask;
		UaNodeId dataTypeId(node.dataTypeId);
		attr.dataType    = dataTypeId.id;
		attr.valueRank   = attribute(attributes, QStringLiteral("ValueRank"), QStringLiteral("-1")).toInt();
		QVector<UA_UInt32> dimensions = arrayDimensions(attributes);
		attr.arrayDimensionsSize = static_cast<size_t>(dimensions.size());
		attr.arrayDimensions     = dimensions.data();
		attr.accessLevel     = static_cast<UA_Byte>(attribute(attributes, QStringLiteral("AccessLevel"), QStringLiteral("1")).toUInt());
		attr.userAccessLevel = static_cast<UA_Byte>(attribute(attributes, QStringLiteral("UserAccessLevel"), QStringLiteral("1")).toUInt());
		attr.minimumSamplingInterval = attribute(attributes, QStringLiteral("MinimumSamplingInterval"), QStringLiteral("0")).toDouble();
		attr.historizing = boolAttribute(attributes, QStringLiteral("Historizing"), false);
		this->decodeValue(node, &attr.value);
		UaNodeId typeDefinitionId(node.typeDefinitionId);
		const UA_StatusCode status = UA_Server_addNode_begin(_uaServer, UA_NODECLASS_VARIABLE, head.nodeId,
			head.parentNodeId, head.parentReferenceTypeId, head.browseName, typeDefinitionId.id, &attr,
			&UA_TYPES[UA_TYPES_VARIABLEATTRIBUTES], nullptr, nullptr);
		UA_Variant_clear(&attr.value);
		return status;
	}
	case UA_NODECLASS_VARIABLETYPE:
	{
		UA_VariableTypeAttributes attr = UA_VariableTypeAttributes_default;
		attr.displayName = head.displayName;
		attr.description = head.description;
		attr.writeMask   = writeMask;
		UaNodeId dataTypeId(node.dataTypeId);
		attr.dataType    = dataTypeId.id;
		attr.valueRank   = attribute(attributes, QStringLiteral("ValueRank"), QStringLiteral("-1")).toInt();
		QVector<UA_UInt32> dimensions = arrayDimensions(attributes);
		attr.arrayDimensionsSize = static_cast<size_t>(dimensions.size());
		attr.arrayDimensions     = dimensions.data();
		attr.isAbstract  = boolAttribute(attributes, QStringLiteral("IsAbstract"), false);
		this->decodeValue(node, &attr.value);
		const UA_StatusCode status = UA_Server_addNode_begin(_uaServer, UA_NODECLASS_VARIABLETYPE, head.nodeId,
			head.parentNodeId, head.parentReferenceTypeId, head.browseName, UA_NODEID_NULL, &attr,
			&UA_TYPES[UA_TYPES_VARIABLETYPEATTRIBUTES], nullptr, nullptr);
		UA_Variant_clear(&attr.value);
		return status;
	}
	case UA_NODECLASS_OBJECTTYPE:
	{
		UA_ObjectTypeAttributes attr = UA_ObjectTypeAttributes_default;
		attr.displayName = head.displayName;
		attr.description = head.description;
		attr.writeMask   = writeMask;
		attr.isAbstract  = boolAttribute(attributes, QStringLiteral("IsAbstract"), false);
		return UA_Server_addObjectTypeNode(_uaServer, head.nodeId, head.parentNodeId, head.parentReferenceTypeId,
			head.browseName, attr, nullptr, nullptr);
	}
	case UA_NODECLASS_REFERENCETYPE:
	{
		UA_ReferenceTypeAttributes attr = UA_ReferenceTypeAttributes_default;
		attr.displayName = head.displayName;
		attr.description = head.description;
		attr.writeMask   = writeMask;
		attr.isAbstract  = boolAttribute(attributes, QStringLiteral("IsAbstract"), false);
		attr.symmetric   = boolAttribute(attributes, QStringLiteral("Symmetric"), false);
		attr.inverseName = node.inverseName;
		const UA_StatusCode status = UA_Server_addReferenceTypeNode(_uaServer, head.nodeId, head.parentNodeId,
			head.parentReferenceTypeId, head.browseName, attr, nullptr, nullptr);
		UA_LocalizedText_clear(&attr.inverseName);
		return status;
	}
	case UA_NODECLASS_DATATYPE:
	{
		UA_DataTypeAttributes attr = UA_DataTypeAttributes_default;
		attr.displayName = head.displayName;
		attr.description = head.description;
		attr.writeMask   = writeMask;
		attr.isAbstract  = boolAttribute(attributes, QStringLiteral("IsAbstract"), false);
		return UA_Server_addDataTypeNode(_uaServer, head.nodeId, head.parentNodeId, head.parentReferenceTypeId,
			head.browseName, attr, nullptr, nullptr);
	}
	case UA_NODECLASS_METHOD:
	{
		UA_MethodAttributes attr = UA_MethodAttributes_default;
		attr.displayName    = head.displayName;
		attr.description    = head.description;
		attr.writeMask      = writeMask;
		attr.executable     = boolAttribute(attributes, QStringLiteral("Executable"), true);
		attr.userExecutable = boolAttribute(attributes, QStringLiteral("UserExecutable"), true);
		return UA_Server_addMethodNode(_uaServer, head.nodeId, head.parentNodeId, head.parentReferenceTypeId,
			head.browseName, attr, &notImplementedMethod, 0, nullptr, 0, nullptr, nullptr, nullptr);
	}
	case UA_NODECLASS_VIEW:
	{
		UA_ViewAttributes attr = UA_ViewAttributes_default;
		attr.displayName     = head.displayName;
		attr.description     = head.description;
		attr.writeMask       = writeMask;
		attr.containsNoLoops = boolAttribute(attributes, QStringLiteral("ContainsNoLoops"), false);
		attr.eventNotifier   = static_cast<UA_Byte>(attribute(attributes, QStringLiteral("EventNotifier"), QStringLiteral("0")).toUInt());
		return UA_Server_addViewNode(_uaServer, head.nodeId, head.parentNodeId, head.parentReferenceTypeId,
			head.browseName, attr, nullptr, nullptr);
	}
	default:
		return UA_STATUSCODE_BADNODECLASSINVALID;
	}
}

///
/// \brief Makes a non-hierarchical reference type usable with QUaNode::addReference() and findReferences(), named by
///        its BrowseName and InverseName.
///
void QUaNodeSetLoader::registerReferenceType(const Node& node)
{
	const QUaReferenceType referenceType{ node.browseName.name(), node.inverseName.text() };
	if (!_server->_hashRefTypes.contains(referenceType))
	{
		_server->_hashRefTypes.insert(referenceType, node.nodeId);
	}
}

///
/// \brief Adds the references declared on a node; most are declared on both ends, so duplicates are expected.
///
void QUaNodeSetLoader::addReferences(const Node& node)
{
	UaNodeId sourceId(node.nodeId);
	for (const auto& reference : node.references)
	{
		UaNodeId referenceTypeId(reference.referenceTypeId);
		UA_ExpandedNodeId targetId;
		UA_ExpandedNodeId_init(&targetId);
		targetId.nodeId = reference.targetId;
		const UA_StatusCode status = UA_Server_addReference(_uaServer, sourceId.id, referenceTypeId.id, targetId,
			reference.isForward);
		UA_ExpandedNodeId_clear(&targetId);
		if (status != UA_STATUSCODE_GOOD && status != UA_STATUSCODE_BADDUPLICATEREFERENCENOTALLOWED)
		{
			this->warn(node, QStringLiteral("reference %1 to %2 not added: %3")
				.arg(reference.referenceType, reference.target, QString::fromLatin1(UA_StatusCode_name(status))));
		}
	}
}

///
/// \brief Finishes the begun nodes: variable types first, then instances from the leaves up, so every child has
///        its C++ instance and its own mandatory children before the parent constructor binds it.
///
void QUaNodeSetLoader::finishNodes(const QList<int>& sorted, const QSet<int>& added)
{
	QList<int> order;
	for (int index : sorted)
	{
		if (added.contains(index) && _nodes.at(index).nodeClass == UA_NODECLASS_VARIABLETYPE)
		{
			order << index;
		}
	}
	for (auto it = sorted.crbegin(); it != sorted.crend(); ++it)
	{
		const UA_NodeClass nodeClass = _nodes.at(*it).nodeClass;
		if (added.contains(*it) && isAddedInTwoSteps(nodeClass) && nodeClass != UA_NODECLASS_VARIABLETYPE)
		{
			order << *it;
		}
	}
	for (int index : std::as_const(order))
	{
		const Node& node = _nodes.at(index);
		UaNodeId nodeId(node.nodeId);
		const UA_StatusCode status = UA_Server_addNode_finish(_uaServer, nodeId.id);
		if (status != UA_STATUSCODE_GOOD)
		{
			// open62541 deletes a node it cannot finish
			_result.addedNodes.removeOne(node.nodeId);
			this->warn(node, QStringLiteral("not added: %1").arg(QString::fromLatin1(UA_StatusCode_name(status))));
		}
	}
}

///
/// \brief Decodes the Value element of a node; a value that cannot be decoded is left empty with a warning.
///        Structures of data types unknown to open62541 are kept in their XML encoding.
///
UA_StatusCode QUaNodeSetLoader::decodeValue(const Node& node, UA_Variant* value)
{
	UA_Variant_init(value);
	if (node.value.isEmpty())
	{
		return UA_STATUSCODE_GOOD;
	}
	UA_NamespaceMapping mapping;
	memset(&mapping, 0, sizeof(UA_NamespaceMapping));
	mapping.namespaceUris     = _localNamespaceStrings.data();
	mapping.namespaceUrisSize = static_cast<size_t>(_localNamespaceStrings.size());
	mapping.remote2local      = _remoteToLocal.data();
	mapping.remote2localSize  = static_cast<size_t>(_remoteToLocal.size());
	UA_DecodeXmlOptions options;
	memset(&options, 0, sizeof(UA_DecodeXmlOptions));
	options.unwrapped        = true;
	options.namespaceMapping = &mapping;
	options.customTypes      = UA_Server_getDataTypes(_uaServer);
	UA_ByteString xml;
	xml.length = static_cast<size_t>(node.value.size());
	xml.data   = reinterpret_cast<UA_Byte*>(const_cast<char*>(node.value.constData()));
	const UA_StatusCode status = UA_decodeXml(&xml, value, &UA_TYPES[UA_TYPES_VARIANT], &options);
	if (status != UA_STATUSCODE_GOOD)
	{
		this->warn(node, QStringLiteral("value ignored, it cannot be decoded: %1")
			.arg(QString::fromLatin1(UA_StatusCode_name(status))));
	}
	return status;
}

///
/// \brief Records a warning about a node, identified as written in the NodeSet.
///
void QUaNodeSetLoader::warn(const Node& node, const QString& message)
{
	_result.warnings << QStringLiteral("%1 (%2): %3").arg(
		node.attributes.value(QStringLiteral("NodeId")),
		node.attributes.value(QStringLiteral("BrowseName")),
		message);
}
