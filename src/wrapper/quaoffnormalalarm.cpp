#include "quaoffnormalalarm.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

#include <QUaServer>

QUaOffNormalAlarm::QUaOffNormalAlarm(
	QUaServer* server
) : QUaDiscreteAlarm(server)
{
	
}

void QUaOffNormalAlarm::setInputNode(QUaBaseVariable* inputNode)
{
	// call base implementation
	QUaAlarmCondition::setInputNode(inputNode);
	if (!inputNode)
	{
		return;
	}
	// subscribe to value changes
	_connections <<
	QObject::connect(_inputNode, &QUaBaseVariable::valueChanged, this,
	[this](const QVariant& value) {
		if (!_normalValue.isValid())
		{
			return;
		}
		this->setActive(value != _normalValue);
	});
}

QVariant QUaOffNormalAlarm::normalValue() const
{
	return _normalValue;
}

void QUaOffNormalAlarm::setNormalValue(const QVariant& normalValue)
{
	if (normalValue == this->normalValue())
	{
		return;
	}
	_normalValue = normalValue;
	// trigger active state recalculation
	if (!_inputNode)
	{
		return;
	}
	emit _inputNode->valueChanged(_inputNode->value(), false);
}

QUaNodeId QUaOffNormalAlarm::normalState() const
{
	return const_cast<QUaOffNormalAlarm*>(this)->getNormalState()->value<QUaNodeId>();
}

void QUaOffNormalAlarm::setNormalState(const QUaNodeId& normalState)
{
	this->getNormalState()->setValue(normalState);
}

QUaProperty* QUaOffNormalAlarm::getNormalState()
{
	return this->browseChild<QUaProperty>("NormalState");
}

#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS