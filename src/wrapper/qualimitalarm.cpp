#include "qualimitalarm.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS

#include <QUaServer>

QUaLimitAlarm::QUaLimitAlarm(
	QUaServer* server
) : QUaAlarmCondition(server)
{
	_highHighLimitRequired     = false;
	_highLimitRequired         = false;
	_lowLimitRequired          = false;
	_lowLowLimitRequired       = false;
	_adaptiveAlarmingSupported = false;
	_baseHighHighLimitRequired  = false;
	_baseHighLimitRequired      = false;
	_baseLowLimitRequired       = false;
	_baseLowLowLimitRequired    = false;
}

double QUaLimitAlarm::highHighLimit() const
{
	Q_ASSERT_X(_highHighLimitRequired, "QUaLimitAlarm::highHighLimit", "First call setHighHighLimitAllowed");
	if (!_highHighLimitRequired)
	{
		return 0.0;
	}
	return const_cast<QUaLimitAlarm*>(this)->getHighHighLimit()->value<double>();
}

void QUaLimitAlarm::setHighHighLimit(const double& highHighLimit)
{
	Q_ASSERT_X(_highHighLimitRequired, "QUaLimitAlarm::setHighHighLimit", "First call setHighHighLimitAllowed");
	if (!_highHighLimitRequired)
	{
		return;
	}
	if (highHighLimit == this->highHighLimit())
	{
		return;
	}
	this->getHighHighLimit()->setValue(highHighLimit);
	emit this->highHighLimitChanged();
}

double QUaLimitAlarm::highLimit() const
{
	Q_ASSERT_X(_highLimitRequired, "QUaLimitAlarm::highLimit", "First call setHighLimitRequired");
	if (!_highLimitRequired)
	{
		return 0.0;
	}
	return const_cast<QUaLimitAlarm*>(this)->getHighLimit()->value<double>();
}

void QUaLimitAlarm::setHighLimit(const double& highLimit)
{
	Q_ASSERT_X(_highLimitRequired, "QUaLimitAlarm::setHighLimit", "First call setHighLimitRequired");
	if (!_highLimitRequired)
	{
		return;
	}
	if (highLimit == this->highLimit())
	{
		return;
	}
	this->getHighLimit()->setValue(highLimit);
	emit this->highLimitChanged();
}

double QUaLimitAlarm::lowLimit() const
{
	Q_ASSERT_X(_lowLimitRequired, "QUaLimitAlarm::lowLimit", "First call setLowLimitRequired");
	if (!_lowLimitRequired)
	{
		return 0.0;
	}
	return const_cast<QUaLimitAlarm*>(this)->getLowLimit()->value<double>();
}

void QUaLimitAlarm::setLowLimit(const double& lowLimit)
{
	Q_ASSERT_X(_lowLimitRequired, "QUaLimitAlarm::setLowLimit", "First call setLowLimitRequired");
	if (!_lowLimitRequired)
	{
		return;
	}
	if (lowLimit == this->lowLimit())
	{
		return;
	}
	this->getLowLimit()->setValue(lowLimit);
	emit this->lowLimitChanged();
}

double QUaLimitAlarm::lowLowLimit() const
{
	Q_ASSERT_X(_lowLowLimitRequired, "QUaLimitAlarm::lowLowLimit", "First call setLowLowLimitRequired");
	if (!_lowLowLimitRequired)
	{
		return 0.0;
	}
	return const_cast<QUaLimitAlarm*>(this)->getLowLowLimit()->value<double>();
}

void QUaLimitAlarm::setLowLowLimit(const double& lowLowLimit)
{
	Q_ASSERT_X(_lowLowLimitRequired, "QUaLimitAlarm::setLowLowLimit", "First call setLowLowLimitRequired");
	if (!_lowLowLimitRequired)
	{
		return;
	}
	if (lowLowLimit == this->lowLowLimit())
	{
		return;
	}
	this->getLowLowLimit()->setValue(lowLowLimit);
	emit this->lowLowLimitChanged();
}

bool QUaLimitAlarm::adaptiveAlarmingSupported() const
{
	return _adaptiveAlarmingSupported;
}

double QUaLimitAlarm::baseHighHighLimit() const
{
	Q_ASSERT_X(_adaptiveAlarmingSupported && _baseHighHighLimitRequired,
		"QUaLimitAlarm::baseHighHighLimit", "First call setAdaptiveAlarmingSupported and setBaseHighHighLimitRequired");
	if (!_adaptiveAlarmingSupported || !_baseHighHighLimitRequired)
	{
		return 0.0;
	}
	return const_cast<QUaLimitAlarm*>(this)->getBaseHighHighLimit()->value<double>();
}

void QUaLimitAlarm::setBaseHighHighLimit(const double& baseHighHighLimit)
{
	Q_ASSERT_X(_adaptiveAlarmingSupported && _baseHighHighLimitRequired,
		"QUaLimitAlarm::setBaseHighHighLimit", "First call setAdaptiveAlarmingSupported and setBaseHighHighLimitRequired");
	if (!_adaptiveAlarmingSupported || !_baseHighHighLimitRequired)
	{
		return;
	}
	this->getBaseHighHighLimit()->setValue(baseHighHighLimit);
}

double QUaLimitAlarm::baseHighLimit() const
{
	Q_ASSERT_X(_adaptiveAlarmingSupported && _baseHighLimitRequired, 
		"QUaLimitAlarm::baseHighLimit", "First call setAdaptiveAlarmingSupported and setBaseHighLimitRequired");
	if (!_adaptiveAlarmingSupported || !_baseHighLimitRequired)
	{
		return 0.0;
	}
	return const_cast<QUaLimitAlarm*>(this)->getBaseHighLimit()->value<double>();
}

void QUaLimitAlarm::setBaseHighLimit(const double& baseHighLimit)
{
	Q_ASSERT_X(_adaptiveAlarmingSupported && _baseHighLimitRequired, 
		"QUaLimitAlarm::baseHighLimit", "First call setAdaptiveAlarmingSupported and setBaseHighLimitRequired");
	if (!_adaptiveAlarmingSupported || !_baseHighLimitRequired)
	{
		return;
	}
	this->getBaseHighLimit()->setValue(baseHighLimit);
}

double QUaLimitAlarm::baseLowLimit() const
{
	Q_ASSERT_X(_adaptiveAlarmingSupported && _baseLowLimitRequired, 
		"QUaLimitAlarm::baseLowLimit", "First call setAdaptiveAlarmingSupported and setBaseLowLimitRequired");
	if (!_adaptiveAlarmingSupported || !_baseLowLimitRequired)
	{
		return 0.0;
	}
	return const_cast<QUaLimitAlarm*>(this)->getBaseLowLimit()->value<double>();
}

void QUaLimitAlarm::setBaseLowLimit(const double& baseLowLimit)
{
	Q_ASSERT_X(_adaptiveAlarmingSupported && _baseLowLimitRequired, 
		"QUaLimitAlarm::setBaseLowLimit", "First call setAdaptiveAlarmingSupported and setBaseLowLimitRequired");
	if (!_adaptiveAlarmingSupported || !_baseLowLimitRequired)
	{
		return;
	}
	this->getBaseLowLimit()->setValue(baseLowLimit);
}

double QUaLimitAlarm::baseLowLowLimit() const
{
	Q_ASSERT_X(_adaptiveAlarmingSupported && _baseLowLowLimitRequired, 
		"QUaLimitAlarm::baseLowLowLimit", "First call setAdaptiveAlarmingSupported and setBaseLowLowLimitRequired");
	if (!_adaptiveAlarmingSupported || !_baseLowLowLimitRequired)
	{
		return 0.0;
	}
	return const_cast<QUaLimitAlarm*>(this)->getBaseLowLowLimit()->value<double>();
}

void QUaLimitAlarm::setBaseLowLowLimit(const double& baseLowLowLimit)
{
	Q_ASSERT_X(_adaptiveAlarmingSupported && _baseLowLowLimitRequired, 
		"QUaLimitAlarm::setBaseLowLowLimit", "First call setAdaptiveAlarmingSupported and setBaseLowLowLimitRequired");
	if (!_adaptiveAlarmingSupported || !_baseLowLowLimitRequired)
	{
		return;
	}
	this->getBaseLowLowLimit()->setValue(baseLowLowLimit);
}

