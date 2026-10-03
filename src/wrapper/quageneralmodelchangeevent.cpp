#include "quageneralmodelchangeevent.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS

#include <QUaServer>

QUaGeneralModelChangeEvent::QUaGeneralModelChangeEvent(
	QUaServer *server
) : QUaBaseModelChangeEvent(server)
{
	_changes = nullptr;
#ifdef UA_ENABLE_HISTORIZING
	_historizing = false;
#endif // UA_ENABLE_HISTORIZING
}

QUaChangesList QUaGeneralModelChangeEvent::changes() const
{
	if (!_changes)
	{
		auto _thiz = const_cast<QUaGeneralModelChangeEvent*>(this);
		_thiz->_changes = _thiz->getChanges();
	}
	QUaChangesList retList;
	QVariant varList = _changes->value();
	if (!varList.isValid() || !varList.canConvert<QVariantList>())
	{
		return retList;
	}
	auto iter = varList.value<QSequentialIterable>();
	for (const QVariant &v : iter)
	{
		retList << v.value<QUaChangeStructureDataType>();
	}
	return retList;
}

void QUaGeneralModelChangeEvent::setChanges(const QUaChangesList & listVerbs)
{
	if (!_changes)
	{
		_changes = this->getChanges();
	}
	_changes->setValue(listVerbs);
}

QUaProperty * QUaGeneralModelChangeEvent::getChanges()
{
	return this->browseChild<QUaProperty>("Changes");
}

#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS