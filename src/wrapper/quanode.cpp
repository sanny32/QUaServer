#include "quanode.h"

#include <QUaServer>
#include <QUaProperty>
#include <QUaBaseDataVariable>
#include <QUaFolderObject>

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
#include <QUaBaseEvent>
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
#include <QUaCondition>
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

#include "quaserver_anex.h"

struct QUaInMemorySerializer
{
    bool writeInstance(
		const QUaNodeId& nodeId,
		const QString& typeName,
		const QMap<QString, QVariant>& attrs,
		const QList<QUaForwardReference>& forwardRefs,
		QQueue<QUaLog>& logOut)
	{
		Q_ASSERT(!_hashNodeTreeData.contains(nodeId));
		if (_hashNodeTreeData.contains(nodeId))
		{
			logOut << QUaLog({
				QObject::tr("Error serializing node. Repeated NodeId %1").arg(nodeId),
				QUaLogLevel::Error,
				QUaLogCategory::Application
			});
			// the show must go on
			return true;
		}
		_hashNodeTreeData[nodeId] = {
			typeName, attrs, forwardRefs
		};
		return true;
	};
    bool readInstance(
		const QUaNodeId& nodeId,
		QString& typeName,
		QMap<QString, QVariant>& attrs,
		QList<QUaForwardReference>& forwardRefs,
		QQueue<QUaLog>& logOut)
	{
		Q_ASSERT(_hashNodeTreeData.contains(nodeId));
		if (!_hashNodeTreeData.contains(nodeId))
		{
			logOut << QUaLog({
				QObject::tr("Error deserializing node. Could not find NodeId %1").arg(nodeId),
				QUaLogLevel::Error,
				QUaLogCategory::Application
			});
			// the show must go on
			return true;
		}
		const auto& data = _hashNodeTreeData[nodeId];
		typeName    = data.typeName;
		attrs       = data.attrs;
		forwardRefs = data.forwardRefs;
		return true;
	};
    void clear()
	{
		_hashNodeTreeData.clear();
	};
    void qtDebug(QUaServer * server, const QString &header)
	{
		qDebug() << header;
		auto listNodeIds = _hashNodeTreeData.keys();
		std::sort(listNodeIds.begin(), listNodeIds.end(),
		[this, server](const QUaNodeId& nodeId1, const QUaNodeId& nodeId2) -> bool {
			QString browse1 = QUaQualifiedName::reduceXml(server->nodeById(nodeId1)->nodeBrowsePath());
			QString browse2 = QUaQualifiedName::reduceXml(server->nodeById(nodeId2)->nodeBrowsePath());
			return browse1 < browse2;
		});
		for (const auto & nodeId : std::as_const(listNodeIds))
		{
			QUaNode* node = server->nodeById(nodeId);
			QString browse = QUaQualifiedName::reduceXml(node->nodeBrowsePath());
			qDebug() << QStringLiteral("%1 [%2] (%3)")
				.arg(browse)
				.arg(nodeId)
				.arg(_hashNodeTreeData[nodeId].typeName);
			if (_hashNodeTreeData[nodeId].attrs.contains( QStringLiteral("value") ))
			{
				qDebug()
					<< _hashNodeTreeData[nodeId].attrs[ QStringLiteral("dataType") ]
					<< _hashNodeTreeData[nodeId].attrs[ QStringLiteral("value") ];
			}
		}
	}
	struct QUaNodeData
	{
		QString typeName;
		QMap<QString, QVariant> attrs;
		QList<QUaForwardReference> forwardRefs;
	};
	QHash<QUaNodeId, QUaNodeData> _hashNodeTreeData;
};

QUaNode::QUaNode(
	QUaServer* server
)
{
	_liveNodes.insert(static_cast<const void*>(this));
	// [NOTE] : constructor of any QUaNode-derived class is not meant to be called by the user
	//          the constructor is called automagically by this library, and _newNodeNodeId and
	//          _newNodeMetaObject must be set in QUaServer before calling the constructor, as
	//          is used in QUaServer::uaConstructor
	Q_CHECK_PTR(server);
	Q_CHECK_PTR(server->_newNodeNodeId);
	Q_CHECK_PTR(server->_newNodeMetaObject);
	const UA_NodeId   &nodeId     = *server->_newNodeNodeId;
	const QMetaObject &metaObject = *server->_newNodeMetaObject;
	// check
	Q_ASSERT(server && !UA_NodeId_isNull(&nodeId));
	// bind itself, only good for constructors of derived classes, because UA constructor overwrites it
	// so we need to also set the context again in QUaServer::uaConstructor
	// set server instance
	this->_qUaServer = server;
	// set c++ instance as context
	UA_Server_setNodeContext(server->_server, nodeId, (void*)this);
	// set node id to c++ instance
	// NOTE : deep copy, the node id passed by open62541 might be a temporary
	UA_NodeId_copy(&nodeId, &this->_nodeId);
	// ignore objects folder
	UA_NodeId objectsFolderNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
	if (UA_NodeId_equal(&nodeId, &objectsFolderNodeId))
	{ 
		return; 
	}
	// get all UA children in advance, because if none, then better early exit
	auto chidrenNodeIds = QUaNode::getChildrenNodeIds(nodeId, server->_server);
	if (chidrenNodeIds.count() <= 0)
	{ 
		return; 
	}
	// create hash of nodeId's by browse name, which must match Qt's metaprops
	QHash<QUaQualifiedName, UA_NodeId> mapChildren;
	for (const auto &childNodeId : std::as_const(chidrenNodeIds))
	{
		// read browse name
		QUaQualifiedName browseName = QUaNode::getBrowseName(childNodeId, server->_server);
		Q_ASSERT(!mapChildren.contains( browseName));
		mapChildren[browseName] = childNodeId;
	}
	// list meta props
	int propCount  = metaObject.propertyCount();
	int propOffset = QUaNode::getPropsOffsetHelper(metaObject);
	int numProps   = 0;
	for (int i = propOffset; i < propCount; i++)
	{
		QMetaProperty metaProperty = metaObject.property(i);
		// check if not enum
		if (!metaProperty.isEnumType())
		{
			// check if available in meta-system
			const QMetaObject *propMetaObject = QMetaType(metaProperty.userType()).metaObject();
			if (!propMetaObject)
			{ continue; }
			// check if OPC UA relevant type
			if (!propMetaObject->inherits(&QUaNode::staticMetaObject))
			{ continue; }
			// check if prop inherits from parent
			Q_ASSERT_X(!propMetaObject->inherits(&metaObject), "QUaNode Constructor",
				"Qt MetaProperty type cannot inherit from Class.");
			if (propMetaObject->inherits(&metaObject))
			{ continue; }
		}
		// inc number of valid props
		numProps++;
		// the Qt meta property name must match the UA browse name
		QUaQualifiedName browseName = QString::fromUtf8(metaProperty.name());
		Q_ASSERT(mapChildren.contains(browseName));
		// get child nodeId for child
		auto childNodeId = mapChildren.take(browseName);
		// get node context (C++ instance)
		auto nodeInstance = QUaNode::getNodeContext(childNodeId, server->_server);
		Q_CHECK_PTR(nodeInstance);
		this->bindChild(nodeInstance, browseName);
		// [NOTE] writing a pointer value to a Q_PROPERTY did not work,
		//        eventhough there appear to be some success cases on the internet
		//        so in the end we have to query children by object name
	} // for each prop
	// handle mandatory children of instance declarations
	QUaNodeId typeNodeId = this->typeDefinitionNodeId();
	Q_ASSERT(_qUaServer->_hashMandatoryChildren.contains(typeNodeId));
	const auto &mandatoryList = _qUaServer->_hashMandatoryChildren[typeNodeId];
	for (const auto & browseName : mandatoryList)
	{
		Q_ASSERT(mapChildren.contains(browseName));
		// get child nodeId for child
		auto childNodeId = mapChildren.take(browseName);
		// get node context (C++ instance)
		auto nodeInstance = QUaNode::getNodeContext(childNodeId, server->_server);
		Q_CHECK_PTR(nodeInstance);
		this->bindChild(nodeInstance, browseName);
	}
	// nodes loaded from a NodeSet are constructed before their parent, with any children of their own
	for (auto it = mapChildren.cbegin(); it != mapChildren.cend(); ++it)
	{
		auto nodeInstance = QUaNode::getNodeContext(it.value(), server->_server);
		if (!nodeInstance || nodeInstance->parent())
		{
			continue;
		}
		UA_NodeId childParentNodeId = QUaNode::getParentNodeId(it.value(), server->_server);
		if (UA_NodeId_equal(&childParentNodeId, &nodeId))
		{
			this->bindChild(nodeInstance, it.key());
		}
		UA_NodeId_clear(&childParentNodeId);
	}
	// cleanup
	for (auto & childNodeId : chidrenNodeIds)
	{
		UA_NodeId_clear(&childNodeId);
	}
}

///
/// \brief Makes \a child a Qt child reachable through browseChild() by \a browseName.
///
void QUaNode::bindChild(QUaNode* child, const QUaQualifiedName& browseName)
{
	child->setParent(this);
	child->setObjectName(browseName);
	Q_ASSERT(!this->browseChild(browseName));
	size_t key = qHash(browseName);
	_browseCache[key] = child;
	QObject::connect(child, &QObject::destroyed, this, [this, key]() {
		_browseCache.remove(key);
	});
}