bool QUaLimitAlarm::highHighLimitRequired() const
{
	return _highHighLimitRequired;
}

void QUaLimitAlarm::setHighHighLimitRequired(const bool& highHighLimitRequired)
{
	if (highHighLimitRequired == _highHighLimitRequired)
	{
		return;
	}
	_highHighLimitRequired = highHighLimitRequired;
	// add or remove component
	auto highHighLimit = this->browseChild<QUaProperty>("HighHighLimit");
	Q_ASSERT(
		(_highHighLimitRequired && !highHighLimit) ||
		(!_highHighLimitRequired && highHighLimit)
	);
	if (!_highHighLimitRequired)
	{
		Q_CHECK_PTR(highHighLimit);
		// remove
		delete highHighLimit;
		return;
	}
	// initialize and set defaults
	highHighLimit = this->browseChild<QUaProperty>("HighHighLimit", true);
	Q_CHECK_PTR(highHighLimit);
	Q_UNUSED(highHighLimit);
	// NOTE : set default value, no event to avoid recomputing active state
	highHighLimit->setValue(+std::numeric_limits<double>::infinity());
	// allow base limits of currently (un)supported limits
	if (_adaptiveAlarmingSupported)
	{
		this->setBaseHighHighLimitRequired(_highHighLimitRequired);
	}
	// notify change
	emit this->highHighLimitRequiredChanged();
}

bool QUaLimitAlarm::highLimitRequired() const
{
	return _highLimitRequired;
}

void QUaLimitAlarm::setHighLimitRequired(const bool& highLimitRequired)
{
	if (highLimitRequired == _highLimitRequired)
	{
		return;
	}
	_highLimitRequired = highLimitRequired;
	// add or remove component
	auto highLimit = this->browseChild<QUaProperty>("HighLimit");
	Q_ASSERT(
		(_highLimitRequired && !highLimit) ||
		(!_highLimitRequired && highLimit)
	);
	if (!_highLimitRequired)
	{
		Q_CHECK_PTR(highLimit);
		// remove
		delete highLimit;
		return;
	}
	// initialize and set defaults
	highLimit = this->browseChild<QUaProperty>("HighLimit", true);
	Q_CHECK_PTR(highLimit);
	Q_UNUSED(highLimit);
	// NOTE : set default value, no event to avoid recomputing active state
	highLimit->setValue(+std::numeric_limits<double>::infinity());
	// allow/disallow base limits of currently (un)supported limits
	if (_adaptiveAlarmingSupported)
	{
		this->setBaseHighLimitRequired(_highLimitRequired);
	}
	// notify change
	emit this->highLimitRequiredChanged();
}

bool QUaLimitAlarm::lowLimitRequired() const
{
	return _lowLimitRequired;
}

void QUaLimitAlarm::setLowLimitRequired(const bool& lowLimitRequired)
{
	if (lowLimitRequired == _lowLimitRequired)
	{
		return;
	}
	_lowLimitRequired = lowLimitRequired;
	// add or remove component
	auto lowLimit = this->browseChild<QUaProperty>("LowLimit");
	Q_ASSERT(
		(_lowLimitRequired && !lowLimit) ||
		(!_lowLimitRequired && lowLimit)
	);
	if (!_lowLimitRequired)
	{
		Q_CHECK_PTR(lowLimit);
		// remove
		delete lowLimit;
		return;
	}
	// initialize and set defaults
	lowLimit = this->browseChild<QUaProperty>("LowLimit", true);
	Q_CHECK_PTR(lowLimit);
	Q_UNUSED(lowLimit);
	// NOTE : set default value, no event to avoid recomputing active state
	lowLimit->setValue(-std::numeric_limits<double>::infinity());
	// allow/disallow base limits of currently (un)supported limits
	if (_adaptiveAlarmingSupported)
	{
		this->setBaseLowLimitRequired(_lowLimitRequired);
	}
	// notify change
	emit this->lowLimitRequiredChanged();
}

bool QUaLimitAlarm::lowLowLimitRequired() const
{
	return _lowLowLimitRequired;
}

