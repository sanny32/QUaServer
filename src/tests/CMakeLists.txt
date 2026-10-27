find_package(Qt6 REQUIRED COMPONENTS Test Network)

# Compile the client/server helpers once for the entire test suite. All tests use
# the same Qt modules so they can also share one PCH with matching compiler flags.
add_library(quaserver_test_support STATIC
    testclient.cpp testclient.h
    testserver.cpp testserver.h
)
target_include_directories(quaserver_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(quaserver_test_support PUBLIC
    QUaServer::QUaServer Qt6::Test Qt6::Network)
# PCH reuse requires the same definitions and include paths in every test.
target_compile_definitions(quaserver_test_support PUBLIC
    QUASERVER_TEST_NODESET_DIR="${PROJECT_SOURCE_DIR}/src/tests/nodesets"
    QUASERVER_TEST_CERTIFICATES_DIR="${PROJECT_SOURCE_DIR}/examples/07_encryption"
    QUASERVER_TEST_PKI_DIR="${PROJECT_SOURCE_DIR}/src/tests/certificates")
if(QUASERVER_HISTORIZING)
    target_include_directories(quaserver_test_support PUBLIC
        "${PROJECT_SOURCE_DIR}/examples/10_historizing")
endif()
if(QUASERVER_USE_PCH)
    target_precompile_headers(quaserver_test_support PRIVATE <QTest> <QUaServer>)
endif()

function(quaserver_configure_test_environment test_name)
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "QT_FORCE_STDERR_LOGGING=1")
    if(WIN32)
        set_tests_properties(${test_name} PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif()
endfunction()

function(quaserver_add_test test_name source_file)
    add_executable(${test_name} "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
    set_target_properties(${test_name} PROPERTIES AUTOMOC ON)
    target_link_libraries(${test_name} PRIVATE quaserver_test_support)
    if(QUASERVER_USE_PCH)
        target_precompile_headers(${test_name} REUSE_FROM quaserver_test_support)
    endif()
    quaserver_deploy_openssl_runtime(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES LABELS "quaserver")
    quaserver_configure_test_environment(${test_name})
    set_property(GLOBAL APPEND PROPERTY QUASERVER_TEST_TARGETS ${test_name})
endfunction()

# Tests that talk to a running QUaServer through the open62541 client.
function(quaserver_add_integration_test test_name source_file)
    quaserver_add_test(${test_name} ${source_file})
    set_property(TEST ${test_name} APPEND PROPERTY LABELS "integration")
endfunction()

include("${PROJECT_SOURCE_DIR}/cmake/tests_unit.cmake")
include("${PROJECT_SOURCE_DIR}/cmake/tests_integration.cmake")

quaserver_configure_unit_tests()
quaserver_configure_integration_tests()

include(ProcessorCount)
ProcessorCount(QUASERVER_TEST_PARALLEL_LEVEL)
if(QUASERVER_TEST_PARALLEL_LEVEL EQUAL 0)
    set(QUASERVER_TEST_PARALLEL_LEVEL 1)
endif()

get_property(test_targets GLOBAL PROPERTY QUASERVER_TEST_TARGETS)
add_custom_target(quaserver_check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
            --parallel ${QUASERVER_TEST_PARALLEL_LEVEL} -L quaserver
    DEPENDS ${test_targets}
    USES_TERMINAL
)