QUaNode::~QUaNode()
{
	_liveNodes.remove(static_cast<const void*>(this));
	// [FIX] : QObject children destructors were called after this one
	//         and the some sub-types destructors might use parent's _nodeId
	//         so we better destroy the children manually before deleting while
	//         _nodeId is still valid
	while (this->children().count() > 0)
	{
		delete this->children().at(0);
	}
	// check if node id has been already removed from node store
	// i.e. child of deleted parent node, or ...
	UA_NodeId outNodeId;
	auto st = UA_Server_readNodeId(_qUaServer->_server, _nodeId, &outNodeId);
	if (st == UA_STATUSCODE_BADNODEIDUNKNOWN)
	{
		// cleanup
		UA_NodeId_clear(&outNodeId);
		UA_NodeId_clear(&_nodeId);
		return;
	}
	Q_ASSERT(UA_NodeId_equal(&_nodeId, &outNodeId));
	// cleanup
	UA_NodeId_clear(&outNodeId);
	// remove context, so we avoid double deleting in ua destructor when called
	st = UA_Server_setNodeContext(_qUaServer->_server, _nodeId, nullptr);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// delete node in ua (NOTE : also delete references)
	st = UA_Server_deleteNode(_qUaServer->_server, _nodeId, true);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// trigger reference deleted, model change event, so client (UaExpert) auto refreshes tree
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	Q_CHECK_PTR(_qUaServer->_changeEvent);
	// add reference deleted change to buffer
	QUaNode* parent = qobject_cast<QUaNode*>(this->parent());
	if (parent && parent->inAddressSpace())
	{
		_qUaServer->addChange({
            parent ? QString(parent->nodeId()) : QUaTypesConverter::nodeIdToQString(UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER)),
            parent ? QString(parent->typeDefinitionNodeId()) : QUaTypesConverter::nodeIdToQString(UA_NODEID_NUMERIC(0, UA_NS0ID_SERVERTYPE)),
			QUaChangeVerb::ReferenceDeleted // UaExpert does not recognize QUaChangeVerb::NodeAdded
		});	
	}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
	UA_NodeId_clear(&_nodeId);
}

bool QUaNode::operator==(const QUaNode & other) const
{
	return UA_NodeId_equal(&this->_nodeId, &other._nodeId);
}

QUaServer * QUaNode::server() const
{
	return _qUaServer;
}

QUaLocalizedText QUaNode::displayName() const
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	// read display name
	UA_LocalizedText outDisplayName;
	auto st = UA_Server_readDisplayName(_qUaServer->_server, _nodeId, &outDisplayName);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	QUaLocalizedText displayName = outDisplayName;
	// cleanup
	UA_LocalizedText_clear(&outDisplayName);
	// return
	return displayName;
}

void QUaNode::setDisplayName(const QUaLocalizedText& displayName)
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
    UA_LocalizedText uaDisplayName = displayName;
	// set value
	auto st = UA_Server_writeDisplayName(_qUaServer->_server, _nodeId, uaDisplayName);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// cleanup
	UA_LocalizedText_clear(&uaDisplayName);
	// emit displayName changed
	emit this->displayNameChanged(displayName);
}

QUaLocalizedText QUaNode::description() const
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	// read description
	UA_LocalizedText outDescription;
	auto st = UA_Server_readDescription(_qUaServer->_server, _nodeId, &outDescription);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	QUaLocalizedText description = outDescription;
	UA_LocalizedText_clear(&outDescription);
	// return
	return description;
}

void QUaNode::setDescription(const QUaLocalizedText& description)
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	UA_LocalizedText uaDescription = description;
	// set value
	auto st = UA_Server_writeDescription(_qUaServer->_server, _nodeId, uaDescription);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	UA_LocalizedText_clear(&uaDescription);
	// emit description changed
	emit this->descriptionChanged(description);
}

quint32 QUaNode::writeMask() const
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	// read writeMask
	UA_UInt32 outWriteMask;
	auto st = UA_Server_readWriteMask(_qUaServer->_server, _nodeId, &outWriteMask);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// return
	return outWriteMask;
}

void QUaNode::setWriteMask(const quint32 & writeMask)
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	// set value
	auto st = UA_Server_writeWriteMask(_qUaServer->_server, _nodeId, writeMask);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// emit writeMask changed
	emit this->writeMaskChanged(writeMask);
}

QUaNodeId QUaNode::nodeId() const
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	return _nodeId;
}

QString QUaNode::nodeClass() const
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	// read nodeClass
	UA_NodeClass outNodeClass;
	auto st = UA_Server_readNodeClass(_qUaServer->_server, _nodeId, &outNodeClass);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// convert to QString
	return QUaTypesConverter::nodeClassToQString(outNodeClass);
}

QUaQualifiedName QUaNode::browseName() const
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	// check cache
	if (!_browseName.isEmpty())
	{
		return _browseName;
	}
	// read browse name
	UA_QualifiedName outBrowseName;
	auto st = UA_Server_readBrowseName(_qUaServer->_server, _nodeId, &outBrowseName);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// update cache
	const_cast<QUaNode*>(this)->_browseName = outBrowseName;
	// cleanup
	UA_QualifiedName_clear(&outBrowseName);
	return _browseName;
}

QUaProperty * QUaNode::addProperty(
	const QUaQualifiedName& browseName,
	const QUaNodeId& nodeId/* = ""*/
)
{
	return _qUaServer->createInstance<QUaProperty>(this, browseName, nodeId);
}

QUaBaseDataVariable* QUaNode::addBaseDataVariable(
	const QUaQualifiedName& browseName,
	const QUaNodeId& nodeId/* = ""*/
)
{
	return _qUaServer->createInstance<QUaBaseDataVariable>(this, browseName, nodeId);
}

QUaBaseObject* QUaNode::addBaseObject(
	const QUaQualifiedName& browseName,
	const QUaNodeId& nodeId/* = ""*/
)
{
	return _qUaServer->createInstance<QUaBaseObject>(this, browseName, nodeId);
}

QUaFolderObject* QUaNode::addFolderObject(
	const QUaQualifiedName& browseName,
	const QUaNodeId& nodeId/* = ""*/
)
{
	return _qUaServer->createInstance<QUaFolderObject>(this, browseName, nodeId);
}

QUaNodeId QUaNode::typeDefinitionNodeId() const
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	if (!_typeDefinitionNodeId.isNull())
	{
		return _typeDefinitionNodeId;
	}
	UA_NodeId retTypeId = QUaNode::typeDefinitionNodeId(_nodeId, _qUaServer->_server);
	// return in string form
	const_cast<QUaNode*>(this)->_typeDefinitionNodeId = retTypeId;
	UA_NodeId_clear(&retTypeId);
	return _typeDefinitionNodeId;
}

UA_NodeId QUaNode::typeDefinitionNodeId(
	const UA_NodeId& nodeId, 
	UA_Server* server
)
{
	UA_NodeId retTypeId = UA_NODEID_NULL;
	// make ua browse
	UA_BrowseDescription* bDesc = UA_BrowseDescription_new();
	UA_NodeId_copy(&nodeId, &bDesc->nodeId); // from child
        // GCC does not like : UA_NodeId_copy(&UA_NODEID_NUMERIC(0, UA_NS0ID_HASTYPEDEFINITION), &bDesc->referenceTypeId);
        bDesc->referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASTYPEDEFINITION);
	bDesc->browseDirection = UA_BROWSEDIRECTION_FORWARD;
	bDesc->includeSubtypes = true;
	bDesc->resultMask = UA_BROWSERESULTMASK_REFERENCETYPEID;
	// browse
	UA_BrowseResult bRes = UA_Server_browse(server, 0, bDesc);
	Q_ASSERT(bRes.statusCode == UA_STATUSCODE_GOOD);
	Q_ASSERT(bRes.referencesSize == 1);
	if (bRes.referencesSize < 1)
	{
		return retTypeId;
	}
	UA_ReferenceDescription rDesc = bRes.references[0];
	UA_NodeId_copy(&rDesc.nodeId.nodeId, &retTypeId);
	// cleanup
    UA_BrowseDescription_clear(bDesc);
	UA_BrowseDescription_delete(bDesc);
    UA_BrowseResult_clear(&bRes);
	// return
	// NOTE : need to clean up returned value
	return retTypeId;
}

UA_NodeId QUaNode::superTypeDefinitionNodeId(
	const UA_NodeId& typeNodeId,
	UA_Server* server
)
{
	// FIX : https://github.com/open62541/open62541/issues/3917
	static const UA_NodeId baseEvtNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEEVENTTYPE);
	if (UA_NodeId_equal(&typeNodeId, &baseEvtNodeId))
	{
		return UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE);
	}
	UA_NodeId retTypeId = UA_NODEID_NULL;
	// make ua browse
	UA_BrowseDescription * bDesc = UA_BrowseDescription_new();
	UA_NodeId_copy(&typeNodeId, &bDesc->nodeId); // from child
        // GCC does not like : UA_NodeId_copy(&UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE), &bDesc->referenceTypeId);
        bDesc->referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASSUBTYPE);
	bDesc->browseDirection = UA_BROWSEDIRECTION_INVERSE;
	bDesc->includeSubtypes = true;
	bDesc->resultMask      = UA_BROWSERESULTMASK_REFERENCETYPEID;
	// browse
	UA_BrowseResult bRes = UA_Server_browse(server, 0, bDesc);
	Q_ASSERT_X(bRes.statusCode == UA_STATUSCODE_GOOD, "QUaNode::superTypeDefinitionNodeId", "Browsing children failed.");
	if (bRes.statusCode != UA_STATUSCODE_GOOD)
	{
		return retTypeId;
	}
	Q_ASSERT(bRes.referencesSize == 1);
	if (bRes.referencesSize < 1)
	{
		return retTypeId;
	}
	UA_ReferenceDescription rDesc = bRes.references[0];
	UA_NodeId_copy(&rDesc.nodeId.nodeId, &retTypeId);
	// cleanup
    UA_BrowseDescription_clear(bDesc);
	UA_BrowseDescription_delete(bDesc);
    UA_BrowseResult_clear(&bRes);
	// return
	// NOTE : need to clean up returned value
	return retTypeId;
}

