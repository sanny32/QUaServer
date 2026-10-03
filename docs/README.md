# QUaServer Guide

Each chapter builds on the previous ones and has a matching example in [`examples`](../examples). Chapters marked with a CMake option need the library to be configured with it (see [Build options](../README.md#build-options)).

| # | Chapter | Content | Example | Option |
|---|---|---|---|---|
| 1 | [Basics](01_basics.md) | Create the server, objects and variables, and access their attributes | [01_basics](../examples/01_basics/main.cpp) | |
| 2 | [Methods](02_methods.md) | Add methods that clients can call remotely | [02_methods](../examples/02_methods/main.cpp) | |
| 3 | [References](03_references.md) | Create and browse custom non-hierarchical references | [03_references](../examples/03_references/main.cpp) | |
| 4 | [Types](04_types.md) | Create custom object and variable types | [04_types](../examples/04_types/main.cpp) | |
| 5 | [Server](05_server.md) | Create certificates and set the server description | [05_server](../examples/05_server/main.cpp) | |
| 6 | [Users](06_users.md) | User accounts, per-user access control and sessions | [06_users](../examples/06_users/main.cpp) | |
| 7 | [Encryption](07_encryption.md) | Encrypt the communication with clients | [07_encryption](../examples/07_encryption/main.cpp) | `QUASERVER_ENCRYPTION` |
| 8 | [Events](08_events.md) | Create and trigger custom events | [08_events](../examples/08_events/main.cpp) | `QUASERVER_EVENTS` |
| 9 | [Serialization](09_serialization.md) | Save and restore the address space | [09_serialization](../examples/09_serialization/main.cpp) | |
| 10 | [Historizing](10_historizing.md) | Store and serve historical data and events | [10_historizing](../examples/10_historizing/main.cpp) | `QUASERVER_HISTORIZING` |
| 11 | [Alarms](11_alarms.md) | Alarms and conditions | [11_alarms_conditions](../examples/11_alarms_conditions/main.cpp) | `QUASERVER_ALARMS_CONDITIONS` |
| 12 | [NodeSets](12_nodesets.md) | Load information models from NodeSet2 XML files | [12_nodesets](../examples/12_nodesets/main.cpp) | |

Reference:

* [How validation works](validation.md) – background on the PKI used by OPC UA server validation.
* [Implemented OPC UA types](wrapper_types.md) – the C++ classes and the OPC UA types they implement.
