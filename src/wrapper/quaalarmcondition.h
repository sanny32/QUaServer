#ifndef QUAALARMCONDITION_H
#define QUAALARMCONDITION_H

#include <QUaAcknowledgeableCondition>

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

class QUaBaseVariable;

class QUaAlarmCondition : public QUaAcknowledgeableCondition
{
    Q_OBJECT

	Q_PROPERTY(bool suppressionSupported  READ suppressionSupported  WRITE setSuppressionSupported)
	Q_PROPERTY(bool outOfServiceSupported READ outOfServiceSupported WRITE setOutOfServiceSupported)
	Q_PROPERTY(bool silenceSupported      READ silenceSupported      WRITE setSilenceSupported)
	Q_PROPERTY(bool latchingSupported     READ latchingSupported     WRITE setLatchingSupported)

friend class QUaServer;

public:
	Q_INVOKABLE explicit QUaAlarmCondition(
		QUaServer *server
	);
	~QUaAlarmCondition();

	// children

	// If condition currently exists
	QUaLocalizedText activeStateCurrentStateName() const;
	void             setActiveStateCurrentStateName(const QUaLocalizedText& activeState);
	bool             activeStateId() const;
	void             setActiveStateId(const bool& activeStateId);
	QDateTime        activeStateTransitionTime() const;
	void             setActiveStateTransitionTime(const QDateTime& transitionTime);
	QUaLocalizedText activeStateTrueState() const;
	void             setActiveStateTrueState(const QUaLocalizedText& trueState);
	QUaLocalizedText activeStateFalseState() const;
	void             setActiveStateFalseState(const QUaLocalizedText& falseState);
	// helper
	bool active() const;
	void setActive(const bool& active, const QString& strMessageAppend = QString());

	// NodeId of the Variable which is used for the calculation of the Alarm state.
	// Can be null if internal
	QUaNodeId inputNode() const;
	void      setInputNode(const QUaNodeId& QUaNodeId);
	// Helper
	virtual void setInputNode(QUaBaseVariable* inputNode);

	// NOTE : the optional states below only work if the matching *Supported() == true;
	//        setters trigger an event when the state changes

	// suppressed by the server for system specific reasons, or by the Suppress methods
	bool suppressed() const;
	void setSuppressed(const bool& suppressed);
	// removed from service by maintenance personnel
	bool outOfService() const;
	void setOutOfService(const bool& outOfService);
	// cleared when the alarm activates, set by Silence or by acknowledging
	bool silenced() const;
	void setSilenced(const bool& silenced);
	// set when the alarm activates, keeps it retained until Reset
	bool latched() const;
	void setLatched(const bool& latched);

	// TODO : ShelvingState

	// NOTE : kept in sync with SuppressedState and OutOfServiceState
	bool suppressedOrShelve() const;
	void setSuppressedOrShelve(const bool& suppressedOrShelve);

	// TODO : MaxTimeShelved
	// TODO : AudibleEnabled
	// TODO : AudibleSound
	// TODO : OnDelay
	// TODO : OffDelay
	// TODO : FirstInGroupFlag
	// TODO : FirstInGroup
	// TODO : <AlarmGroup>
	// TODO : ReAlarmTime
	// TODO : ReAlarmRepeatCount

	// methods

	Q_INVOKABLE void Silence();

	Q_INVOKABLE void Suppress();
	Q_INVOKABLE void Suppress2(QUaLocalizedText Comment);

	Q_INVOKABLE void Unsuppress();
	Q_INVOKABLE void Unsuppress2(QUaLocalizedText Comment);

	Q_INVOKABLE void RemoveFromService();
	Q_INVOKABLE void RemoveFromService2(QUaLocalizedText Comment);

	Q_INVOKABLE void PlaceInService();
	Q_INVOKABLE void PlaceInService2(QUaLocalizedText Comment);

	Q_INVOKABLE void Reset();
	Q_INVOKABLE void Reset2(QUaLocalizedText Comment);