QString QUaNode::typeDefinitionDisplayName() const
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	UA_NodeId typeId = this->typeDefinitionNodeId();
	Q_ASSERT(!UA_NodeId_isNull(&typeId));
	if (UA_NodeId_isNull(&typeId))
	{
		return QString();
	}
	// read display name
	UA_LocalizedText outDisplayName;
	auto st = UA_Server_readDisplayName(_qUaServer->_server, typeId, &outDisplayName);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	QString displayName = QUaTypesConverter::uaStringToQString(outDisplayName.text);
	// cleanup
	UA_NodeId_clear(&typeId);
	UA_LocalizedText_clear(&outDisplayName);
	// return
	return displayName;
}

QHash<QUaNodeId, QUaQualifiedName> QUaNode::_hashTypeBrowseNames;

QUaQualifiedName QUaNode::typeDefinitionBrowseName() const
{
	Q_CHECK_PTR(_qUaServer);
	Q_ASSERT(!UA_NodeId_isNull(&_nodeId));
	QUaNodeId typeId = this->typeDefinitionNodeId();
	Q_ASSERT(!typeId.isNull());
	if (typeId.isNull())
	{
		return QUaQualifiedName();
	}
	// check cache
	if (QUaNode::_hashTypeBrowseNames.contains(typeId))
	{
		return QUaNode::_hashTypeBrowseNames[typeId];
	}
	// read browse name
	UA_QualifiedName outBrowseName;
	auto st = UA_Server_readBrowseName(_qUaServer->_server, typeId, &outBrowseName);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	// populate return value
	QUaQualifiedName  browseName = outBrowseName;
	// cleanup
	UA_QualifiedName_clear(&outBrowseName);
	// update cache
	QUaNode::_hashTypeBrowseNames[typeId] = browseName;
	return  browseName;
}

QList<QUaNode*> QUaNode::browseChildren() const
{
	// TODO : check if faster with open62541 browse API
	return this->findChildren<QUaNode*>(QString(), Qt::FindDirectChildrenOnly);
}

QUaNode* QUaNode::browseChild(
	const QUaQualifiedName&  browseName,
	const bool& instantiateOptional/* = false*/)
{
	// first check cache
	QUaNode* child = nullptr;
	size_t key = qHash(browseName);
	if (_browseCache.contains(key))
	{
		child = _browseCache.value(key);
		if (child || !instantiateOptional)
		{
			return child;
		}
	}
	if (!instantiateOptional)
	{
		_browseCache[key] = nullptr;
		return nullptr;
	}
	child = this->instantiateOptionalChild(browseName);
	Q_ASSERT_X(child, "QUaNode::browseChild", "TODO : error log");
	return child;
}

bool QUaNode::hasChild(const QUaQualifiedName &browseName)
{
	// NOTE : do not use _browseCache.contains because maybe we tested if existed
	//        and cached a nullptr. This will giv a false positive
	return this->browseChild(browseName);
}

QUaNode * QUaNode::browsePath(const QUaBrowsePath& browsePath) const
{
	QUaNode * currNode = const_cast<QUaNode *>(this);
	for (const auto & browseName : browsePath)
	{
		currNode = currNode->browseChild(browseName);
		if (!currNode)
		{
			return nullptr;
		}
	}
	return currNode;
}

QUaBrowsePath QUaNode::nodeBrowsePath() const
{
	// get parents browse path and then attach current browse name
	// stop recursion if current node is ObjectsFolder
	if (this == _qUaServer->objectsFolder())
	{
		return QUaBrowsePath() << this->browseName();
	}
#ifdef QT_DEBUG 
	QUaNode* parent = qobject_cast<QUaNode*>(this->parent());
	// handle hidden nodes (i.e. branches)
#ifndef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	Q_ASSERT(parent);
#else
	Q_ASSERT(parent || qobject_cast<const QUaCondition*>(this));
#endif // !UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
	if (!parent)
	{
		return QUaBrowsePath() << _qUaServer->objectsFolder()->browseName() << this->browseName();
	}
#else
	QUaNode* parent = static_cast<QUaNode*>(this->parent());
#endif // QT_DEBUG 
	return parent->nodeBrowsePath() << this->browseName();
}

void QUaNode::addReference(const QUaReferenceType& ref, QUaNode* nodeTarget, const bool& isForward/* = true*/)
{
	// first check if reference type is registered
	if (!_qUaServer->_hashRefTypes.contains(ref))
	{
		_qUaServer->registerReferenceType(ref);
	}
	Q_ASSERT(_qUaServer->_hashRefTypes.contains(ref));
	// reject hierarchical references
	Q_ASSERT_X(!_qUaServer->_hashHierRefTypes.contains(ref), "QUaNode::addReference", "Cannot add hierarchical references using this method.");
	if (_qUaServer->_hashHierRefTypes.contains(ref))
	{
		return;
	}
	// check valid node target
	Q_ASSERT_X(nodeTarget, "QUaNode::addReference", "Invalid target node.");
	if (!nodeTarget)
	{
		return;
	}
	UA_NodeId refTypeId = _qUaServer->_hashRefTypes[ref];
	// check if reference already exists
	auto set = getRefsInternal(ref, isForward);
	if (set.contains(nodeTarget->_nodeId))
	{
		// cleanup set
		QSetIterator<UA_NodeId> i(set);
		while (i.hasNext())
		{
			UA_NodeId nodeId = i.next();
			UA_NodeId_clear(&nodeId);
		}
		return;
	}
	// cleanup set
	QSetIterator<UA_NodeId> i(set);
	while (i.hasNext())
	{
		UA_NodeId nodeId = i.next();
		UA_NodeId_clear(&nodeId);
	}
	// add the reference
	auto st = UA_Server_addReference(
		_qUaServer->_server,
		_nodeId,
		refTypeId,
		{ nodeTarget->_nodeId, UA_STRING_NULL, 0 },
		isForward
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// emit events
	emit this->referenceAdded(ref, nodeTarget, isForward);
	emit nodeTarget->referenceAdded(ref, this, !isForward);
	// subscribe node destructions
	QObject::connect(nodeTarget, &QObject::destroyed, this,
	[this, ref, nodeTarget, isForward]() {
		// emit event
		emit this->referenceRemoved(ref, nodeTarget, isForward);
	});
	QObject::connect(this, &QObject::destroyed, nodeTarget,
	[this, ref, nodeTarget, isForward]() {
		// emit event
		emit nodeTarget->referenceRemoved(ref, this, !isForward);
	});
}

void QUaNode::removeReference(const QUaReferenceType& ref, QUaNode* nodeTarget, const bool& isForward/* = true*/)
{
	// first check if reference type is removeReference
	Q_ASSERT_X(_qUaServer->_hashRefTypes.contains(ref), "QUaNode::addReference", "Reference not registered.");
	if (!_qUaServer->_hashRefTypes.contains(ref))
	{
		return;
	}
	// check valid node target
	Q_ASSERT_X(nodeTarget, "QUaNode::removeReference", "Invalid target node.");
	if (!nodeTarget)
	{
		return;
	}
	UA_NodeId refTypeId = _qUaServer->_hashRefTypes[ref];
	// check reference exists
	auto set = getRefsInternal(ref, isForward);
	if (!set.contains(nodeTarget->_nodeId))
	{
		// cleanup set
		QSetIterator<UA_NodeId> i(set);
		while (i.hasNext())
		{
			UA_NodeId nodeId = i.next();
			UA_NodeId_clear(&nodeId);
		}
		return;
	}
	// cleanup set
	QSetIterator<UA_NodeId> i(set);
	while (i.hasNext())
	{
		UA_NodeId nodeId = i.next();
		UA_NodeId_clear(&nodeId);
	}
	// remove the reference
	auto st = UA_Server_deleteReference(
		_qUaServer->_server,
		_nodeId,
		refTypeId,
		isForward,
		{ nodeTarget->_nodeId, UA_STRING_NULL, 0 },
		true
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	// emit event
	emit this->referenceRemoved(ref, nodeTarget, isForward);
	emit nodeTarget->referenceRemoved(ref, this, !isForward);
	// unsubscribe node destructions
	QObject::disconnect(nodeTarget, &QObject::destroyed, this, 0);
	QObject::disconnect(this, &QObject::destroyed, nodeTarget, 0);
}

QList<QUaNode*> QUaNode::findReferences(const QUaReferenceType& ref, const bool& isForward /*= true*/) const
{
	QList<QUaNode*> retRefList;
	// call internal method
	auto set = getRefsInternal(ref, isForward);
	// get contexts
	QSetIterator<UA_NodeId> i(set);
	while (i.hasNext())
	{
		UA_NodeId nodeId = i.next();
		QUaNode* node = QUaNode::getNodeContext(nodeId, _qUaServer->_server);
		if (node)
		{
			Q_ASSERT(UA_NodeId_equal(&nodeId, &node->_nodeId));
			retRefList.append(node);
		}
		// cleanup set
		UA_NodeId_clear(&nodeId);
	}
	// return
	return retRefList;
}

/// NOTE : code borrowed from open62541 addOptionalObjectField, uses only public API
UA_StatusCode QUaNode::addOptionalVariableField(
	UA_Server* server,
	const UA_NodeId* originNode,
	const UA_QualifiedName* fieldName,
	const UA_NodeId* optionalVariableFieldNodeId,
	UA_NodeId* outOptionalVariable)
{
	UA_VariableAttributes vAttr = UA_VariableAttributes_default;
	UA_StatusCode retval = UA_Server_readValueRank(server, *optionalVariableFieldNodeId, &vAttr.valueRank);
	if (retval != UA_STATUSCODE_GOOD)
	{
		UA_LOG_ERROR(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_USERLAND,
			"Reading ValueRank failed. StatusCode %s", UA_StatusCode_name(retval));
		return retval;
	}
	retval = UA_Server_readDisplayName(server, *optionalVariableFieldNodeId, &vAttr.displayName);
	if (retval != UA_STATUSCODE_GOOD)
	{
		UA_LOG_ERROR(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_USERLAND,
			"Reading DisplayName failed. StatusCode %s", UA_StatusCode_name(retval));
		return retval;
	}
	retval = UA_Server_readDataType(server, *optionalVariableFieldNodeId, &vAttr.dataType);
	if (retval != UA_STATUSCODE_GOOD)
	{
		UA_LOG_ERROR(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_USERLAND,
			"Reading DataType failed. StatusCode %s", UA_StatusCode_name(retval));
		UA_VariableAttributes_clear(&vAttr);
		return retval;
	}
	// copy array dimensions
	UA_Variant arrayDimensions;
	UA_Variant_init(&arrayDimensions);
	retval = UA_Server_readArrayDimensions(server, *optionalVariableFieldNodeId, &arrayDimensions);
	if (retval == UA_STATUSCODE_GOOD &&
		arrayDimensions.type == &UA_TYPES[UA_TYPES_UINT32] &&
		arrayDimensions.arrayLength > 0)
	{
		vAttr.arrayDimensionsSize = arrayDimensions.arrayLength;
		vAttr.arrayDimensions     = static_cast<UA_UInt32*>(arrayDimensions.data);
		// NOTE : ownership moved to vAttr
		UA_Variant_init(&arrayDimensions);
	}
	UA_Variant_clear(&arrayDimensions);

	/* Get typedefintion */
	UA_NodeId typeNodeId = QUaNode::typeDefinitionNodeId(*optionalVariableFieldNodeId, server);
	if (UA_NodeId_isNull(&typeNodeId)) {
		UA_LOG_WARNING(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_USERLAND,
			"Invalid VariableType. StatusCode %s",
			UA_StatusCode_name(UA_STATUSCODE_BADTYPEDEFINITIONINVALID));
        UA_VariableAttributes_clear(&vAttr);
		return UA_STATUSCODE_BADTYPEDEFINITIONINVALID;
	}

	/* Set referenceType to parent */
	UA_NodeId referenceToParent;
	UA_NodeId propertyTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE);
	if (UA_NodeId_equal(&typeNodeId, &propertyTypeNodeId))
		referenceToParent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
	else
		referenceToParent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);

	/* Set a random unused NodeId with specified Namespace Index*/
	UA_NodeId optionalVariable = { originNode->namespaceIndex, UA_NODEIDTYPE_NUMERIC, {0} };
	retval = UA_Server_addVariableNode(server, optionalVariable, *originNode,
		referenceToParent, *fieldName, typeNodeId,
		vAttr, nullptr, outOptionalVariable);
	Q_ASSERT(retval == UA_STATUSCODE_GOOD);
	UA_NodeId_clear(&typeNodeId);
    UA_VariableAttributes_clear(&vAttr);
	return retval;
}

