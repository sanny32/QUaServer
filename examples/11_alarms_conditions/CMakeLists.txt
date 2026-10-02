set(_sources main.cpp)
set(_qt)
if(QUASERVER_HISTORIZING)
    list(APPEND _sources
        ../10_historizing/quainmemoryhistorizer.cpp
        ../10_historizing/quainmemoryhistorizer.h
        ../10_historizing/quasqlitehistorizer.cpp
        ../10_historizing/quasqlitehistorizer.h
    )
    set(_qt Sql)
endif()

quaserver_add_example(11_alarms_conditions
    SOURCES ${_sources}
    QT      ${_qt})
