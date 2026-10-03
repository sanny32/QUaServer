#include "quarefreshstartevent.h"

#ifdef UA_ENABLE_SUBSCRIPTIONS_EVENTS

#include <QUaServer>

QUaRefreshStartEvent::QUaRefreshStartEvent(
	QUaServer *server
) : QUaSystemEvent(server)
{
#ifdef UA_ENABLE_HISTORIZING
	_historizing = false;
#endif // UA_ENABLE_HISTORIZING
}

#endif // UA_ENABLE_SUBSCRIPTIONS_EVENTS