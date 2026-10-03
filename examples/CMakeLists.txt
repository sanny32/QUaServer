# quaserver_add_example(<name> SOURCES <files...> [QT <components...>] [FILES <runtime files...>])
#
# Must be called from the example directory. Runtime files (certificates, keys)
# are copied next to the executable and the example directory is used as
# working directory when debugging in Visual Studio.
function(quaserver_add_example name)
    cmake_parse_arguments(ARG "" "" "SOURCES;QT;FILES" ${ARGN})

    add_executable(${name} ${ARG_SOURCES})
    set_target_properties(${name} PROPERTIES
        AUTOMOC ON
        VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE QUaServer::QUaServer)
    quaserver_deploy_openssl_runtime(${name})

    if(ARG_QT)
        find_package(Qt6 REQUIRED COMPONENTS ${ARG_QT})
        foreach(_component ${ARG_QT})
            target_link_libraries(${name} PRIVATE Qt6::${_component})
        endforeach()
    endif()

    foreach(_file ${ARG_FILES})
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${CMAKE_CURRENT_SOURCE_DIR}/${_file}
                    $<TARGET_FILE_DIR:${name}>
            VERBATIM)
    endforeach()
endfunction()

add_subdirectory(01_basics)
add_subdirectory(02_methods)
add_subdirectory(03_references)
add_subdirectory(04_types)
add_subdirectory(05_server)
add_subdirectory(06_users)
add_subdirectory(07_encryption)
add_subdirectory(08_events)
add_subdirectory(09_serialization)
add_subdirectory(10_historizing)
add_subdirectory(11_alarms_conditions)
add_subdirectory(12_nodesets)
