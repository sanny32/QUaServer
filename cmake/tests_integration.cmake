function(quaserver_configure_integration_tests)
    quaserver_add_integration_test(quaserver_tests_sessions_integration test_sessions_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_services_integration test_services_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_endpoints_integration test_endpoints_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_limits_integration test_limits_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_nodemanagement_integration test_nodemanagement_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_nodeset_integration test_nodeset_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_nodeset_compatibility test_nodeset_compatibility.cpp)
    quaserver_add_integration_test(quaserver_tests_nodeset_export_integration test_nodeset_export_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_structures_integration test_structures_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_discovery_integration test_discovery_integration.cpp)

    if(QUASERVER_EVENTS)
        quaserver_add_integration_test(quaserver_tests_events_integration test_events_integration.cpp)
    endif()

    if(QUASERVER_ALARMS_CONDITIONS)
        quaserver_add_integration_test(quaserver_tests_alarms_integration test_alarms_integration.cpp)
    endif()

    if(QUASERVER_HISTORIZING)
        quaserver_add_integration_test(quaserver_tests_history_integration test_history_integration.cpp)
        target_sources(quaserver_tests_history_integration PRIVATE
            "${PROJECT_SOURCE_DIR}/examples/10_historizing/quainmemoryhistorizer.cpp"
            "${PROJECT_SOURCE_DIR}/examples/10_historizing/quainmemoryhistorizer.h")
    endif()

    if(QUASERVER_ENCRYPTION)
        quaserver_add_integration_test(quaserver_tests_encryption_integration test_encryption_integration.cpp)
    endif()
endfunction()
