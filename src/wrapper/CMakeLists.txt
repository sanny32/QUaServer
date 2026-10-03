set(QUASERVER_SOURCES
    quaserver.cpp
    quaserver_anex.cpp
    quanode.cpp
    quabasevariable.cpp
    quaproperty.cpp
    quabasedatavariable.cpp
    quabaseobject.cpp
    quafolderobject.cpp
    quacustomdatatypes.cpp
    quaenum.cpp
    quatypesconverter.cpp
    quanodesetloader.cpp
)

set(QUASERVER_HEADERS
    pch_open62541.h
    quaserver.h
    quaserver_anex.h
    quanode.h
    quabasevariable.h
    quaproperty.h
    quabasedatavariable.h
    quabaseobject.h
    quafolderobject.h
    quacustomdatatypes.h
    quaenum.h
    quatypesconverter.h
    quanodesetloader.h
    QUaServer
    QUaNode
    QUaBaseVariable
    QUaProperty
    QUaBaseDataVariable
    QUaBaseObject
    QUaFolderObject
    QUaCustomDataTypes
    QUaEnum
    QUaTypesConverter
)

if(QUASERVER_EVENTS)
    list(APPEND QUASERVER_SOURCES
        quabaseevent.cpp
        quabasemodelchangeevent.cpp
        quageneralmodelchangeevent.cpp
        quasystemevent.cpp
    )
    list(APPEND QUASERVER_HEADERS
        quabaseevent.h
        quabasemodelchangeevent.h
        quageneralmodelchangeevent.h
        quasystemevent.h
        QUaBaseEvent
        QUaBaseModelChangeEvent
        QUaGeneralModelChangeEvent
        QUaSystemEvent
    )
endif()

if(QUASERVER_ALARMS_CONDITIONS)
    foreach(_name
            conditionvariable statevariable finitestatevariable twostatevariable
            transitionvariable finitetransitionvariable statemachine
            finitestatemachine state transition exclusivelimitstatemachine
            condition acknowledgeablecondition alarmcondition discretealarm
            offnormalalarm limitalarm exclusivelimitalarm exclusivelevelalarm
            refreshstartevent refreshendevent refreshrequiredevent transitionevent)
        list(APPEND QUASERVER_SOURCES qua${_name}.cpp)
        list(APPEND QUASERVER_HEADERS qua${_name}.h)
    endforeach()
    list(APPEND QUASERVER_HEADERS
        QUaConditionVariable QUaStateVariable QUaFiniteStateVariable
        QUaTwoStateVariable QUaTransitionVariable QUaFiniteTransitionVariable
        QUaStateMachine QUaFiniteStateMachine QUaState QUaTransition
        QUaExclusiveLimitStateMachine QUaCondition QUaAcknowledgeableCondition
        QUaAlarmCondition QUaDiscreteAlarm QUaOffNormalAlarm QUaLimitAlarm
        QUaExclusiveLimitAlarm QUaExclusiveLevelAlarm QUaRefreshStartEvent
        QUaRefreshEndEvent QUaRefreshRequiredEvent QUaTransitionEvent
    )
endif()

if(QUASERVER_HISTORIZING)
    list(APPEND QUASERVER_SOURCES quahistorybackend.cpp)
    list(APPEND QUASERVER_HEADERS quahistorybackend.h QUaHistoryBackend)
endif()

if(QUASERVER_NAMESPACE_FULL)
    list(APPEND QUASERVER_SOURCES quaoptionset.cpp quaoptionsetvariable.cpp)
    list(APPEND QUASERVER_HEADERS quaoptionset.h quaoptionsetvariable.h
                                  QUaOptionSet QUaOptionSetVariable)
endif()

add_library(QUaServer STATIC ${QUASERVER_SOURCES} ${QUASERVER_HEADERS})
add_library(QUaServer::QUaServer ALIAS QUaServer)

set_target_properties(QUaServer PROPERTIES
    AUTOMOC ON
    DEBUG_POSTFIX d
)

target_include_directories(QUaServer PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_compile_features(QUaServer PUBLIC cxx_std_17)

target_link_libraries(QUaServer PUBLIC
    Qt6::Core
    open62541::open62541
)

if(WIN32)
    # quaserver.cpp resolves client addresses with Winsock
    target_link_libraries(QUaServer PRIVATE ws2_32)
endif()

# Qt enables strict QList iterators everywhere except Windows; match that so MSVC builds catch the same errors
target_compile_definitions(QUaServer PRIVATE QT_STRICT_QLIST_ITERATORS)

if(MSVC)
    # macro redefinition warnings coming from open62541 headers
    target_compile_options(QUaServer PUBLIC /wd4005)
endif()