// NOTE : code borrowed from open62541 addOptionalObjectField, uses only public API
UA_StatusCode QUaNode::addOptionalObjectField(
	UA_Server* server,
	const UA_NodeId* originNode,
	const UA_QualifiedName* fieldName,
	const UA_NodeId* optionalObjectFieldNodeId,
	UA_NodeId* outOptionalObject)
{
	UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
	UA_StatusCode retval = UA_Server_readDisplayName(server, *optionalObjectFieldNodeId, &oAttr.displayName);
	if (retval != UA_STATUSCODE_GOOD)
	{
		UA_LOG_ERROR(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_USERLAND,
			"Reading DisplayName failed. StatusCode %s", UA_StatusCode_name(retval));
		return retval;
	}

	/* Get typedefintion */
	UA_NodeId typeNodeId = QUaNode::typeDefinitionNodeId(*optionalObjectFieldNodeId, server);
	if (UA_NodeId_isNull(&typeNodeId)) {
		UA_LOG_WARNING(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_USERLAND,
			"Invalid ObjectType. StatusCode %s",
			UA_StatusCode_name(UA_STATUSCODE_BADTYPEDEFINITIONINVALID));
        UA_ObjectAttributes_clear(&oAttr);
		return UA_STATUSCODE_BADTYPEDEFINITIONINVALID;
	}

	/* Set referenceType to parent */
	UA_NodeId referenceToParent;
	UA_NodeId propertyTypeNodeId = UA_NODEID_NUMERIC(0, UA_NS0ID_PROPERTYTYPE);
	if (UA_NodeId_equal(&typeNodeId, &propertyTypeNodeId))
		referenceToParent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASPROPERTY);
	else
		referenceToParent = UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT);

	UA_NodeId optionalObject = { originNode->namespaceIndex, UA_NODEIDTYPE_NUMERIC, {0} };
	retval = UA_Server_addObjectNode(server, optionalObject, *originNode,
		referenceToParent, *fieldName, typeNodeId,
		oAttr, nullptr, outOptionalObject);

	UA_NodeId_clear(&typeNodeId);
    UA_ObjectAttributes_clear(&oAttr);
	return retval;
}

UA_NodeId QUaNode::getOptionalChildNodeId(
	UA_Server* server, 
	const UA_NodeId& typeNodeIdIn, 
	const UA_QualifiedName& browseNameIn
)
{
	UA_NodeId optionalFieldNodeId = UA_NODEID_NULL;
	UA_NodeId typeNodeId;
	UA_NodeId_copy(&typeNodeIdIn, &typeNodeId);
	QUaQualifiedName browseName(browseNameIn);
	// look for optional child starting from this type of to base object type
    static UA_NodeId baseObjType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE);
    static UA_NodeId baseVarType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEVARIABLETYPE);
    while (!UA_NodeId_equal(&typeNodeId, &baseObjType) &&
        !UA_NodeId_equal(&typeNodeId, &baseVarType))
	{
		auto childrenNodeIds = QUaNode::getChildrenNodeIds(typeNodeId, server);
		for (auto & childNodeId : childrenNodeIds)
		{
			// ignore if not optional
			if (!QUaNode::hasOptionalModellingRule(childNodeId, server))
			{
				UA_NodeId_clear(&childNodeId);
				continue;
			}
			// ignore if browse name does not match
			QUaQualifiedName childBrowseName = QUaNode::getBrowseName(childNodeId, server);
			if (childBrowseName != browseName)
			{
				UA_NodeId_clear(&childNodeId);
				continue;
			}
			// copy, do not clear because will be used later
			optionalFieldNodeId = childNodeId;
		}
		// cleanup
		for (auto & childNodeId : childrenNodeIds)
		{
			UA_NodeId_clear(&childNodeId);
		}
		// check if found
		if (!UA_NodeId_isNull(&optionalFieldNodeId))
		{
			UA_NodeId_clear(&typeNodeId);
			break;
		}
		UA_NodeId typeNodeIdNew = QUaNode::superTypeDefinitionNodeId(typeNodeId, server);
		UA_NodeId_clear(&typeNodeId);
		UA_NodeId_copy(&typeNodeIdNew, &typeNodeId);
		UA_NodeId_clear(&typeNodeIdNew);
	}
	return optionalFieldNodeId;
}

