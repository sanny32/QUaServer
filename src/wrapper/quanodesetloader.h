#ifndef QUANODESETLOADER_H
#define QUANODESETLOADER_H

#include <QHash>
#include <QSet>
#include <QVector>
#include <QXmlStreamReader>

#include <QUaServer>

class QUaNodeSetLoader
{
public:
	explicit QUaNodeSetLoader(QUaServer* server);

	QUaNodeSetResult load(QIODevice* device);

private:
	struct Reference
	{
		QString   referenceType;
		QString   target;
		bool      isForward = true;
		QUaNodeId referenceTypeId;
		QUaNodeId targetId;
	};

	struct DefinitionField
	{
		QString   name;
		QString   dataType;
		qint32    valueRank  = -1;
		bool      isOptional = false;
		qint64    value      = 0;
		bool      hasValue   = false;
		QUaNodeId dataTypeId;
	};

	struct Node
	{
		UA_NodeClass            nodeClass = UA_NODECLASS_UNSPECIFIED;
		QHash<QString, QString> attributes;
		QUaLocalizedText        displayName;
		QUaLocalizedText        description;
		QUaLocalizedText        inverseName;
		QList<Reference>        references;
		QByteArray              value;
		QUaNodeId               nodeId;
		QUaQualifiedName        browseName;
		QUaNodeId               parentNodeId;
		QUaNodeId               parentReferenceTypeId;
		QUaNodeId               typeDefinitionId;
		QUaNodeId               dataTypeId;
		bool                    hasDefinition = false;
		bool                    isUnion       = false;
		bool                    isOptionSet   = false;
		QList<DefinitionField>  definition;
	};

	bool readNodeSet(QXmlStreamReader& reader);
	void readNamespaceUris(QXmlStreamReader& reader);
	void readAliases(QXmlStreamReader& reader);
	void readNode(QXmlStreamReader& reader, UA_NodeClass nodeClass);
	static QUaLocalizedText readLocalizedText(QXmlStreamReader& reader);
	static QList<Reference> readReferences(QXmlStreamReader& reader);
	static QByteArray readValue(QXmlStreamReader& reader);
	static void readDefinition(QXmlStreamReader& reader, Node& node);

	void mapNamespaces();
	bool resolveNodeId(const QString& text, QUaNodeId& nodeId) const;
	QUaQualifiedName resolveBrowseName(const QString& text) const;
	void resolveNodes();
	bool resolveNode(Node& node);
	void findHierarchicalReferenceTypes();
	void resolveParents();
	QList<int> sortNodes() const;
	void visitNode(int index, QVector<char>& states, QList<int>& sorted) const;

	UA_StatusCode addNode(const Node& node);
	void registerReferenceType(const Node& node);
	void registerDataType(const Node& node);
	void registerStructure(const Node& node);
	QUaNodeId encodingId(const Node& node, const QString& encodingName) const;
	void addReferences(const Node& node);
	void finishNodes(const QList<int>& sorted, const QSet<int>& begun);
	UA_StatusCode decodeValue(const Node& node, UA_Variant* value);
	void warn(const Node& node, const QString& message);

	QUaServer*              _server;
	UA_Server*              _uaServer;
	QStringList             _namespaceUris;
	QHash<QString, QString> _aliases;
	QList<Node>             _nodes;
	QHash<QUaNodeId, int>   _nodeIndexes;
	QVector<quint16>        _remoteToLocal;
	QList<QByteArray>       _localNamespaceUris;
	QVector<UA_String>      _localNamespaceStrings;
	QSet<QUaNodeId>         _hierarchicalReferenceTypes;
	QUaNodeSetResult        _result;
};

#endif // QUANODESETLOADER_H
