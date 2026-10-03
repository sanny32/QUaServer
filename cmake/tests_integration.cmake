function(quaserver_configure_integration_tests)
    quaserver_add_integration_test(quaserver_tests_sessions_integration test_sessions_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_services_integration test_services_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_endpoints_integration test_endpoints_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_limits_integration test_limits_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_nodemanagement_integration test_nodemanagement_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_nodeset_integration test_nodeset_integration.cpp)
    quaserver_add_integration_test(quaserver_tests_structures_integration test_structures_integration.cpp)
    target_compile_definitions(quaserver_tests_nodeset_integration PRIVATE
        QUASERVER_TEST_NODESET_DIR="${PROJECT_SOURCE_DIR}/src/tests/nodesets")
    set(locked_tests
        quaserver_tests_sessions_integration
        quaserver_tests_services_integration
        quaserver_tests_endpoints_integration
        quaserver_tests_limits_integration
        quaserver_tests_nodemanagement_integration
        quaserver_tests_nodeset_integration
        quaserver_tests_structures_integration)

    if(QUASERVER_EVENTS)
        quaserver_add_integration_test(quaserver_tests_events_integration test_events_integration.cpp)
        list(APPEND locked_tests quaserver_tests_events_integration)
    endif()

    if(QUASERVER_ALARMS_CONDITIONS)
        quaserver_add_integration_test(quaserver_tests_alarms_integration test_alarms_integration.cpp)
        list(APPEND locked_tests quaserver_tests_alarms_integration)
    endif()

    if(QUASERVER_HISTORIZING)
        quaserver_add_integration_test(quaserver_tests_history_integration test_history_integration.cpp)
        target_sources(quaserver_tests_history_integration PRIVATE
            "${PROJECT_SOURCE_DIR}/examples/10_historizing/quainmemoryhistorizer.cpp"
            "${PROJECT_SOURCE_DIR}/examples/10_historizing/quainmemoryhistorizer.h")
        target_include_directories(quaserver_tests_history_integration PRIVATE
            "${PROJECT_SOURCE_DIR}/examples/10_historizing")
        list(APPEND locked_tests quaserver_tests_history_integration)
    endif()

    if(QUASERVER_ENCRYPTION)
        quaserver_add_integration_test(quaserver_tests_encryption_integration test_encryption_integration.cpp)
        target_compile_definitions(quaserver_tests_encryption_integration PRIVATE
            QUASERVER_TEST_CERTIFICATES_DIR="${PROJECT_SOURCE_DIR}/examples/07_encryption"
            QUASERVER_TEST_PKI_DIR="${PROJECT_SOURCE_DIR}/src/tests/certificates")
        list(APPEND locked_tests quaserver_tests_encryption_integration)
    endif()

    # A free port is probed before the server binds it, so concurrent servers could race for it.
    set_property(TEST ${locked_tests} APPEND PROPERTY RESOURCE_LOCK opcua_test_server)
endfunction()