void QUaLimitAlarm::setLowLowLimitRequired(const bool& lowLowLimitRequired)
{
	if (lowLowLimitRequired == _lowLowLimitRequired)
	{
		return;
	}
	_lowLowLimitRequired = lowLowLimitRequired;
	// add or remove component
	auto lowLowLimit = this->browseChild<QUaProperty>("LowLowLimit");
	Q_ASSERT(
		(_lowLowLimitRequired && !lowLowLimit) ||
		(!_lowLowLimitRequired && lowLowLimit)
	);
	if (!_lowLowLimitRequired)
	{
		Q_CHECK_PTR(lowLowLimit);
		// remove
		delete lowLowLimit;
		return;
	}
	// initialize and set defaults
	lowLowLimit = this->browseChild<QUaProperty>("LowLowLimit", true);
	Q_CHECK_PTR(lowLowLimit);
	Q_UNUSED(lowLowLimit);
	// NOTE : set default value, no event to avoid recomputing active state
	lowLowLimit->setValue(-std::numeric_limits<double>::infinity());
	// allow/disallow base limits of currently (un)supported limits
	if (_adaptiveAlarmingSupported)
	{
		this->setBaseLowLowLimitRequired(_lowLowLimitRequired);
	}
	// notify change
	emit this->lowLowLimitRequiredChanged();
}

bool QUaLimitAlarm::baseHighHighLimitRequired() const
{
	return _baseHighHighLimitRequired;
}



void QUaLimitAlarm::setAdaptiveAlarmingSupported(const bool& adaptiveAlarmingSupported)
{
	if (adaptiveAlarmingSupported == _adaptiveAlarmingSupported)
	{
		return;
	}
	_adaptiveAlarmingSupported = adaptiveAlarmingSupported;
	// allow/disallow base limits of currently (un)supported limits
	if (_adaptiveAlarmingSupported)
	{
		this->setBaseHighHighLimitRequired(this->highHighLimitRequired());
		this->setBaseHighLimitRequired    (this->highLimitRequired()    );
		this->setBaseLowLimitRequired     (this->lowLimitRequired()     );
		this->setBaseLowLowLimitRequired  (this->lowLowLimitRequired()  );
	}
	else
	{
		this->setBaseHighHighLimitRequired(false);
		this->setBaseHighLimitRequired    (false);
		this->setBaseLowLimitRequired     (false);
		this->setBaseLowLowLimitRequired  (false);
	}
}

void QUaLimitAlarm::setBaseHighHighLimitRequired(const bool& baseHighHighLimitRequired)
{
	if (baseHighHighLimitRequired == _baseHighHighLimitRequired)
	{
		return;
	}
	_baseHighHighLimitRequired = baseHighHighLimitRequired;
	// add or remove component
	auto baseHighHighLimit = this->browseChild<QUaProperty>("BaseHighHighLimit");
	Q_ASSERT(
		(_baseHighHighLimitRequired && !baseHighHighLimit) ||
		(!_baseHighHighLimitRequired && baseHighHighLimit)
	);
	if (!_baseHighHighLimitRequired)
	{
		Q_CHECK_PTR(baseHighHighLimit);
		// remove
		delete baseHighHighLimit;
		return;
	}
	// initialize and set defaults
	baseHighHighLimit = this->browseChild<QUaProperty>("BaseHighHighLimit", true);
	Q_CHECK_PTR(baseHighHighLimit);
	Q_UNUSED(baseHighHighLimit);
	// NOTE : set default value, no event to avoid recomputing active state
	baseHighHighLimit->setValue(+std::numeric_limits<double>::infinity());
}

bool QUaLimitAlarm::baseHighLimitRequired() const
{
	return _baseHighLimitRequired;
}

void QUaLimitAlarm::setBaseHighLimitRequired(const bool& baseHighLimitRequired)
{
	if (baseHighLimitRequired == _baseHighLimitRequired)
	{
		return;
	}
	_baseHighLimitRequired = baseHighLimitRequired;
	// add or remove component
	auto baseHighLimit = this->browseChild<QUaProperty>("BaseHighLimit");
	Q_ASSERT(
		(_baseHighLimitRequired && !baseHighLimit) ||
		(!_baseHighLimitRequired && baseHighLimit)
	);
	if (!_baseHighLimitRequired)
	{
		Q_CHECK_PTR(baseHighLimit);
		// remove
		delete baseHighLimit;
		return;
	}
	// initialize and set defaults
	baseHighLimit = this->browseChild<QUaProperty>("BaseHighLimit", true);
	Q_CHECK_PTR(baseHighLimit);
	Q_UNUSED(baseHighLimit);
	// NOTE : set default value, no event to avoid recomputing active state
	baseHighLimit->setValue(+std::numeric_limits<double>::infinity());
}

