# Third-Party Software Notices

QUaServer source code is licensed under the MIT License in `LICENSE`, except
where noted below. QUaServer is distributed as source code; the third-party
components below are fetched or located at build time and become part of the
binaries built with it. Each component remains subject to its own license.
Applications that distribute such binaries must comply with these licenses.

The referenced license texts are under `licenses/`.

## QUaTypesConverter

Files: `src/wrapper/quatypesconverter.h`, `src/wrapper/quatypesconverter.cpp`.

Adapted from `qopen62541valueconverter.h` and `qopen62541valueconverter.cpp` of
Qt OPC UA 6.9.

Copyright (C) 2017 The Qt Company Ltd.

License: LGPL-3.0-only. The license text is provided in
`licenses/LGPL-3.0-only.txt`; the GPLv3 text it refers to is provided in
`licenses/GPL-3.0-only.txt`.

Source: https://github.com/qt/qtopcua/tree/6.9/src/plugins/opcua/open62541

## Qt 6

Components: Qt Core; the tests also use Qt Test and Qt Network.

Copyright (C) The Qt Company Ltd. and other contributors.

QUaServer links Qt dynamically. Qt is available under commercial terms or the
GNU Lesser General Public License version 3, provided in
`licenses/LGPL-3.0-only.txt` and `licenses/GPL-3.0-only.txt`.

Source: https://code.qt.io/cgit/qt/

## open62541

Version: 1.5.8, statically linked into QUaServer.

Copyright (C) The open62541 authors; contributors retain their individual
copyright.

The open62541 library is licensed under MPL-2.0 (`licenses/MPL-2.0.txt`). Some
of its plugins, such as the default server configuration, access control and
logging, are in the public domain under CC0-1.0 (`licenses/CC0-1.0.txt`).

The build also compiles the following files from the open62541 `deps/`
directory, each under its own license:

| Files | License | Text |
|---|---|---|
| `ziptree`, `utf8` | MPL-2.0 | `licenses/MPL-2.0.txt` |
| `cj5`, `itoa`, `mp_printf`, `yxml`, `libc_time`, `parse_num`, `musl_inet_pton` | MIT | `licenses/MIT-open62541.txt` |
| `base64`, `open62541_queue.h` | BSD-3-Clause | `licenses/BSD-3-Clause-open62541.txt` |
| `dtoa` | BSL-1.0 | `licenses/BSL-1.0-open62541.txt` |
| `pcg_basic` | Apache-2.0 | `licenses/Apache-2.0.txt` |

Source: https://github.com/open62541/open62541/tree/v1.5.8

## UA-Nodeset

Used when the full namespace zero is enabled (`QUASERVER_NAMESPACE_FULL`,
`QUASERVER_EVENTS` or `QUASERVER_ALARMS_CONDITIONS`): the namespace zero
compiled into open62541 is generated from the UA-Nodeset schema files.

Copyright (c) 2005-2024 The OPC Foundation, Inc.

License: OPC Foundation MIT License 1.00, provided in
`licenses/MIT-UA-Nodeset.txt`.

Source: https://github.com/OPCFoundation/UA-Nodeset/tree/257db9ad98ee7ba4b67d3500b54bfc9d744a36af/Schema

## OpenSSL

Version: OpenSSL 3.x. Used only when encryption is enabled
(`QUASERVER_ENCRYPTION`) and linked dynamically. On Windows, the build copies
the OpenSSL DLLs shipped with Qt next to the examples and tests.

Copyright The OpenSSL Project Authors.

License: Apache-2.0. The license text is provided in `licenses/Apache-2.0.txt`;
acknowledgements are provided in `licenses/OpenSSL-ACKNOWLEDGEMENTS.md`.

Source: https://github.com/openssl/openssl
