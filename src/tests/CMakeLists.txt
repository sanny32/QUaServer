find_package(Qt6 REQUIRED COMPONENTS Test Network)

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
    target_include_directories(${test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${test_name} PRIVATE QUaServer::QUaServer Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES LABELS "quaserver")
    quaserver_configure_test_environment(${test_name})
    set_property(GLOBAL APPEND PROPERTY QUASERVER_TEST_TARGETS ${test_name})
endfunction()

# Tests that talk to a running QUaServer through the open62541 client.
function(quaserver_add_integration_test test_name source_file)
    quaserver_add_test(${test_name} ${source_file})
    target_sources(${test_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/testclient.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/testclient.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/testserver.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/testserver.h")
    target_link_libraries(${test_name} PRIVATE Qt6::Network)
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
