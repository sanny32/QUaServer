#include "quaalarmcondition.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

#include <QUaServer>
#include <QUaTwoStateVariable>
#include <QUaConditionVariable>

QUaAlarmCondition::QUaAlarmCondition(
	QUaServer *server
) : QUaAcknowledgeableCondition(server)
{
	_inputNode = nullptr;
	// resue rest of defaults 
	this->resetInternals();
	this->addOptionalMethod("GetGroupMemberships");
	QObject::connect(this, &QUaAcknowledgeableCondition::conditionAcknowledged,
		this, &QUaAlarmCondition::silenceOnAcknowledge);
}

QUaAlarmCondition::~QUaAlarmCondition()
{
	this->cleanConnections();
}

QUaLocalizedText QUaAlarmCondition::activeStateCurrentStateName() const
{
	return const_cast<QUaAlarmCondition*>(this)->getActiveState()->currentStateName();
}

void QUaAlarmCondition::setActiveStateCurrentStateName(const QUaLocalizedText& activeState)
{
	this->getActiveState()->setCurrentStateName(activeState);
}

bool QUaAlarmCondition::activeStateId() const
{
	return const_cast<QUaAlarmCondition*>(this)->getActiveState()->id();
}

void QUaAlarmCondition::setActiveStateId(const bool& activeStateId)
{
	this->getActiveState()->setId(activeStateId);
}

QDateTime QUaAlarmCondition::activeStateTransitionTime() const
{
	return const_cast<QUaAlarmCondition*>(this)->getActiveState()->transitionTime();
}

void QUaAlarmCondition::setActiveStateTransitionTime(const QDateTime& transitionTime)
{
	this->getActiveState()->setTransitionTime(transitionTime);
}

QUaLocalizedText QUaAlarmCondition::activeStateTrueState() const
{
	return const_cast<QUaAlarmCondition*>(this)->getActiveState()->trueState();
}

void QUaAlarmCondition::setActiveStateTrueState(const QUaLocalizedText& trueState)
{
	this->getActiveState()->setTrueState(trueState);
}

QUaLocalizedText QUaAlarmCondition::activeStateFalseState() const
{
	return const_cast<QUaAlarmCondition*>(this)->getActiveState()->falseState();
}

void QUaAlarmCondition::setActiveStateFalseState(const QUaLocalizedText& falseState)
{
	this->getActiveState()->setFalseState(falseState);
}

QUaNodeId QUaAlarmCondition::inputNode() const
{
	return const_cast<QUaAlarmCondition*>(this)->getInputNode()->value<QUaNodeId>();
}

void QUaAlarmCondition::setInputNode(const QUaNodeId& inputNodeId)
{
	QUaNode * node = _qUaServer->nodeById(inputNodeId);
	QUaBaseVariable* var = qobject_cast<QUaBaseVariable*>(node);
	if (node && !var)
	{
		Q_ASSERT_X(false, "QUaAlarmCondition::setInputNode", "Input node must be a variable.");
		// TODO : error log
	}
	this->setInputNode(var);
}

void QUaAlarmCondition::setInputNode(QUaBaseVariable* inputNode)
{
	this->cleanConnections();
	_inputNode = inputNode;
	// set nodeId
	this->getInputNode()->setValue(_inputNode ? _inputNode->nodeId() : QUaNodeId());
	// subscribe to source changes
	if (!_inputNode)
	{
		return;
	}
	_connections <<
	QObject::connect(_inputNode, &QObject::destroyed, this,
	[this]() {
		this->cleanConnections();
		this->setInputNode(nullptr);
	});
}

bool QUaAlarmCondition::suppressed() const
{
	auto state = this->optionalState("SuppressedState");
	return state && state->id();
}

void QUaAlarmCondition::setSuppressed(const bool& suppressed)
{
	auto state = this->getSuppressedState();
	if (!this->changeState(state, suppressed))
	{
		return;
	}
	this->updateSuppressedOrShelve();
	this->triggerStateChange(state);
	emit this->suppressedChanged();
}