bool QUaLimitAlarm::baseLowLimitRequired() const
{
	return _baseLowLimitRequired;
}

void QUaLimitAlarm::setBaseLowLimitRequired(const bool& baseLowLimitRequired)
{
	if (baseLowLimitRequired == _baseLowLimitRequired)
	{
		return;
	}
	_baseLowLimitRequired = baseLowLimitRequired;
	// add or remove component
	auto baseLowLimit = this->browseChild<QUaProperty>("BaseLowLimit");
	Q_ASSERT(
		(_baseLowLimitRequired && !baseLowLimit) ||
		(!_baseLowLimitRequired && baseLowLimit)
	);
	if (!_baseLowLimitRequired)
	{
		Q_CHECK_PTR(baseLowLimit);
		// remove
		delete baseLowLimit;
		return;
	}
	// initialize and set defaults
	baseLowLimit = this->browseChild<QUaProperty>("BaseLowLimit", true);
	Q_CHECK_PTR(baseLowLimit);
	Q_UNUSED(baseLowLimit);
	// NOTE : set default value, no event to avoid recomputing active state
	baseLowLimit->setValue(-std::numeric_limits<double>::infinity());
}

bool QUaLimitAlarm::baseLowLowLimitRequired() const
{
	return _baseLowLowLimitRequired;
}

void QUaLimitAlarm::setBaseLowLowLimitRequired(const bool& baseLowLowLimitRequired)
{
	if (baseLowLowLimitRequired == _baseLowLowLimitRequired)
	{
		return;
	}
	_baseLowLowLimitRequired = baseLowLowLimitRequired;
	// add or remove component
	auto baseLowLowLimit = this->browseChild<QUaProperty>("BaseLowLowLimit");
	Q_ASSERT(
		(_baseLowLowLimitRequired && !baseLowLowLimit) ||
		(!_baseLowLowLimitRequired && baseLowLowLimit)
	);
	if (!_baseLowLowLimitRequired)
	{
		Q_CHECK_PTR(baseLowLowLimit);
		// remove
		delete baseLowLowLimit;
		return;
	}
	// initialize and set defaults
	baseLowLowLimit = this->browseChild<QUaProperty>("BaseLowLowLimit", true);
	Q_CHECK_PTR(baseLowLowLimit);
	Q_UNUSED(baseLowLowLimit);
	// NOTE : set default value, no event to avoid recomputing active state
	baseLowLowLimit->setValue(-std::numeric_limits<double>::infinity());
}

QUaProperty* QUaLimitAlarm::getHighHighLimit()
{
	return this->browseChild<QUaProperty>("HighHighLimit");
}

QUaProperty* QUaLimitAlarm::getHighLimit()
{
	return this->browseChild<QUaProperty>("HighLimit");
}

QUaProperty* QUaLimitAlarm::getLowLimit()
{
	return this->browseChild<QUaProperty>("LowLimit");
}

QUaProperty* QUaLimitAlarm::getLowLowLimit()
{
	return this->browseChild<QUaProperty>("LowLowLimit");
}

QUaProperty* QUaLimitAlarm::getBaseHighHighLimit()
{
	return this->browseChild<QUaProperty>("BaseHighHighLimit");
}

QUaProperty* QUaLimitAlarm::getBaseHighLimit()
{
	return this->browseChild<QUaProperty>("BaseHighLimit");
}

QUaProperty* QUaLimitAlarm::getBaseLowLimit()
{
	return this->browseChild<QUaProperty>("BaseLowLimit");
}

QUaProperty* QUaLimitAlarm::getBaseLowLowLimit()
{
	return this->browseChild<QUaProperty>("BaseLowLowLimit");
}

#endif // UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS


