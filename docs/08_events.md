# Events

To use events, configure the project with the `QUASERVER_EVENTS` option:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path to Qt6> -DQUASERVER_EVENTS=ON
cmake --build build
```

* The *open62541* library is then built with `UA_NAMESPACE_ZERO=FULL`, because by default *open62541* does not include the complete address space of the OPC UA standard in order to reduce binary size. But to support events, it is actually necessary to have the `FULL` address space available in the server application.

* `UA_ENABLE_SUBSCRIPTIONS_EVENTS=ON` is the *open62541* flag that enables events.

Note that the binaries are now considerably larger because they contain the full default OPC UA address space.

Options can be combined, for example to enable both events and encryption:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path to Qt6> -DQUASERVER_EVENTS=ON -DQUASERVER_ENCRYPTION=ON
```

Events now can be used in the C++ code. To create an event, it is first necessary to **subtype** the `QUaBaseEvent` class, for example:

In `myevent.h`:

```c++
#include <QUaBaseEvent>

class MyEvent : public QUaBaseEvent
{
    Q_OBJECT

public:
	Q_INVOKABLE explicit MyEvent(QUaServer *server);

};
```

In `myevent.cpp`:

```c++
#include "myevent.h"

MyEvent::MyEvent(QUaServer *server)
	: QUaBaseEvent(server)
{

}
```

The same rules apply as when subtyping *Objects* or *Variables* (see the [Types](04_types.md) section).

Events must have an **originator** node, which can be any object in the address space that allows to subscribe to events. This is defined in the [`EventNotifier`](https://reference.opcfoundation.org/v104/Core/docs/Part3/8.59/) attribute which can be accessed through the `QUaBaseObject` API:

```c++
quint8 eventNotifier() const;
void setEventNotifier(const quint8 &eventNotifier);
```

The value should be an enumeration, but to simplify the usage, there are a couple of helper methods:

```c++
bool subscribeToEvents() const;
void setSubscribeToEvents(const bool& subscribeToEvents);
```

The `setSubscribeToEvents(true)` enables events for the object while `setSubscribeToEvents(false)` disables them. By default events are **disabled** for all objects. Except for the [**Server Object**](https://reference.opcfoundation.org/v104/Core/docs/Part5/8.3.2/).

If there is an event which does not originate from any object, then is necessary to use the *Server Object* to create and trigger the event. An event is instantiated using the `createEvent<T>()` method:

```c++
auto event = server.createEvent<MyEvent>();
```

Note that for events there is no need to define a `BrowseName` upon instantiation, that is because events are not normally exposed in the address space (with the exception of Alarms and Conditions).

Once an event is created, some [event variables](https://reference.opcfoundation.org/v104/Core/ObjectTypes/BaseEventType/) can be set to define the event information. This is provided by the inherited `QUaBaseEvent` API:

```c++
QString sourceName() const;
void setSourceName(const QString &strSourceName);

QDateTime time() const;
void setTime(const QDateTime &dateTime);

QString message() const;
void setMessage(const QString &strMessage);

quint16 severity() const;
void setSeverity(const quint16 &intSeverity);
```

* `SourceName` : Description of the source of the Event.

* `Time` : Time (in UTC) the Event occurred. It comes from the underlying system or device.

* `Message` : Human-readable description of the Event.

* `Severity` : Urgency of the Event. Value from 1 to 1000, with 1 being the lowest severity and 1000 being the highest.

The variables must be set before triggering the event. Then, the event can be triggered with the `trigger()` method.

In order to be able to test the events though, it is necessary to have a mechanism to trigger events on demand. One option is to create a method to trigger the event:

```c++
auto event = server.createEvent<MyEvent>();

objsFolder->addMethod("triggerServerEvent", [&event]() {
	// set event information
	event->setSourceName("Server");
	event->setMessage("An event occurred in the server");
	event->setTime(QDateTime::currentDateTimeUtc());
	event->setSeverity(100);
	// trigger event
	event->trigger();	
});
```

In order to visualize events, some clients require a special events window. For example in *UA Expert*, click the `Add Document` button, then select `Event View` and click `Add`. Then *drag and drop* the *Server Object* (`/Root/Objects/Server`) to the *Configuration* window. Now is possible to see events.

<p align="center">
  <img src="../res/img/08_events_01.jpg">
</p>

The event can be triggered any number of times, and its variables can be updated to new values at any point. Once is not needed anymore, the event can be deleted:

```c++
delete event;
```

If it is desired to trigger events with an specific object as originator, simply create the event using that object's `createEvent<T>()` method:

```c++
QUaFolderObject * objsFolder = server.objectsFolder();
auto obj = objsFolder->addBaseObject("obj");

// Enable object for events
obj->setSubscribeToEvents(true);
// Create event with object as originator
auto obj_event = obj->createEvent<MyEvent>();
```

But now on the client it is necessary to *drag and drop* the originator object to the *Configuration* window.

<p align="center">
  <img src="../res/img/08_events_02.jpg">
</p>

## Events Example

Build and run the [08_events](../examples/08_events/main.cpp) example to learn more.