QUaNode * QUaNode::instantiateOptionalChild(
	UA_Server* server, 
	QUaNode * parent,
	const UA_NodeId& optionalFieldNodeId,
	const UA_QualifiedName &childName)
{
	UA_NodeId outOptionalNode;
	UA_NodeId parentNodeId = parent->nodeId();
	// get the node class of the optional field
	UA_NodeClass nodeClass = UA_NODECLASS_UNSPECIFIED;
	auto st = UA_Server_readNodeClass(server, optionalFieldNodeId, &nodeClass);
	if (st != UA_STATUSCODE_GOOD)
	{
		UA_LOG_WARNING(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_USERLAND,
			"Couldn't find optional Field Node in ConditionType. StatusCode %s",
			UA_StatusCode_name(UA_STATUSCODE_BADNOTFOUND));
		return nullptr;
	}
	switch (nodeClass) {
	case UA_NODECLASS_VARIABLE:
	{
		UA_StatusCode retval = QUaNode::addOptionalVariableField(
			server,
			&parentNodeId,
			&childName,
			&optionalFieldNodeId,
			&outOptionalNode
		);
		if (retval != UA_STATUSCODE_GOOD)
		{
			UA_LOG_ERROR(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_USERLAND,
				"Adding Condition Optional Variable Field failed. StatusCode %s",
				UA_StatusCode_name(retval));
			return nullptr;
		}
	}
	break;
	case UA_NODECLASS_OBJECT:
	{
		UA_StatusCode retval = QUaNode::addOptionalObjectField(
			server,
			&parentNodeId, &childName,
			&optionalFieldNodeId,
			&outOptionalNode
		);
		if (retval != UA_STATUSCODE_GOOD)
		{
			UA_LOG_ERROR(UA_Server_getConfig(server)->logging, UA_LOGCATEGORY_USERLAND,
				"Adding Condition Optional Object Field failed. StatusCode %s",
				UA_StatusCode_name(retval));
			return nullptr;
		}
	}
	break;
	case UA_NODECLASS_METHOD:
		// NOTE : use QUaNode::addOptionalMethod instead
		Q_ASSERT(false);
		return nullptr;
	default:
		Q_ASSERT(false);
		return nullptr;
	}
	// if we reached here, the optional field has been instantiated
	// if the type of the optional field is registered in QUaServer then
	// it has been boud to its corresponding Qt type and there is nothing
	// more to do here
	QUaNode* child = QUaNode::getNodeContext(outOptionalNode, server);
	if (child)
	{
		Q_ASSERT(child->parent() == parent);
		Q_ASSERT(child->browseName() == childName);
		return child;
	}
	// else the type is not registered, so we need to create an instance of its
	// base class at least
	QMetaObject metaObject;
	if (nodeClass == UA_NODECLASS_VARIABLE) {
		metaObject = QUaBaseDataVariable::staticMetaObject;
	}
	else if (nodeClass == UA_NODECLASS_OBJECT) {
		metaObject = QUaBaseObject::staticMetaObject;
	}
	else {
		Q_ASSERT(false);
		return nullptr;
	}
	auto srv = QUaServer::getServerNodeContext(server);
	// to understand code below, see QUaServer::uaConstructor
	srv->_newNodeNodeId = &outOptionalNode;
	srv->_newNodeMetaObject = &metaObject;
	// instantiate new C++ node, _newNodeNodeId and _newNodeMetaObject only meant to be used during this call
	auto* pQObject = metaObject.newInstance(Q_ARG(QUaServer*, srv));
	Q_ASSERT_X(pQObject, "QUaNode::instantiateOptionalChild",
		"Failed instantiation. No matching Q_INVOKABLE constructor with signature "
		"CONSTRUCTOR(QUaServer *server) found.");
	auto* newInstance = qobject_cast<QUaNode*>(pQObject);
	Q_CHECK_PTR(newInstance);
	if (!newInstance)
	{
		return nullptr;
	}
	// need to bind again using the official (void ** nodeContext) of the UA constructor
	// because we set context on C++ instantiation, but later the UA library overwrites it 
	// after calling the UA constructor
	st = UA_Server_setNodeContext(
		server,
		outOptionalNode,
		static_cast<void*>(newInstance)
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	UA_NodeId_clear(&newInstance->_nodeId);
	newInstance->_nodeId = outOptionalNode; // NOTE : ownership transferred
	// need to set parent and browse name
	auto browseName = QUaQualifiedName(childName);
	parent->bindChild(newInstance, browseName);
	// emit child added to parent
	emit parent->childAdded(newInstance);
	// success
	return newInstance;
	
}

// NOTE : need to cleanup result after calling this method
QSet<UA_NodeId> QUaNode::getRefsInternal(const QUaReferenceType& ref, const bool & isForward /*= true*/) const
{
	QSet<UA_NodeId> retRefSet;
	// first check if reference type is registered
	if (!_qUaServer->_hashRefTypes.contains(ref))
	{
		_qUaServer->registerReferenceType(ref);
		// there cannot be any since it didnt even exist before
		return retRefSet;
	}
	Q_ASSERT(_qUaServer->_hashRefTypes.contains(ref));
	UA_NodeId refTypeId = _qUaServer->_hashRefTypes[ref];
	// make ua browse
	UA_BrowseDescription * bDesc = UA_BrowseDescription_new();
	UA_NodeId_copy(&_nodeId, &bDesc->nodeId); // from child
	UA_NodeId_copy(&refTypeId, &bDesc->referenceTypeId);
	bDesc->browseDirection = isForward ? UA_BROWSEDIRECTION_FORWARD : UA_BROWSEDIRECTION_INVERSE;
	bDesc->includeSubtypes = true;
	bDesc->nodeClassMask   = UA_NODECLASS_OBJECT | UA_NODECLASS_VARIABLE; // only objects or variables (no types or refs)
	bDesc->resultMask      = UA_BROWSERESULTMASK_REFERENCETYPEID;	
	// browse
	UA_BrowseResult bRes = UA_Server_browse(_qUaServer->_server, 0, bDesc);
	Q_ASSERT(bRes.statusCode == UA_STATUSCODE_GOOD);
	while (bRes.referencesSize > 0)
	{
		for (size_t i = 0; i < bRes.referencesSize; i++)
		{
			UA_ReferenceDescription rDesc = bRes.references[i];
			Q_ASSERT(UA_NodeId_equal(&rDesc.referenceTypeId, &refTypeId));
			UA_NodeId nodeId/* = rDesc.nodeId.nodeId*/;
			UA_NodeId_copy(&rDesc.nodeId.nodeId, &nodeId);
			Q_ASSERT(!retRefSet.contains(nodeId));
			retRefSet.insert(nodeId);
		}
        UA_BrowseResult_clear(&bRes);
		bRes = UA_Server_browseNext(_qUaServer->_server, true, &bRes.continuationPoint);
	}
	// cleanup
    UA_BrowseDescription_clear(bDesc);
	UA_BrowseDescription_delete(bDesc);
    UA_BrowseResult_clear(&bRes);
	// return
	return retRefSet;
}

QUaWriteMask QUaNode::userWriteMaskInternal(const QString & strUserName)
{
	// if has specific callback, use that one
	if (_userWriteMaskCallback)
	{
		return _userWriteMaskCallback(strUserName);
	}
	// else use possible reimplementation
	return this->userWriteMask(strUserName);
}

QUaAccessLevel QUaNode::userAccessLevelInternal(const QString & strUserName)
{
	// if has specific callback, use that one
	if (_userAccessLevelCallback)
	{
		return _userAccessLevelCallback(strUserName);
	}
	// else use possible reimplementation
	return this->userAccessLevel(strUserName);
}

bool QUaNode::userExecutableInternal(const QString & strUserName)
{
	// if has specific callback, use that one
	if (_userExecutableCallback)
	{
		return _userExecutableCallback(strUserName);
	}
	// else use possible reimplementation
	return this->userExecutable(strUserName);
}

// NOTE : some code borrowed from UA_Server_addConditionOptionalField
QUaNode* QUaNode::instantiateOptionalChild(const QUaQualifiedName&  browseName)
{
	// convert browse name to qualified name
	UA_QualifiedName childName = browseName;
	// look if optional child really in model
	UA_NodeId typeNodeId = QUaNode::typeDefinitionNodeId(_nodeId, _qUaServer->_server);
	UA_NodeId optionalFieldNodeId = QUaNode::getOptionalChildNodeId(
		_qUaServer->_server, 
		typeNodeId, 
		childName
	);
	UA_NodeId_clear(&typeNodeId);
	if (UA_NodeId_isNull(&optionalFieldNodeId))
	{
		UA_LOG_WARNING(UA_Server_getConfig(_qUaServer->_server)->logging, UA_LOGCATEGORY_USERLAND,
			"Couldn't find optional Field Node in ConditionType. StatusCode %s",
			UA_StatusCode_name(UA_STATUSCODE_BADNOTFOUND));
		UA_NodeId_clear(&optionalFieldNodeId);
		UA_QualifiedName_clear(&childName);
		return nullptr;
	}
	// instantiate according to type
	QUaNode * newInstance = QUaNode::instantiateOptionalChild(
		_qUaServer->_server,
		this,
		optionalFieldNodeId,
		childName
	);
	UA_NodeId_clear(&optionalFieldNodeId);
	UA_QualifiedName_clear(&childName);
	if (!newInstance)
	{
		Q_ASSERT_X(false, "QUaNode::instantiateOptionalChild", "Could not instatiate");
		return nullptr;
	}
	// trigger reference added, model change event, so client (UaExpert) auto refreshes tree
#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS
	if (!this->metaObject()->inherits(&QUaBaseEvent::staticMetaObject)
#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
		|| this->metaObject()->inherits(&QUaCondition::staticMetaObject)
#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS
		)
	{
		if (this->inAddressSpace())
		{
			// add reference added change to buffer
			_qUaServer->addChange({
				this->nodeId(),
				this->typeDefinitionNodeId(),
				QUaChangeVerb::ReferenceAdded // UaExpert does not recognize QUaChangeVerb::NodeAdded
			});
		}
	}
#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS
	return newInstance;
}

QUaNode* QUaNode::cloneNode(
	QUaNode* parentNode/* = nullptr*/,
	const QUaQualifiedName&  browseName/* = ""*/,
	const QUaNodeId& nodeId/* = ""*/
)
{
	// create new clean instance of the same type
	UA_NodeId newInstanceNodeId = _qUaServer->createInstanceInternal(
		*this->metaObject(),
		parentNode,
		browseName.isEmpty() ? this->browseName() :  browseName,
		nodeId
	);
	if (UA_NodeId_isNull(&newInstanceNodeId))
	{
		UA_NodeId_clear(&newInstanceNodeId);
		return nullptr;
	}
	// get new c++ instance
	auto tmp = QUaNode::getNodeContext(newInstanceNodeId, _qUaServer->_server);
	QUaNode* newInstance = qobject_cast<QUaNode*>(tmp);
	Q_CHECK_PTR(newInstance);
	UA_NodeId_clear(&newInstanceNodeId);
	if (!newInstance)
	{
		return newInstance;
	}
	// serialize this instance to memory
	QQueue<QUaLog> logOut;
	QUaInMemorySerializer serializer;
	this->serialize(serializer, logOut);

	//// [DEBUG]
	//serializer.qtDebug(
	//	_qUaServer, 
	//	tr("*************** %1 ***************").arg(this->nodeId())
	//);

	// replace nodeId
	Q_ASSERT(serializer._hashNodeTreeData.contains(this->nodeId()));
	serializer._hashNodeTreeData[newInstance->nodeId()] =
		serializer._hashNodeTreeData.take(this->nodeId());
	// replace browseName
	Q_ASSERT(serializer._hashNodeTreeData[newInstance->nodeId()].attrs.contains( QStringLiteral("browseName") ));
	serializer._hashNodeTreeData[newInstance->nodeId()].attrs[ QStringLiteral("browseName") ] =
		newInstance->browseName().toXmlString();
	// deserialize to new instance
	newInstance->deserialize(serializer, logOut);

	//// [DEBUG]
	////serializer.clear();
	//newInstance->serialize(serializer, logOut);
	//serializer.qtDebug(
	//	_qUaServer, 
	//	tr("*************** %1 ***************").arg(this->nodeId())
	//);
	// return new instance
	return newInstance;
}

const QUaSession* QUaNode::currentSession() const
{
	return _qUaServer->_currentSession;
}

bool QUaNode::hasOptionalMethod(const QUaQualifiedName& methodName) const
{
	// get all ua methods of INSTANCE
	auto methodsNodeIds = QUaNode::getMethodsNodeIds(_nodeId, _qUaServer->_server);
	for (const auto & methNodeId : std::as_const(methodsNodeIds))
	{
		// ignore if not optional
		if (!QUaNode::hasOptionalModellingRule(methNodeId, _qUaServer->_server))
		{
			continue;
		}
		// ignore if browse name match
		QUaQualifiedName methBrowseName = QUaNode::getBrowseName(methNodeId, _qUaServer->_server);
		if (methodName == methBrowseName)
		{
			// cleanup
			for (auto & methodNodeId : methodsNodeIds)
			{
				UA_NodeId_clear(&methodNodeId);
			}
			return true;
		}
	}
	// cleanup
	for (auto & methodNodeId : methodsNodeIds)
	{
		UA_NodeId_clear(&methodNodeId);
	}
	return false;
}

bool QUaNode::addOptionalMethod(const QUaQualifiedName& methodName)
{
	// do not add twice
	if (this->hasOptionalMethod(methodName))
	{
		return true;
	}
	UA_NodeId typeNodeId = QUaNode::typeDefinitionNodeId(_nodeId, _qUaServer->_server);
	UA_NodeId methodNodeId = UA_NODEID_NULL;
	// look for optional method starting from this type of to base object type
    static UA_NodeId baseObjType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE);
    while (!UA_NodeId_equal(&typeNodeId, &baseObjType))
		{
		// get all ua methods of TYPE
		auto methodsNodeIds = QUaNode::getMethodsNodeIds(typeNodeId, _qUaServer->_server);
		for (const auto & methNodeId : std::as_const(methodsNodeIds))
		{
			// ignore if not optional
			if (!QUaNode::hasOptionalModellingRule(methNodeId, _qUaServer->_server))
			{
				continue;
			}
			// ignore if browse name does not match
			QUaQualifiedName methBrowseName = QUaNode::getBrowseName(methNodeId, _qUaServer->_server);
			if (methodName != methBrowseName)
			{
				continue;
			}
			methodNodeId = methNodeId;
			break;
		}
		// cleanup
		for (auto & methNodeId : methodsNodeIds)
		{
			UA_NodeId_clear(&methNodeId);
		}
		// check if found
		if (!UA_NodeId_isNull(&methodNodeId))
		{
			break;
		}
		UA_NodeId typeNodeIdNew = QUaNode::superTypeDefinitionNodeId(typeNodeId, _qUaServer->_server);
		UA_NodeId_clear(&typeNodeId);
		UA_NodeId_copy(&typeNodeIdNew, &typeNodeId);
		UA_NodeId_clear(&typeNodeIdNew);
	}
	UA_NodeId_clear(&typeNodeId);
	Q_ASSERT_X(!UA_NodeId_isNull(&methodNodeId),
		"QUaNode::addOptionalMethod", "Could not find optional method.");
	if (UA_NodeId_isNull(&methodNodeId))
	{
		return false;
	}
	// add reference from instance to method
	auto st = UA_Server_addReference(
		_qUaServer->_server,
		_nodeId,
		UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
		{ methodNodeId, UA_STRING_NULL, 0 },
		true
	);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	if (st != UA_STATUSCODE_GOOD)
	{
		return false;
	}
	return true;
}

