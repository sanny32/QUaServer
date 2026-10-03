# QUaServer

[![Test CI](https://github.com/sanny32/QUaServer/actions/workflows/test-ci.yml/badge.svg)](https://github.com/sanny32/QUaServer/actions/workflows/test-ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

A [Qt](https://www.qt.io/) based C++ library that wraps the [open62541](https://open62541.org/) library and provides an object-oriented API for OPC UA servers.

Part of the flexibility of the *open62541* server API is traded for ease of use: the address space is built from `QObject` based C++ classes, values and events are exposed through Qt signals and properties, and the server runs on the Qt event loop. The goal is to prototype OPC UA servers quickly, without spending much time on complex address space structures. If more flexibility is required, use *open62541* directly.

Features:

* Objects, variables, properties and folders, with custom object and variable types
* Methods callable by clients, from callbacks, lambdas or `Q_INVOKABLE` methods
* Custom non-hierarchical references
* User accounts with per-user access control, and session tracking
* Server certificates and encrypted communication
* Events, alarms and conditions
* Serialization of the address space
* Historizing of data and events

Test the library properly before using it in production. Please report any issue with a minimal working example that reproduces it. To browse a running server, a client such as [UaExpert](https://www.unified-automation.com/downloads/opc-ua-clients.html) is recommended.

## Quick Start

```c++
#include <QCoreApplication>
#include <QUaServer>

int main(int argc, char *argv[])
{
	QCoreApplication a(argc, argv);

	QUaServer server;

	QUaFolderObject *objsFolder = server.objectsFolder();
	QUaBaseDataVariable *varTemperature = objsFolder->addBaseDataVariable("Temperature");
	varTemperature->setValue(21.5);

	objsFolder->addMethod("addNumbers", [](int x, int y) {
		return x + y;
	});

	server.start();

	return a.exec();
}
```

The server listens on `opc.tcp://localhost:4840`. Continue with the [guide](docs/README.md) to learn more.

## Requirements

* Qt 6.9 or higher (`Core`)
* A C++17 compiler
* CMake 3.21 or higher
* Git and Python 3, to fetch and generate the *open62541* sources
* OpenSSL 3, only for encryption (see [Encryption](docs/07_encryption.md))

The [open62541](https://github.com/open62541/open62541) library is cloned by CMake ([FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html)) into the build directory at configure time, so the first configuration needs network access. Its version is pinned by `QUASERVER_OPEN62541_VERSION` in [`cmake/Dependencies.cmake`](cmake/Dependencies.cmake). To build offline from a local *open62541* checkout, pass `-DFETCHCONTENT_SOURCE_DIR_OPEN62541=<path to open62541>`.

## Build

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path to Qt6>
cmake --build build
```

The examples are built by default and placed in `build/bin`. On Windows, make sure Qt's `bin` directory is in `PATH` (or run `windeployqt`) before running them.

### Build Options

| Option | Default | Description |
|---|---|---|
| `QUASERVER_NAMESPACE_FULL` | `OFF` | Build *open62541* with the full namespace zero |
| `QUASERVER_ENCRYPTION` | `OFF` | Encryption support (requires *OpenSSL* 3) |
| `QUASERVER_EVENTS` | `OFF` | Events support (implies `QUASERVER_NAMESPACE_FULL`) |
| `QUASERVER_ALARMS_CONDITIONS` | `OFF` | Alarms and conditions support (implies `QUASERVER_EVENTS`) |
| `QUASERVER_HISTORIZING` | `OFF` | Historizing support |
| `QUASERVER_BUILD_EXAMPLES` | `ON` (top level) | Build the examples |
| `QUASERVER_BUILD_TESTS` | `OFF` | Build the tests |

The full namespace zero makes the binaries considerably larger. When changing options of an existing build directory, rebuild the complete project.

### Tests

The tests use *Qt Test* and *CTest* and live in [`src/tests`](src/tests). Integration tests start the server on a free local port and talk to it with the *open62541* client. Tests for encryption, events, alarms and historizing are built only when the matching option is enabled.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path to Qt6> -DQUASERVER_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The `quaserver_check` target builds and runs all of them at once.

## Use in Your Project

*QUaServer* is meant to be built as part of the host project, either fetched by CMake or added as a subdirectory (e.g. a git submodule). Set the build options before adding it, then link against the `QUaServer::QUaServer` target:

```cmake
cmake_minimum_required(VERSION 3.21)
project(my_project LANGUAGES C CXX)

set(QUASERVER_EVENTS ON)
set(QUASERVER_ENCRYPTION ON)

include(FetchContent)
FetchContent_Declare(QUaServer
    GIT_REPOSITORY https://github.com/sanny32/QUaServer.git
    GIT_TAG        <commit or tag>
)
FetchContent_MakeAvailable(QUaServer)
# or, with the sources in the project tree:
# add_subdirectory(QUaServer)

add_executable(my_project main.cpp)
target_link_libraries(my_project PRIVATE QUaServer::QUaServer)
# Windows only: copies the OpenSSL DLLs next to the executable when encryption is enabled
quaserver_deploy_openssl_runtime(my_project)
```

When embedded:

* *QUaServer* and *open62541* are always built as static libraries with position-independent code, regardless of the host project's `BUILD_SHARED_LIBS`, so they can be linked into executables, shared libraries and plugins.
* Examples are not built by default, and the host project's `cmake --install` installs nothing from *QUaServer* or *open62541*: they are already linked into the host binaries.
* At run time the application needs the Qt libraries and, with `QUASERVER_ENCRYPTION`, the OpenSSL libraries (see [Encryption](docs/07_encryption.md)); deploy them along with it.
* The binaries contain third-party code with its own license terms, see [Third-party software](#third-party-software).

## Documentation

The [guide](docs/README.md) explains every feature step by step, each chapter with a matching example in [`examples`](examples):

[Basics](docs/01_basics.md) ·
[Methods](docs/02_methods.md) ·
[References](docs/03_references.md) ·
[Types](docs/04_types.md) ·
[Server](docs/05_server.md) ·
[Users](docs/06_users.md) ·
[Encryption](docs/07_encryption.md) ·
[Events](docs/08_events.md) ·
[Serialization](docs/09_serialization.md) ·
[Historizing](docs/10_historizing.md) ·
[Alarms](docs/11_alarms.md)

## Third-party software

*QUaServer* source code is licensed under MIT, except `src/wrapper/quatypesconverter.h` and `quatypesconverter.cpp`, which are adapted from [Qt OPC UA](https://github.com/qt/qtopcua) and licensed under LGPL-3.0. Binaries built with *QUaServer* also contain *open62541* and, optionally, OpenSSL, each with its own license terms. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the accompanying [`licenses`](licenses/) directory for the component list and license texts.

## MIT License

Copyright (c) 2019 - 2020 Juan Gonzalez Burgos  
Copyright (c) 2026 Alexandr Ananev

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
