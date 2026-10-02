set(_sources
    main.cpp
    quainmemoryhistorizer.cpp
    quainmemoryhistorizer.h
    quasqlitehistorizer.cpp
    quasqlitehistorizer.h
)
if(QUASERVER_EVENTS)
    list(APPEND _sources
        ../08_events/myevent.cpp
        ../08_events/myevent.h
    )
endif()

quaserver_add_example(10_historizing
    SOURCES ${_sources}
    QT      Sql)