bool QUaNode::removeOptionalMethod(const QUaQualifiedName& methodName)
{
	// do not remove twice
	if (!this->hasOptionalMethod(methodName))
	{
		return true;
	}
	UA_NodeId typeNodeId = QUaNode::typeDefinitionNodeId(_nodeId, _qUaServer->_server);
	// get all ua methods of TYPE
	auto methodsNodeIds = QUaNode::getMethodsNodeIds(typeNodeId, _qUaServer->_server);
	UA_NodeId methodNodeId = UA_NODEID_NULL;
	for (const auto & methNodeId : std::as_const(methodsNodeIds))
	{
		// ignore if not optional
		if (!QUaNode::hasOptionalModellingRule(methNodeId, _qUaServer->_server))
		{
			continue;
		}
		// ignore if browse name does not match
		QUaQualifiedName methBrowseName = QUaNode::getBrowseName(methNodeId, _qUaServer->_server);
		if (methodName != methBrowseName)
		{
			continue;
		}
		methodNodeId = methNodeId;
		break;
	}
	Q_ASSERT_X(!UA_NodeId_isNull(&methodNodeId),
		"QUaNode::addOptionalMethod", "Could not find optional method.");
	if (UA_NodeId_isNull(&methodNodeId))
	{
		// cleanup
		for (auto & methodNodeId : methodsNodeIds)
		{
			UA_NodeId_clear(&methodNodeId);
		}
		UA_NodeId_clear(&typeNodeId);
		return false;
	}
	// remove reference from instance to method
	auto st = UA_Server_deleteReference(
		_qUaServer->_server,
		_nodeId,
		UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
		true,
		{ methodNodeId, UA_STRING_NULL, 0 },
		true
	);
	// cleanup
	for (auto & methodNodeId : methodsNodeIds)
	{
		UA_NodeId_clear(&methodNodeId);
	}
	UA_NodeId_clear(&typeNodeId);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	if (st != UA_STATUSCODE_GOOD)
	{
		return false;
	}
	return true;
}

bool QUaNode::inAddressSpace() const
{
	if (this == _qUaServer->_pobjectsFolder)
	{
		return true;
	}
	QUaNode* parent = qobject_cast<QUaNode*>(this->parent());
	return parent && (parent == _qUaServer->_pobjectsFolder || parent->inAddressSpace());
}

const QMap<QString, QVariant> QUaNode::serializeAttrs() const
{
	QMap<QString, QVariant> retMap;
	// first serialize browseName
	retMap[QStringLiteral("browseName")] = this->browseName().toXmlString();
	// list meta props
	auto metaObject = this->metaObject();
	int  propCount  = metaObject->propertyCount();
	int  propOffset = QUaNode::staticMetaObject.propertyOffset();
	for (int i = propOffset; i < propCount; i++)
	{
		QMetaProperty metaProperty = metaObject->property(i);
		QString strPropName = QString::fromUtf8(metaProperty.name());
		Q_ASSERT(metaProperty.isReadable());
		// non-writabe props cannot be restored
		if (!metaProperty.isWritable())
		{
			continue;
		}
		Q_ASSERT(!retMap.contains(strPropName));
		// ignore QUaNode * properties (type children)
		QMetaType metaType( metaProperty.userType() );
		auto propMetaObject = metaType.metaObject();
		if (propMetaObject && propMetaObject->inherits(&QUaNode::staticMetaObject))
		{
			continue;
		}
		// check known type
		if (metaType.isValid())
		{
			// get the Qt meta property name
			retMap[strPropName] = metaProperty.read(this);
		}
	}
	return retMap;
}

const QList<QUaForwardReference> QUaNode::serializeRefs() const
{
	QList<QUaForwardReference> retList;
	// serialize all reference types
	for (const auto & refType : _qUaServer->referenceTypes())
	{
		const auto references = this->findReferences(refType);
		for (const auto & ref : references)
		{
			QUaForwardReference fRef = { 
				ref->nodeId(),
				QString::fromUtf8( ref->metaObject()->className() ),
				refType 
			};
			retList << fRef;
		}
	}
	return retList;
}

void QUaNode::deserializeAttrs(
	const QMap<QString, QVariant>& attrs, 
	QQueue<QUaLog>& logOut)
{
	QStringList listAttrsNotInProps = attrs.keys();
	QStringList listPropsNotInAttrs;
	// first deserialize browseName
	Q_ASSERT_X(
		attrs.contains( QStringLiteral("browseName") ),
		"QUaNode::deserializeAttrs", 
		"Deserialized attributes must contain the browseName"
	);
	Q_ASSERT_X(
		QUaQualifiedName::fromXmlString(attrs[ QStringLiteral("browseName") ].toString()) == this->browseName(),
		"QUaNode::deserializeAttrs",
		"Deserialized browseName does not match instance browseName"
	);
	listAttrsNotInProps.removeOne( QStringLiteral("browseName") );
	// open62541 accepts a ValueRank above one (a multi-dimensional array) only while the variable has no value
	auto variable = qobject_cast<QUaBaseVariable*>(this);
	const QVariant valueRank = attrs.value(QStringLiteral("valueRank"));
	if (variable && valueRank.toInt() > 1)
	{
		variable->setValueRank(valueRank.toInt());
	}
	// list meta props
	auto metaObject = this->metaObject();
	int  propCount  = metaObject->propertyCount();
	int  propOffset = QUaNode::staticMetaObject.propertyOffset();
	for (int i = propOffset; i < propCount; i++)
	{
		QMetaProperty metaProperty = metaObject->property(i);
		QString strPropName = QString::fromUtf8(metaProperty.name());
		Q_ASSERT(metaProperty.isReadable());
		// non-writabe props cannot be restored
		if (!metaProperty.isWritable())
		{
			continue;
		}
		// ignore QUaNode * properties (type children)
		QMetaType metaType( metaProperty.userType() );
		auto propMetaObject = metaType.metaObject();
		if (propMetaObject && propMetaObject->inherits(&QUaNode::staticMetaObject))
		{
			continue;
		}
		// check known type
		if (!metaType.isValid())
		{
			continue;
		}
		// check if metaprop in attrs
		if (attrs.contains(strPropName))
		{
			// remove from shame list
			bool ok = listAttrsNotInProps.removeOne(strPropName);
			Q_ASSERT(ok);
			// write property
			auto val = attrs[strPropName]; // 6.34[%]
			if (val.isValid() && !val.isNull())
			{
				ok = metaProperty.write(this, val);
				Q_ASSERT_X(ok, "QUaNode::deserializeAttrs", "Invalid value for deserialized attribute.");
			}
			continue;
		}
		// else add to shame list
		listPropsNotInAttrs << strPropName;
	}
	// check props not in attrs
	if (!listPropsNotInAttrs.isEmpty())
	{
		logOut.enqueue({
			tr("Node %1 has attributes that were not found in data; %2. Ignoring.")
				.arg(this->nodeId())
				.arg(listPropsNotInAttrs.join( QLatin1String(", ") )),
			QUaLogLevel::Warning,
			QUaLogCategory::Serialization
		});
	}
	// check attrs not in props
	if (!listAttrsNotInProps.isEmpty())
	{
		logOut.enqueue({
			tr("Node %1 does not have attributes that were found in data; %2. Ignoring.")
				.arg(this->nodeId())
				.arg(listAttrsNotInProps.join( QLatin1String(", ") )),
			QUaLogLevel::Warning,
			QUaLogCategory::Serialization
		});
	}
}

QUaWriteMask QUaNode::userWriteMask(const QString & strUserName)
{
	// if has a node parent, use parent's callback
	QUaNode * parent = qobject_cast<QUaNode*>(this->parent());
	if (parent)
	{
		return parent->userWriteMask(strUserName);
	}
	// else all permissions
	return 0xFFFFFFFF;
}

QUaAccessLevel QUaNode::userAccessLevel(const QString & strUserName)
{
	// if has a node parent, use parent's callback
	QUaNode * parent = qobject_cast<QUaNode*>(this->parent());
	if (parent)
	{
		return parent->userAccessLevelInternal(strUserName);
	}
	// else all permissions
	return 0xFF;
}

bool QUaNode::userExecutable(const QString & strUserName)
{
	// if has a node parent, use parent's callback
	QUaNode * parent = qobject_cast<QUaNode*>(this->parent());
	if (parent)
	{
		return parent->userExecutable(strUserName);
	}
	// else allow
	return true;
}

QString QUaNode::className() const
{
	return QString::fromUtf8(this->metaObject()->className());
}

///
/// \brief Returns the source of the first inverse reference of \a referenceTypeId or its subtypes, null when none.
///        The result must be cleared.
///
UA_NodeId QUaNode::getFirstInverseReferenceSource(const UA_NodeId& nodeId, const UA_NodeId& referenceTypeId,
                                                  UA_Server* server)
{
	UA_BrowseDescription bDesc;
	UA_BrowseDescription_init(&bDesc);
	bDesc.nodeId          = nodeId;
	bDesc.referenceTypeId = referenceTypeId;
	bDesc.browseDirection = UA_BROWSEDIRECTION_INVERSE;
	bDesc.includeSubtypes = true;
	bDesc.resultMask      = UA_BROWSERESULTMASK_NONE;
	UA_BrowseResult bRes = UA_Server_browse(server, 0, &bDesc);
	Q_ASSERT(bRes.statusCode == UA_STATUSCODE_GOOD);
	UA_NodeId sourceNodeId = UA_NODEID_NULL;
	if (bRes.referencesSize > 0)
	{
		UA_NodeId_copy(&bRes.references[0].nodeId.nodeId, &sourceNodeId);
	}
	UA_BrowseResult_clear(&bRes);
	return sourceNodeId;
}

///
/// \brief Returns the parent of a node: the source of a HasChild reference (e.g. HasComponent), else of any other
///        hierarchical reference such as Organizes. Nodes may have several parents, as methods shared by the instances
///        of a type or nodes of a NodeSet; the first one is returned. The result must be cleared.
///
UA_NodeId QUaNode::getParentNodeId(const UA_NodeId & childNodeId, UA_Server * server)
{
	UA_NodeId parentNodeId = QUaNode::getFirstInverseReferenceSource(
		childNodeId, UA_NODEID_NUMERIC(0, UA_NS0ID_HASCHILD), server);
	if (UA_NodeId_isNull(&parentNodeId))
	{
		parentNodeId = QUaNode::getFirstInverseReferenceSource(
			childNodeId, UA_NODEID_NUMERIC(0, UA_NS0ID_HIERARCHICALREFERENCES), server);
	}
	return parentNodeId;
}

// NOTE : need to cleanup result after calling this method
QList<UA_NodeId> QUaNode::getChildrenNodeIds(
	const UA_NodeId & parentNodeId, 
	UA_Server * server,
	const UA_UInt32& nodeClassMask, /* = UA_NODECLASS_OBJECT | UA_NODECLASS_VARIABLE*/
	const UA_NodeId& referenceTypeId /*= UA_NODEID_NULL*/
	// NOTE : by default browse only objects or variables (no types or refs)
)
{
	QList<UA_NodeId> retListChildren;
	UA_BrowseDescription * bDesc = UA_BrowseDescription_new();
	UA_NodeId_copy(&parentNodeId, &bDesc->nodeId); // from parent
	UA_NodeId_copy(&referenceTypeId, &bDesc->referenceTypeId);
	bDesc->browseDirection = UA_BROWSEDIRECTION_FORWARD; //  look downwards
	bDesc->includeSubtypes = false;
	bDesc->nodeClassMask   = nodeClassMask; 
	bDesc->resultMask      = UA_BROWSERESULTMASK_NONE; // only need node ids
	// browse
	UA_BrowseResult bRes = UA_Server_browse(server, 0, bDesc);
	Q_ASSERT_X(bRes.statusCode == UA_STATUSCODE_GOOD, "QUaNode::getChildrenNodeIds", "Browsing children failed.");
	if (bRes.statusCode != UA_STATUSCODE_GOOD)
	{
		return retListChildren;
	}
	while (bRes.referencesSize > 0)
	{
		for (size_t i = 0; i < bRes.referencesSize; i++)
		{
			UA_ReferenceDescription rDesc = bRes.references[i];
			// ignore modelling rules
            static UA_NodeId ruleMandatory = UA_NODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY);
            static UA_NodeId ruleOptional  = UA_NODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_OPTIONAL);
            if (UA_NodeId_equal(
					&rDesc.nodeId.nodeId, 
                    &ruleMandatory
				) ||
				UA_NodeId_equal(
					&rDesc.nodeId.nodeId,
                    &ruleOptional
				))
			{
				continue;
			}
			UA_NodeId nodeId;
			UA_NodeId_copy(&rDesc.nodeId.nodeId, &nodeId);
			Q_ASSERT(!retListChildren.contains(nodeId));
			retListChildren << nodeId;
		}
        UA_BrowseResult_clear(&bRes);
		bRes = UA_Server_browseNext(server, true, &bRes.continuationPoint);
	}
	// cleanup
    UA_BrowseDescription_clear(bDesc);
	UA_BrowseDescription_delete(bDesc);
    UA_BrowseResult_clear(&bRes);
	// return
	return retListChildren;
}

// NOTE : need to cleanup result after calling this method
QList<UA_NodeId> QUaNode::getMethodsNodeIds(const UA_NodeId& parentNodeId, UA_Server* server)
{
	QList<UA_NodeId> retListMethods;
	UA_BrowseDescription* bDesc = UA_BrowseDescription_new();
	UA_NodeId_copy(&parentNodeId, &bDesc->nodeId); // from parent
	bDesc->browseDirection = UA_BROWSEDIRECTION_FORWARD; //  look downwards
	bDesc->includeSubtypes = false;
	bDesc->nodeClassMask = UA_NODECLASS_METHOD; // only methods
	bDesc->resultMask = UA_BROWSERESULTMASK_NONE; // only need node ids
	// browse
	UA_BrowseResult bRes = UA_Server_browse(server, 0, bDesc);
	assert(bRes.statusCode == UA_STATUSCODE_GOOD);
	while (bRes.referencesSize > 0)
	{
		for (size_t i = 0; i < bRes.referencesSize; i++)
		{
			UA_ReferenceDescription rDesc = bRes.references[i];
			UA_NodeId nodeId/* = rDesc.nodeId.nodeId*/;
			UA_NodeId_copy(&rDesc.nodeId.nodeId, &nodeId);
			Q_ASSERT(!retListMethods.contains(nodeId));
			retListMethods << nodeId;
		}
        UA_BrowseResult_clear(&bRes);
		bRes = UA_Server_browseNext(server, true, &bRes.continuationPoint);
	}
	// cleanup
    UA_BrowseDescription_clear(bDesc);
	UA_BrowseDescription_delete(bDesc);
    UA_BrowseResult_clear(&bRes);
	// return
	return retListMethods;
}