bool QUaAlarmCondition::outOfService() const
{
	auto state = this->optionalState("OutOfServiceState");
	return state && state->id();
}

void QUaAlarmCondition::setOutOfService(const bool& outOfService)
{
	auto state = this->getOutOfServiceState();
	if (!this->changeState(state, outOfService))
	{
		return;
	}
	this->updateSuppressedOrShelve();
	this->triggerStateChange(state);
	emit this->outOfServiceChanged();
}

bool QUaAlarmCondition::silenced() const
{
	auto state = this->optionalState("SilenceState");
	return state && state->id();
}

void QUaAlarmCondition::setSilenced(const bool& silenced)
{
	auto state = this->getSilenceState();
	if (!this->changeState(state, silenced))
	{
		return;
	}
	this->triggerStateChange(state);
	emit this->silencedChanged();
}

bool QUaAlarmCondition::latched() const
{
	auto state = this->optionalState("LatchedState");
	return state && state->id();
}

void QUaAlarmCondition::setLatched(const bool& latched)
{
	auto state = this->getLatchedState();
	if (!this->changeState(state, latched))
	{
		return;
	}
	this->setRetain(this->requiresAttention() || this->hasBranches());
	this->triggerStateChange(state);
	emit this->latchedChanged();
}

bool QUaAlarmCondition::suppressedOrShelve() const
{
	return const_cast<QUaAlarmCondition*>(this)->getSuppressedOrShelve()->value<bool>();
}

void QUaAlarmCondition::setSuppressedOrShelve(const bool& suppressedOrShelve)
{
	this->getSuppressedOrShelve()->setValue(suppressedOrShelve);
}

void QUaAlarmCondition::Silence()
{
	if (!this->checkMethodSupported(this->silenceSupported()))
	{
		return;
	}
	// spec : ignored if not audible, i.e. already silenced
	this->setSilenced(true);
}

void QUaAlarmCondition::Suppress()
{
	this->Suppress2(QUaLocalizedText());
}

void QUaAlarmCondition::Suppress2(QUaLocalizedText Comment)
{
	if (!this->checkMethodSupported(this->suppressionSupported()))
	{
		return;
	}
	this->applyComment(Comment, !this->suppressed());
	this->setSuppressed(true);
}

void QUaAlarmCondition::Unsuppress()
{
	this->Unsuppress2(QUaLocalizedText());
}

void QUaAlarmCondition::Unsuppress2(QUaLocalizedText Comment)
{
	if (!this->checkMethodSupported(this->suppressionSupported()))
	{
		return;
	}
	this->applyComment(Comment, this->suppressed());
	this->setSuppressed(false);
}

void QUaAlarmCondition::RemoveFromService()
{
	this->RemoveFromService2(QUaLocalizedText());
}

void QUaAlarmCondition::RemoveFromService2(QUaLocalizedText Comment)
{
	if (!this->checkMethodSupported(this->outOfServiceSupported()))
	{
		return;
	}
	this->applyComment(Comment, !this->outOfService());
	this->setOutOfService(true);
}

void QUaAlarmCondition::PlaceInService()
{
	this->PlaceInService2(QUaLocalizedText());
}

void QUaAlarmCondition::PlaceInService2(QUaLocalizedText Comment)
{
	if (!this->checkMethodSupported(this->outOfServiceSupported()))
	{
		return;
	}
	this->applyComment(Comment, this->outOfService());
	this->setOutOfService(false);
}

void QUaAlarmCondition::Reset()
{
	this->Reset2(QUaLocalizedText());
}

void QUaAlarmCondition::Reset2(QUaLocalizedText Comment)
{
	if (!this->checkMethodSupported(this->latchingSupported()))
	{
		return;
	}
	if (!this->latched() || this->active() || QUaAcknowledgeableCondition::requiresAttention())
	{
		this->setMethodReturnStatusCode(UA_STATUSCODE_BADINVALIDSTATE);
		return;
	}
	this->applyComment(Comment, true);
	this->setLatched(false);
}

