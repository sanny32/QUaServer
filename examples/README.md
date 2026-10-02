# Build Examples

Requires `Qt 6.9+`, `CMake 3.21+` and `Python 3`. The examples are built from the root of the repository:

```bash
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path to Qt6>
cmake --build build
```

Features are enabled with CMake options, for example to build all of them:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path to Qt6> \
      -DQUASERVER_ALARMS_CONDITIONS=ON -DQUASERVER_HISTORIZING=ON -DQUASERVER_ENCRYPTION=ON
```

The executables are placed in `build/bin`. On Windows, make sure Qt's `bin` directory is in `PATH` (or run `windeployqt`) before running them.
