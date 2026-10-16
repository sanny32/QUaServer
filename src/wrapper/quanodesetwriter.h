#ifndef QUANODESETWRITER_H
#define QUANODESETWRITER_H

#include <QHash>
#include <QSet>
#include <QXmlStreamWriter>

#include <QUaServer>

class QUaNodeSetWriter
{
public:
	explicit QUaNodeSetWriter(const QUaServer* server);

	QUaNodeSetExportResult write(QIODevice* device, const QStringList& namespaceUris);

private:
	struct Reference
	{
		QString referenceType;
		QString target;
		bool    isForward = true;
	};

	struct Field
	{
		QList<QPair<QString, QString>> attributes;
		QUaLocalizedText               description;
	};

	struct Node
	{
		QString                        element;
		QList<QPair<QString, QString>> attributes;
		QList<QUaLocalizedText>        displayName;
		QUaLocalizedText               description;
		QUaLocalizedText               inverseName;
		QList<Reference>               references;
		QByteArray                     value;
		bool                           hasDefinition = false;
		QString                        definitionName;
		QList<Field>                   definition;
	};

	bool selectNamespaces(const QStringList& namespaceUris);
	void findHierarchicalReferenceTypes();
	QList<UA_ReferenceDescription> browse(const UA_NodeId& nodeId, UA_BrowseDirection direction,
	                                      const UA_NodeId& referenceTypeId) const;
	void collectNodes();
	bool isExported(const UA_NodeId& nodeId) const;
	bool readAttribute(const UA_NodeId& nodeId, UA_AttributeId attributeId, UA_Variant& value) const;
	Node describeNode(const UA_NodeId& nodeId);
	void describeReferences(const UA_NodeId& nodeId, bool isType, Node& node);
	void describeVariable(const UA_NodeId& nodeId, bool isType, Node& node);
	void describeDefinition(const UA_NodeId& nodeId, Node& node);
	QString nodeIdText(const UA_NodeId& nodeId);
	QString browseNameText(const UA_QualifiedName& browseName) const;
	QByteArray encodeValue(const UA_Variant& value, const UA_NodeId& nodeId);
	void writeDocument(QXmlStreamWriter& writer, const QList<Node>& nodes) const;
	void writeNode(QXmlStreamWriter& writer, const Node& node) const;
	static void writeLocalizedText(QXmlStreamWriter& writer, const QString& name, const QUaLocalizedText& text);
	static void writeValue(QXmlStreamWriter& writer, const QByteArray& value);
	void warn(const UA_NodeId& nodeId, const QString& message);

	const QUaServer*        _server;
	UA_Server*              _uaServer;
	QStringList             _serverNamespaces;
	QSet<quint16>           _namespaces;
	QSet<QUaNodeId>         _hierarchicalReferenceTypes;
	QList<QUaNodeId>        _order;
	QSet<QUaNodeId>         _exported;
	QHash<QString, QString> _aliases;
	QUaNodeSetExportResult  _result;
};

#endif // QUANODESETWRITER_H