QList<QUaNodeId> QUaAlarmCondition::GetGroupMemberships()
{
	QList<QUaNodeId> groups;
	const auto groupNodes = this->findReferences({ "AlarmGroupMember", "MemberOfAlarmGroup" }, false);
	for (auto group : groupNodes)
	{
		groups << group->nodeId();
	}
	return groups;
}

bool QUaAlarmCondition::suppressionSupported() const
{
	return this->optionalState("SuppressedState") != nullptr;
}

void QUaAlarmCondition::setSuppressionSupported(const bool& suppressionSupported)
{
	this->setStateSupported("SuppressedState", tr("Suppressed"), tr("Unsuppressed"),
		{ "Suppress", "Suppress2", "Unsuppress", "Unsuppress2" }, suppressionSupported);
	this->updateSuppressedOrShelve();
}

bool QUaAlarmCondition::outOfServiceSupported() const
{
	return this->optionalState("OutOfServiceState") != nullptr;
}

void QUaAlarmCondition::setOutOfServiceSupported(const bool& outOfServiceSupported)
{
	this->setStateSupported("OutOfServiceState", tr("Out of Service"), tr("In Service"),
		{ "RemoveFromService", "RemoveFromService2", "PlaceInService", "PlaceInService2" }, outOfServiceSupported);
	this->updateSuppressedOrShelve();
}

bool QUaAlarmCondition::silenceSupported() const
{
	return this->optionalState("SilenceState") != nullptr;
}

void QUaAlarmCondition::setSilenceSupported(const bool& silenceSupported)
{
	this->setStateSupported("SilenceState", tr("Silenced"), tr("Not Silenced"),
		{ "Silence" }, silenceSupported);
}

bool QUaAlarmCondition::latchingSupported() const
{
	return this->optionalState("LatchedState") != nullptr;
}

void QUaAlarmCondition::setLatchingSupported(const bool& latchingSupported)
{
	this->setStateSupported("LatchedState", tr("Latched"), tr("Unlatched"),
		{ "Reset", "Reset2" }, latchingSupported);
}

bool QUaAlarmCondition::active() const
{
	return this->activeStateId();
}

void QUaAlarmCondition::setActive(const bool& active, const QString& strMessageAppend/* = QString()*/)
{
	// nothing to do if same
	if (active == this->active())
	{
		return;
	}
	// activate or deactivate
	auto strActiveStateName = active ?
		this->activeStateTrueState() :
		this->activeStateFalseState();
	auto time = QDateTime::currentDateTimeUtc();
	this->setActiveStateId(active);
	this->setActiveStateCurrentStateName(strActiveStateName);
	this->setActiveStateTransitionTime(time);
	// if active have to retain and reset acknowledged and confirmed
	if (active)
	{
		// part of this activation event, no event of their own
		this->changeState(this->getLatchedState(), true);
		this->changeState(this->getSilenceState(), false);
		this->setRetain(true);
		// reset aknowledged
		this->setAckedStateCurrentStateName(this->ackedStateFalseState());
		this->setAckedStateId(false);
		this->setAckedStateTransitionTime(this->getAckedState()->serverTimestamp());
		// reset confirmed
		if (this->confirmRequired())
		{
			this->setConfirmedStateCurrentStateName(this->confirmedStateFalseState());
			this->setConfirmedStateId(false);
			this->setConfirmedStateTransitionTime(this->getConfirmedState()->serverTimestamp());
		}
		this->setMessage(
			strMessageAppend.isEmpty() ?
			tr("Alarm %1. Requires Acknowledge.").arg(strActiveStateName) :
			tr("Alarm %1. %2 Requires Acknowledge.").arg(strActiveStateName).arg(strMessageAppend)
		);
	}
	else
	{
		QString strMessage = strMessageAppend.isEmpty() ? 
			tr("Alarm %1.").arg(strActiveStateName) :
			tr("Alarm %1. %2").arg(strActiveStateName).arg(strMessageAppend);
		// a latch alone keeps the alarm retained but needs no branch to acknowledge
		if (QUaAcknowledgeableCondition::requiresAttention())
		{
			// create branch
			auto branch = this->createBranch<QUaAlarmConditionBranch>();
			this->setRetain(true);
			if (!this->acknowledged())
			{
				strMessage += tr(" Requires Acknowledge.");
			}
			else if (_confirmRequired && !this->confirmed())
			{
				strMessage += tr(" Requires Confirm.");
			}
			if (branch)
			{
				strMessage += tr(" Has branches.");
			}
		}
		else
		{
			bool hasBranches = this->hasBranches();
			this->setRetain(hasBranches || this->latched());
			if (this->latched())
			{
				strMessage += tr(" Requires Reset.");
			}
			if (hasBranches)
			{
				strMessage += tr(" Has branches.");
			}
		}
		this->setMessage(strMessage);
	}
	// trigger event
	this->setTime(time);
	this->setReceiveTime(time);
	// NOTE : message set according to situation
	this->trigger();
	// emit qt signal
	active ?
		emit this->activated() :
		emit this->deactivated();
}