	// AlarmGroups referencing this alarm through AlarmGroupMember
	Q_INVOKABLE QList<QUaNodeId> GetGroupMemberships();

	// helpers

	// SuppressedState with Suppress, Suppress2, Unsuppress and Unsuppress2
	bool suppressionSupported() const;
	void setSuppressionSupported(const bool& suppressionSupported);
	// OutOfServiceState with RemoveFromService, RemoveFromService2, PlaceInService and PlaceInService2
	bool outOfServiceSupported() const;
	void setOutOfServiceSupported(const bool& outOfServiceSupported);
	// SilenceState with Silence
	bool silenceSupported() const;
	void setSilenceSupported(const bool& silenceSupported);
	// LatchedState with Reset and Reset2
	bool latchingSupported() const;
	void setLatchingSupported(const bool& latchingSupported);

signals:
	void activated();
	void deactivated();
	void suppressedChanged();
	void outOfServiceChanged();
	void silencedChanged();
	void latchedChanged();

protected:
	QUaBaseVariable* _inputNode;
	QList<QMetaObject::Connection> _connections;
	void cleanConnections();
	// LocalizedText
	QUaTwoStateVariable* getActiveState();
	// NodeId
	QUaProperty* getInputNode();

	// NOTE : optional, nullptr unless supported
	// LocalizedText
	QUaTwoStateVariable* getSuppressedState();
	// LocalizedText
	QUaTwoStateVariable* getOutOfServiceState();
	// LocalizedText
	QUaTwoStateVariable* getSilenceState();
	// LocalizedText
	QUaTwoStateVariable* getLatchedState();

	// TODO : ShelvedStateMachineType, getShelvingState

	// Boolean
	QUaProperty* getSuppressedOrShelve();

	// TODO : Duration, getMaxTimeShelved

	// TODO : Boolean, getAudibleEnabled
	// TODO : AudioDataType, AudibleSound

	// TODO : Duration, getOnDelay
	// TODO : Duration, getOffDelay

	// TODO : Boolean, getFirstInGroupFlag
	// TODO : AlarmGroupType, getFirstInGroup

	// TODO : AlarmGroupType, get<AlarmGroup>

	// TODO : Duration, getReAlarmTime
	// TODO : Int16, getReAlarmRepeatCount

	// helpers

	// reimplement to define branch delete conditions
	virtual bool requiresAttention() const;
	// reimplement to reset type internals (QUaAlarmCondition::Reset)
	virtual void resetInternals();

private:
	QUaTwoStateVariable* optionalState(const QUaQualifiedName& stateName) const;
	void setStateSupported(
		const QUaQualifiedName& stateName,
		const QUaLocalizedText& trueState,
		const QUaLocalizedText& falseState,
		const QList<QUaQualifiedName>& methods,
		const bool& supported
	);
	bool changeState(QUaTwoStateVariable* state, const bool& id);
	void triggerStateChange(QUaTwoStateVariable* state);
	bool checkMethodSupported(const bool& supported);
	void applyComment(const QUaLocalizedText& comment, const bool& stateChanges);
	void updateSuppressedOrShelve();
	void silenceOnAcknowledge();
};

class QUaAlarmConditionBranch : public QUaAcknowledgeableConditionBranch
{
public:
	QUaAlarmConditionBranch(
		QUaCondition* parent,
		const QUaNodeId& branchId = QUaNodeId()
	);

	bool active() const;

protected:

	// reimplement
	virtual bool requiresAttention() const override;

	// QUaAlarmCondition
	static QUaBrowsePath ActiveState;
	static QUaBrowsePath ActiveState_Id;
	//static QUaBrowsePath ActiveState_FalseState;
	//static QUaBrowsePath ActiveState_TrueState;
	static QUaBrowsePath ActiveState_TransitionTime;

	friend QUaAlarmCondition;
};

#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

#endif // QUAALARMCONDITION_H