QUaNode::QUaEventFieldMetaData QUaNode::getTypeVars(
	const QUaNodeId& typeNodeId, 
	UA_Server* server)
{
	QSet<QUaBrowsePath> alreadyAdded;
	QUaEventFieldMetaData retNames;
	UA_NodeId typeUaNodeId = typeNodeId;
	// look for children starting from this type up to base object type
    static UA_NodeId baseObjType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE);
    static UA_NodeId baseVarType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEVARIABLETYPE);
    while (!UA_NodeId_equal(&typeUaNodeId, &baseObjType) &&
           !UA_NodeId_equal(&typeUaNodeId, &baseVarType))
	{
		// variable children
		auto varsNodeIds = QUaNode::getChildrenNodeIds(
			typeUaNodeId, 
			server,
			UA_NODECLASS_VARIABLE
		);
		for (const auto & varNodeId : std::as_const(varsNodeIds))
		{
			// only children that have a modelling rule
			UA_NodeId modellingRule = QUaNode::getModellingRule(varNodeId, server);
			if (UA_NodeId_isNull(&modellingRule))
			{
				continue;
			}
			// ignore if browse path already added
			QUaQualifiedName browseName = QUaNode::getBrowseName(varNodeId, server);
			QUaBrowsePath browsePath = QUaBrowsePath() << browseName;
			if (alreadyAdded.contains(browsePath))
			{
				continue;
			}
			// read type
			UA_NodeId outDataType;
			auto st = UA_Server_readDataType(server, varNodeId, &outDataType);
			Q_ASSERT(st == UA_STATUSCODE_GOOD);
			Q_UNUSED(st);
			QMetaType::Type qType = QUaDataType::qTypeByNodeId(outDataType);
			// ignore type for which there is not yet a Qt type
			if (qType == QMetaType::UnknownType)
			{
				// TODO : support these types
				continue;
			}
			// read valueRank
			qint32 outValueRank;
			st = UA_Server_readValueRank(server, varNodeId, &outValueRank);
			Q_ASSERT(st == UA_STATUSCODE_GOOD);
			Q_UNUSED(st);
			if (outValueRank != UA_VALUERANK_SCALAR)
			{
				// TODO : undefined so far
				//Q_ASSERT_X(outValueRank == UA_VALUERANK_ANY, 
				//	"QUaNode::getTypeVars", 
				//	"Not Supported!");
				QByteArray byteType = QByteArrayLiteral("QList<") + QMetaType(qType).name() + '>';
				qType = static_cast<QMetaType::Type>( QMetaType::fromName(byteType).id() );
				// fallback for array types not registered in Qt
				if (qType == QMetaType::UnknownType)
				{
					qType = QMetaType::QVariantList;
				}
			}
			// add to return list
			Q_ASSERT(!retNames.contains(browsePath));
			retNames[browsePath] = qType;
			// add to already read
			alreadyAdded << browsePath;
			// recurse variables's children (e.g. to get Id of two state variables)
			UA_NodeId childTypeId = QUaNode::typeDefinitionNodeId(varNodeId, server);
			auto childrenVars = QUaNode::getTypeVars(childTypeId, server);
			UA_NodeId_clear(&childTypeId);
			// prepend parent browse name and add to return list
			QUaEventFieldMetaDataIter i = childrenVars.begin();
			while (i != childrenVars.end())
			{
				QUaBrowsePath browsePath = i.key();
				browsePath.prepend(browseName);
				// add to return list
				Q_ASSERT(!retNames.contains(browsePath));
				retNames[browsePath] = i.value();
				// add to already read
				alreadyAdded << browsePath;
				i++;
			}
		}
		// cleanup
		for (auto & varNodeId : varsNodeIds)
		{
			UA_NodeId_clear(&varNodeId);
		}
		UA_NodeId typeNodeIdNew = QUaNode::superTypeDefinitionNodeId(typeUaNodeId, server);
		Q_ASSERT_X(!UA_NodeId_isNull(&typeNodeIdNew), "QUaNode::getTypeVars", "Invalid super type NodeId");
		if (UA_NodeId_isNull(&typeNodeIdNew)) 
		{
			break;
		}
		UA_NodeId_clear(&typeUaNodeId);
		UA_NodeId_copy (&typeNodeIdNew, &typeUaNodeId);
		UA_NodeId_clear(&typeNodeIdNew);
	}
	return retNames;
}

QUaNode * QUaNode::getNodeContext(const UA_NodeId & nodeId, UA_Server * server)
{
	void * context = QUaNode::getVoidContext(nodeId, server);
	// try to cast to C++ node, dynamic_cast check is necessary
	return QUaNode::fromVoidContext(context);
}

QSet<const void*> QUaNode::_liveNodes;

QUaNode* QUaNode::fromVoidContext(void* context)
{
	if (!context || !_liveNodes.contains(context))
	{
		return nullptr;
	}
	return static_cast<QUaNode*>(context);
}

void * QUaNode::getVoidContext(const UA_NodeId & nodeId, UA_Server * server)
{
	// get void context
	void * context;
	auto st = UA_Server_getNodeContext(server, nodeId, &context);
	if (st != UA_STATUSCODE_GOOD)
	{
		return nullptr;
	}
	// try to cast to C++ node
	return context;
}

QUaQualifiedName QUaNode::getBrowseName(const UA_NodeId & nodeId, UA_Server * server)
{
	// read browse name
	UA_QualifiedName outBrowseName;
	auto st = UA_Server_readBrowseName(server, nodeId, &outBrowseName);
	Q_ASSERT(st == UA_STATUSCODE_GOOD);
	Q_UNUSED(st);
	QUaQualifiedName  browseName = outBrowseName;
	UA_QualifiedName_clear(&outBrowseName);
	return  browseName;
}

UA_NodeId QUaNode::getModellingRule(const UA_NodeId& nodeId, UA_Server* server)
{
	UA_BrowseDescription * bDesc = UA_BrowseDescription_new();
	UA_NodeId_copy(&nodeId, &bDesc->nodeId); // from parent
        // GCC does not like : UA_NodeId_copy(&UA_NODEID_NUMERIC(0, UA_NS0ID_HASMODELLINGRULE), &bDesc->referenceTypeId);
        bDesc->referenceTypeId = UA_NODEID_NUMERIC(0, UA_NS0ID_HASMODELLINGRULE);
	bDesc->browseDirection = UA_BROWSEDIRECTION_FORWARD; //  look downwards
	bDesc->includeSubtypes = false;
	bDesc->nodeClassMask   = UA_NODECLASS_OBJECT; // in specific UA_NS0ID_MODELLINGRULE_MANDATORY 78 /* Object */
	bDesc->resultMask      = UA_BROWSERESULTMASK_NONE; // no info needed, only existance
	// browse
	UA_BrowseResult bRes = UA_Server_browse(server, 0, bDesc);
	Q_ASSERT(bRes.statusCode == UA_STATUSCODE_GOOD);
	UA_NodeId modellingRule = UA_NODEID_NULL;
	if (bRes.referencesSize > 0)
	{
		Q_ASSERT(bRes.referencesSize == 1);
		// check if modelling rule is mandatory
		UA_ReferenceDescription rDesc = bRes.references[0];
		modellingRule = rDesc.nodeId.nodeId;		
	}
	// cleanup
    UA_BrowseDescription_clear(bDesc);
	UA_BrowseDescription_delete(bDesc);
    UA_BrowseResult_clear(&bRes);
	// return
	return modellingRule;
}

bool QUaNode::hasMandatoryModellingRule(const UA_NodeId& nodeId, UA_Server* server)
{
    static UA_NodeId ruleMandatory = UA_NODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_MANDATORY);
	UA_NodeId modellingRule = QUaNode::getModellingRule(nodeId, server);
    return UA_NodeId_equal(&modellingRule, &ruleMandatory);
}

bool QUaNode::hasOptionalModellingRule(const UA_NodeId& nodeId, UA_Server* server)
{
    static UA_NodeId ruleOptional  = UA_NODEID_NUMERIC(0, UA_NS0ID_MODELLINGRULE_OPTIONAL);
	UA_NodeId modellingRule = QUaNode::getModellingRule(nodeId, server);
    return UA_NodeId_equal(&modellingRule, &ruleOptional);
}

int QUaNode::getPropsOffsetHelper(const QMetaObject & metaObject)
{
	int propOffset;
	// need to set props also inherited from base class
	if (metaObject.inherits(&QUaBaseVariable::staticMetaObject))
	{
		propOffset = QUaBaseVariable::staticMetaObject.propertyOffset();
	}
	else
	{
		// [NOTE] : assert below does not work, dont know why
		//Q_ASSERT(metaObject.inherits(&QUaBaseObject::staticMetaObject));
		propOffset = QUaBaseObject::staticMetaObject.propertyOffset();
	}
	return propOffset;
}

QDebug operator<<(QDebug debug, const QUaReferenceType& refType)
{
	QDebugStateSaver saver(debug);
	debug.nospace() << '{' << refType.strForwardName << ", " << refType.strInverseName << '}';
	return debug;
}