void QUaAlarmCondition::cleanConnections()
{
	while (_connections.count() > 0)
	{
		QObject::disconnect(_connections.takeFirst());
	}
}

QUaTwoStateVariable* QUaAlarmCondition::getActiveState()
{
	return this->browseChild<QUaTwoStateVariable>("ActiveState");
}

QUaProperty* QUaAlarmCondition::getInputNode()
{
	return this->browseChild<QUaProperty>("InputNode");
}

QUaProperty* QUaAlarmCondition::getSuppressedOrShelve()
{
	return this->browseChild<QUaProperty>("SuppressedOrShelved");
}

QUaTwoStateVariable* QUaAlarmCondition::getSuppressedState()
{
	return this->optionalState("SuppressedState");
}

QUaTwoStateVariable* QUaAlarmCondition::getOutOfServiceState()
{
	return this->optionalState("OutOfServiceState");
}

QUaTwoStateVariable* QUaAlarmCondition::getSilenceState()
{
	return this->optionalState("SilenceState");
}

QUaTwoStateVariable* QUaAlarmCondition::getLatchedState()
{
	return this->optionalState("LatchedState");
}

bool QUaAlarmCondition::requiresAttention() const
{
	// base implementation
	bool requiresAttention = QUaAcknowledgeableCondition::requiresAttention();
	// if active or waiting for Reset
	requiresAttention = requiresAttention || this->active() || this->latched();
	// return result
	return requiresAttention;
}

void QUaAlarmCondition::resetInternals()
{
	QUaAcknowledgeableCondition::resetInternals();
	// set default : Inactive state
	this->setActiveStateFalseState(tr("Inactive"));
	this->setActiveStateTrueState(tr("Active"));
	this->setActiveStateCurrentStateName(tr("Inactive"));
	this->setActiveStateId(false);
	this->setActiveStateTransitionTime(this->getActiveState()->serverTimestamp());
}

QUaTwoStateVariable* QUaAlarmCondition::optionalState(const QUaQualifiedName& stateName) const
{
	return const_cast<QUaAlarmCondition*>(this)->browseChild<QUaTwoStateVariable>(stateName);
}

void QUaAlarmCondition::setStateSupported(
	const QUaQualifiedName& stateName,
	const QUaLocalizedText& trueState,
	const QUaLocalizedText& falseState,
	const QList<QUaQualifiedName>& methods,
	const bool& supported)
{
	// NOTE : idempotent, deserialization may have instantiated the state already
	auto state = this->optionalState(stateName);
	if (!supported)
	{
		delete state;
		for (const auto& method : methods)
		{
			this->removeOptionalMethod(method);
		}
		return;
	}
	if (!state)
	{
		state = this->browseChild<QUaTwoStateVariable>(stateName, true);
		Q_CHECK_PTR(state);
		state->setFalseState(falseState);
		state->setTrueState(trueState);
		state->setCurrentStateName(falseState);
		state->setId(false);
		state->setTransitionTime(state->serverTimestamp());
	}
	for (const auto& method : methods)
	{
		this->addOptionalMethod(method);
	}
}

bool QUaAlarmCondition::changeState(QUaTwoStateVariable* state, const bool& id)
{
	if (!state || state->id() == id)
	{
		return false;
	}
	state->setId(id);
	state->setCurrentStateName(id ? state->trueState() : state->falseState());
	state->setTransitionTime(QDateTime::currentDateTimeUtc());
	return true;
}

void QUaAlarmCondition::triggerStateChange(QUaTwoStateVariable* state)
{
	auto time = QDateTime::currentDateTimeUtc();
	this->setTime(time);
	this->setReceiveTime(time);
	this->setMessage(tr("Alarm %1.").arg(state->currentStateName()));
	this->trigger();
}

bool QUaAlarmCondition::checkMethodSupported(const bool& supported)
{
	// called through the type's method node on an instance that does not expose it
	if (!supported)
	{
		this->setMethodReturnStatusCode(UA_STATUSCODE_BADMETHODINVALID);
	}
	return supported;
}

void QUaAlarmCondition::applyComment(const QUaLocalizedText& comment, const bool& stateChanges)
{
	// spec : a null Comment (empty locale and text) leaves the current one untouched
	if (comment.locale().isEmpty() && comment.text().isEmpty())
	{
		return;
	}
	if (!stateChanges)
	{
		this->setComment(comment);
		return;
	}
	// reported by the state change event
	this->getComment()->QUaBaseVariable::setValue(comment);
	auto session = this->currentSession();
	if (session)
	{
		this->setClientUserId(session->userName());
	}
}

void QUaAlarmCondition::updateSuppressedOrShelve()
{
	this->setSuppressedOrShelve(this->suppressed() || this->outOfService());
}

void QUaAlarmCondition::silenceOnAcknowledge()
{
	if (this->acknowledged())
	{
		this->setSilenced(true);
	}
}
/***************************************************************************************
*/

QUaBrowsePath QUaAlarmConditionBranch::ActiveState               ({ { 0, "ActiveState" } }); // [LocalizedText]
QUaBrowsePath QUaAlarmConditionBranch::ActiveState_Id            ({ { 0, "ActiveState" },{ 0, "Id"             } }); // [Boolean]
//QUaBrowsePath QUaAlarmConditionBranch::ActiveState_FalseState    ({ { 0, "ActiveState" },{ 0, "FalseState"     } }); // [LocalizedText]
//QUaBrowsePath QUaAlarmConditionBranch::ActiveState_TrueState     ({ { 0, "ActiveState" },{ 0, "TrueState"      } }); // [LocalizedText]
QUaBrowsePath QUaAlarmConditionBranch::ActiveState_TransitionTime({ { 0, "ActiveState" },{ 0, "TransitionTime" } }); // [UtcTime]

QUaAlarmConditionBranch::QUaAlarmConditionBranch(
	QUaCondition* parent,
	const QUaNodeId& branchId
) : QUaAcknowledgeableConditionBranch(parent, branchId)
{
	
}

bool QUaAlarmConditionBranch::active() const
{
	return this->value(QUaAlarmConditionBranch::ActiveState_Id).value<bool>();
}

bool QUaAlarmConditionBranch::requiresAttention() const
{
	// base implementation
	bool requiresAttention = QUaAcknowledgeableConditionBranch::requiresAttention();
	// if active
	requiresAttention = requiresAttention || this->active();
	// return result
	return requiresAttention;
}

#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS