# Historizing

The *QUaServer* supports storing *histrical data* and *historical events*, exposing them through the [*HistoryRead* service](https://reference.opcfoundation.org/v104/Core/docs/Part4/5.10.3/).

To enable this functionality, configure the project with the `QUASERVER_HISTORIZING` option:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path to Qt6> -DQUASERVER_HISTORIZING=ON
cmake --build build
```

To support *historizing*, `QUaServer` provides the `setHistorizer` method:

```c++
template<typename T>
bool setHistorizer(T& historizer);
```

## Historizing Data

To historize data, the historizer `T` can be any C++ *type* implementing the following interface:

```c++
// required API for QUaServer::setHistorizer
// write data point to backend, return true on success
bool writeHistoryData(
	const QUaNodeId &nodeId,
	const QUaHistoryDataPoint &dataPoint,
	QQueue<QUaLog>  &logOut
);
// required API for QUaServer::setHistorizer
// update an existing node's data point in backend, return true on success
bool updateHistoryData(
	const QUaNodeId &nodeId, 
	const QUaHistoryDataPoint &dataPoint,
	QQueue<QUaLog>  &logOut
);
// required API for QUaServer::setHistorizer
// remove an existing node's data points within a range, return true on success
bool removeHistoryData(
	cconst QUaNodeId &nodeId, 
	const QDateTime  &timeStart,
	const QDateTime  &timeEnd,
	QQueue<QUaLog>   &logOut
); 
// required API for QUaServer::setHistorizer
// return the timestamp of the first sample available for the given node
QDateTime firstTimestamp(
	const QString  &strNodeId,
	QQueue<QUaLog> &logOut
) const;
// required API for QUaServer::setHistorizer
// return the timestamp of the latest sample available for the given node
QDateTime lastTimestamp(
	const QUaNodeId &nodeId, 
	QQueue<QUaLog>  &logOut
) const;
// required API for QUaServer::setHistorizer
// return true if given timestamp is available for the given node
bool hasTimestamp(
	const QString   &strNodeId,
	const QDateTime &timestamp,
	QQueue<QUaLog>  &logOut
) const;
// required API for QUaServer::setHistorizer
// return a timestamp matching the criteria for the given node
QDateTime findTimestamp(
	const QUaNodeId &nodeId, 
	const QDateTime &timestamp,
	const QUaHistoryBackend::TimeMatch& match,
	QQueue<QUaLog>  &logOut
) const;
// required API for QUaServer::setHistorizer
// return the number for data points within a time range for the given node
quint64 numDataPointsInRange(
	const QUaNodeId &nodeId, 
	const QDateTime &timeStart,
	const QDateTime &timeEnd,
	QQueue<QUaLog>  &logOut
) const;
// required API for QUaServer::setHistorizer
// return the numPointsToRead data points for the given node
// starting from the numPointsOffset offset after given start time (pagination)
QVector<QUaHistoryDataPoint> readHistoryData(
	const QUaNodeId &nodeId, 
	const QDateTime &timeStart,
	const quint64   &numPointsOffset,
	const quint64   &numPointsToRead,
	QQueue<QUaLog>  &logOut
) const;
```

To allow **storing** data it is only necessary to implement `writeHistoryData`, while the other methods can return default values. The data will be saved to whatever media it is desired. 

Implementing only `writeHistoryData`, means clients won't be able to access the historcal data remotely yet, for that it is necessary to implement more methods of the API, as explained further below.

To *store* the data, the API passes the *NodeId* (`const QUaNodeId &nodeId`) of the variable to be historized.

The `QUaHistoryDataPoint` structure (`const QUaHistoryDataPoint &dataPoint`) provides the information that needs to be stored:

```c++
struct QUaHistoryDataPoint
{
	QDateTime timestamp;
	QVariant  value;
	quint32   status;
};
```

Whatever storage media is chosen, it must be *queryable* first, by *NodeId* and second, by *Timestamp*. 

For example, if a `SQL` database is chosen for storage, one approach is to create one table for each *NodeId*. Each table having three columns for *time*, *value* and *status* respectively. To speed up queries, it is recommended to create *indexes* over the *time* column.

Some *pseudo-SQL* code is used in this documentation to illustrate *possible* implementation of each API method. For example, to create each *NodeId* table:

```sql
CREATE TABLE ":NodeId" (
	[:NodeId] INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
	[Time] INTEGER NOT NULL,
	[Value] :DataType NOT NULL,
	[Status] INTEGER NOT NULL
);
-- create index to optimize queries by time
CREATE UNIQUE INDEX ":NodeId_Time" ON ":NodeId"(Time);
```

For `writeHistoryData`:

```sql
INSERT INTO ":NodeId" (Time, Value, Status) VALUES (:Time, :Value, :Status);
```

All the methods of the API should populate the `QQueue<QUaLog> &logOut` parameter with log entries describing any error occurred during the storing or querying process.

To allow an OPC UA Client to **access** the historcal data remotely, it is necessary to further implement the `firstTimestamp`, `lastTimestamp`, `hasTimestamp`, `findTimestamp`, `numDataPointsInRange` and `readHistoryData`. The implementation of this methods is self-describing by their names. Below some *pseudo-SQL* to illustrate *possible* implementation:

For `firstTimestamp`:

```sql
SELECT p.Time FROM ":NodeId" p ORDER BY p.Time ASC LIMIT 1;
```

For `lastTimestamp`:

```sql
SELECT p.Time FROM ":NodeId" p ORDER BY p.Time DESC LIMIT 1;
```

For `hasTimestamp`:

```sql
SELECT COUNT(*) FROM ":NodeId" p WHERE p.Time = :Time;
```

For `findTimestamp`:

```sql
-- from above
SELECT p.Time FROM ":NodeId" p WHERE p.Time > :Time ORDER BY p.Time ASC LIMIT 1;
-- from below
SELECT p.Time FROM ":NodeId" p WHERE p.Time < :Time ORDER BY p.Time DESC LIMIT 1;
```

For `numDataPointsInRange`:

```sql
SELECT COUNT(*) FROM ":NodeId" p WHERE p.Time >= :TimeStart AND p.Time <= :TimeEnd ORDER BY p.Time ASC;
```

For `readHistoryData`:

```sql
SELECT p.Time, p.Value, p.Status FROM":NodeId" p WHERE p.Time >= :Time ORDER BY p.Time ASC LIMIT :Limit OFFSET :Offset;
```

To allow *modifying* historical data, the `updateHistoryData` and `removeHistoryData` should be implemented accordingly.

Finally, to historize a variable, the `QUaBaseVariable::setHistorizing(const bool& historizing)` method should be called. And to allow clients to access its historical data remotelly, the `QUaBaseVariable::setReadHistoryAccess(const bool& readHistoryAccess)` method should be called. For example:

```c++
// create int variable
auto varInt = objsFolder->addBaseDataVariable("MyInt", "ns=0;s=MyInt");
varInt->setValue(0);
// NOTE : must enable historizing for each variable
varInt->setHistorizing(true);
varInt->setReadHistoryAccess(true);
```

Similarly, to allow clients to modify the historical data, the `QUaBaseVariable::setWriteHistoryAccess(const bool& bHistoryWrite)` method should be called.

<p align="center">
  <img src="../res/img/10_historizing_01_data.gif">
</p>

## Historizing Events

Historizing events is only possible if the `QUaServer` project is configured with the `QUASERVER_EVENTS` CMake option. See the [Events](08_events.md) section for more information.

To historize events, the historizer `T` can be any C++ *type* implementing the following interface:

```c++
// write a event's data to backend
bool writeHistoryEventsOfType(
	const QUaNodeId            &eventTypeNodeId,
	const QList<QUaNodeId>     &emittersNodeIds,
	const QUaHistoryEventPoint &eventPoint,
	QQueue<QUaLog>             &logOut
);
// get event types (node ids) for which there are events stored for the given emitter
QVector<QUaNodeId> eventTypesOfEmitter(
	const QUaNodeId &emitterNodeId,
	QQueue<QUaLog>  &logOut
);
// find a timestamp matching the criteria for the emitter and event type
QDateTime findTimestampEventOfType(
	const QUaNodeId                    &emitterNodeId,
	const QUaNodeId                    &eventTypeNodeId,
	const QDateTime                    &timestamp,
	const QUaHistoryBackend::TimeMatch &match,
	QQueue<QUaLog>                     &logOut
);
// get the number for events within a time range for the given emitter and event type
quint64 numEventsOfTypeInRange(
	const QUaNodeId &emitterNodeId,
	const QUaNodeId &eventTypeNodeId,
	const QDateTime &timeStart,
	const QDateTime &timeEnd,
	QQueue<QUaLog>  &logOut
);
// return the numPointsToRead events for the given emitter and event type,
// starting from the numPointsOffset offset after given start time (pagination)
QVector<QUaHistoryEventPoint> readHistoryEventsOfType(
	const QUaNodeId &emitterNodeId,
	const QUaNodeId &eventTypeNodeId,
	const QDateTime &timeStart,
	const quint64   &numPointsOffset,
	const quint64   &numPointsToRead,
	const QList<QUaBrowsePath> &columnsToRead,
	QQueue<QUaLog>  &logOut
);
```

To allow **storing** events it is only necessary to implement `writeHistoryEventsOfType`, while the other methods can return default values. The events will be saved to whatever media it is desired. 

Implementing only `writeHistoryEventsOfType`, means clients won't be able to access the historcal events remotely yet, for that it is necessary to implement more methods of the API, as explained further below.

To *store* the events, the API passes the *NodeId* (`const QUaNodeId &eventTypeNodeId`) of the **event type** to be historized, a list of *emitter* nodeIds and the event data.

The `QUaHistoryEventPoint` structure provides the information that needs to be stored:

```c++
struct QUaHistoryEventPoint
{
	QDateTime timestamp;
	QHash<QUaBrowsePath, QVariant> fields;
};
```

Historizing events is slightly more complicated than historizing data. Mainly because *historical data* has always the same structure (`{timestamp, value, status}`), while *event data* changes depending on the *event type*, and there can be any number of event types, including the custom ones. 

Another complication is that in OPC UA, the same event can be *emitted* or *notified* by different objects (objects that are not necessarily be the event's `SourceNode`) according to a [*Event References*](https://reference.opcfoundation.org/v104/Core/docs/Part3/7.18/) organization defined by the OPC specification. So when the event history is queried for a *notifier* node, all the events that were emitted by this node must be retrieved. This relation is specified by the `const QList<QUaNodeId> &emittersNodeIds` argument in the `writeHistoryEventsOfType` historic API method.

Whatever storage media is chosen, events must be *queryable* first by *EventType*, then by *Emitter*, and finally by *Timestamp*. 

For example, if a `SQL` database is chosen for storage, one approach is to create one table for each *EventType*. Each table having a fixed number of columns according to the fixed amount of event fields an *EventType* has. The `const QUaHistoryEventPoint &eventPoint` argument if the `writeHistoryEventsOfType` API method is always guaranteed to contain the same event fields for a given type. So the `QUaHistoryEventPoint::fields` information can be used to create the *EventType* tables.

```sql
CREATE TABLE ":EventTypeNodeId" ( :EventFieldNames :EventFieldTypes );
```

Then create one able to store the *EventType* table names, to be able to relate them to a unique index.

```sql
CREATE TABLE "EventTypeTableNames"
(
	[EventTypeTableNames] INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
	[TableName] TEXT NOT NULL
);
```

To speed up queries, it is recommended to create an *index* over the *TableName* column.

Then a table per *Emitter* can be created with fixed a number of columns that help query and relate to the events stored in the *EventType* tables.

```sql
CREATE TABLE ":EmitterNodeId"
(
	[:EmitterNodeId] INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,
	[Time] INTEGER NOT NULL,      -- to be able to query emitter's events by time range
	[EventType] INTEGER NOT NULL, -- index of EventTypeTableNames table
	[EventId] INTEGER NOT NULL    -- index of event in its EventType table
);
```

To speed up queries, it is recommended to create an *index* over the *Time* and *EventType* columns.

```sql
CREATE INDEX ":EmitterNodeId_Time_EventType" ON ":EmitterNodeId" (Time, EventType);
```

Then the procedure to store an event when the `writeHistoryEventsOfType` API method is called would be:

* Insert new event in its *EventType* table, return new event's *EventType* key.

* Check if the (*EventType*) *TableName* already in the *EventTypeTableNames* table else insert it, fetch the *EventTypeTableNames* key.

* Insert the key of the new event and event type in each emitter table.

Then the rest of the API to query the event history could be imlpemented as follows:

For `eventTypesOfEmitter`:

```sql
SELECT n.TableName FROM EventTypeTableNames n 
INNER JOIN 
(
	SELECT DISTINCT EventType FROM ":EmitterNodeId"
) e
ON n.EventTypeTableNames = e.EventType
```

For `findTimestampEventOfType`:

```sql
-- from above
SELECT e.Time FROM ":EmitterNodeId" e WHERE e.Time >= :Time AND e.EventType = :EventTypeKey ORDER BY e.Time ASC LIMIT 1;
-- from below
SELECT e.Time FROM ":EmitterNodeId" e WHERE e.Time < :Time AND e.EventType = :EventTypeKey ORDER BY e.Time DESC LIMIT 1;
```

For `numEventsOfTypeInRange`:

```sql
SELECT COUNT(*) FROM ":EmitterNodeId" e WHERE e.Time >= :TimeStart AND e.Time <= :TimeEnd AND e.EventType = :EventTypeKey ORDER BY e.Time ASC;
```

For `readHistoryEventsOfType` :

```sql
SELECT * FROM ":EventTypeNodeId" t 
INNER JOIN 
(
	SELECT EventId FROM ":EmitterNodeId" e WHERE e.Time >= :TimeStart AND e.EventType = :EventTypeKey ORDER BY e.Time ASC LIMIT :Limit OFFSET :Offset
) e
ON t.EventTypeNodeId = e.EventId;
```

<p align="center">
  <img src="../res/img/10_historizing_02_events.gif">
</p>

## Historizing Example

The [`quainmemoryhistorizer.cpp`](../examples/10_historizing/quainmemoryhistorizer.cpp) file shows an example of historical data and event storage in memory, while the [`quasqlitehistorizer.cpp`](../examples/10_historizing/quasqlitehistorizer.cpp) file shows an example of historical storage using *Sqlite*.

Note that these examples are provided for illustration purposes only and not for production. The user is encouraged to implement (and if possible, share) their own historizer implementations.

Build and run the [10_historizing](../examples/10_historizing/main.cpp) example to learn more.
