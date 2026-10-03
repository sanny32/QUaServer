# Fetches open62541 at configure time and builds it as part of the main build.
# Encryption uses OpenSSL: the one shipped with Qt on Windows, the system one elsewhere.

include(FetchContent)

set(QUASERVER_OPEN62541_VERSION "v1.5.8")
# deps/ua-nodeset commit pinned by that open62541 release
set(QUASERVER_UA_NODESET_COMMIT "257db9ad98ee7ba4b67d3500b54bfc9d744a36af")

# Helper to force a cache option of a subproject
macro(quaserver_set_cache name value type)
    set(${name} ${value} CACHE ${type} "" FORCE)
endmacro()

# Full namespace zero needs only these UA-Nodeset files; the whole repo is ~250 MB with paths beyond MAX_PATH.
function(quaserver_fetch_ua_nodeset_schema)
    set(_dir ${CMAKE_BINARY_DIR}/_deps/ua-nodeset)
    set(_url https://raw.githubusercontent.com/OPCFoundation/UA-Nodeset/${QUASERVER_UA_NODESET_COMMIT}/Schema)
    set(_files
        Opc.Ua.NodeSet2.xml 79d4e0d787cac6234acfe72d48d370b316ec811176bf912c7f1ed199a4c94d5a
        NodeIds.csv         b9ab8d8f221324430ec88d34baf3f7f8511fec454bbdf487d54fe2c3ff573d96
        StatusCode.csv      18c4826d221941912a2ed982e2ea5a43aac977de02977b33d8ccaab367157464
        Opc.Ua.Types.bsd    e1d7fa8e4b3f49ffd2dc518409650b5a29bfa5a5b454990926e85c1fc8537766
    )
    while(_files)
        list(POP_FRONT _files _name _sha256)
        file(DOWNLOAD ${_url}/${_name} ${_dir}/Schema/${_name}
             EXPECTED_HASH SHA256=${_sha256}
             TLS_VERIFY ON)
    endwhile()
    quaserver_set_cache(UA_NODESET_DIR ${_dir} STRING)
endfunction()

# Points FindOpenSSL to the toolkit the Qt installer puts in <Qt root>/Tools/OpenSSLv3,
# unless OPENSSL_ROOT_DIR is given explicitly.
function(quaserver_hint_qt_openssl)
    if(NOT WIN32 OR OPENSSL_ROOT_DIR OR DEFINED ENV{OPENSSL_ROOT_DIR})
        return()
    endif()
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(_arch Win_x86)
    else()
        set(_arch Win_x64)
    endif()
    get_filename_component(_root "${QT6_INSTALL_PREFIX}/../../Tools/OpenSSLv3/${_arch}" ABSOLUTE)
    if(EXISTS ${_root}/include/openssl/opensslv.h)
        set(OPENSSL_ROOT_DIR ${_root} PARENT_SCOPE)
    endif()
endfunction()

# Copies the OpenSSL runtime DLLs next to the executable <target> on Windows.
# Reads only cache variables, so host projects can call it from any directory.
function(quaserver_deploy_openssl_runtime target)
    if(NOT WIN32 OR NOT QUASERVER_ENCRYPTION OR NOT OPENSSL_INCLUDE_DIR)
        return()
    endif()
    get_filename_component(_bin "${OPENSSL_INCLUDE_DIR}/../bin" ABSOLUTE)
    file(GLOB _dlls "${_bin}/libcrypto-*.dll" "${_bin}/libssl-*.dll")
    if(_dlls)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_dlls} $<TARGET_FILE_DIR:${target}>
            VERBATIM)
    endif()
endfunction()

# ------------------------------------------------------------------------------
# OpenSSL
# ------------------------------------------------------------------------------
if(QUASERVER_ENCRYPTION)
    quaserver_hint_qt_openssl()
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    quaserver_set_cache(UA_ENABLE_ENCRYPTION OPENSSL STRING)
else()
    quaserver_set_cache(UA_ENABLE_ENCRYPTION OFF STRING)
endif()

# ------------------------------------------------------------------------------
# open62541
# ------------------------------------------------------------------------------
if(QUASERVER_NAMESPACE_FULL)
    quaserver_fetch_ua_nodeset_schema()
    quaserver_set_cache(UA_NAMESPACE_ZERO FULL STRING)
else()
    quaserver_set_cache(UA_NAMESPACE_ZERO REDUCED STRING)
endif()

quaserver_set_cache(UA_ENABLE_SUBSCRIPTIONS_EVENTS            ${QUASERVER_EVENTS}            BOOL)
quaserver_set_cache(UA_ENABLE_SUBSCRIPTIONS_ALARMS_CONDITIONS ${QUASERVER_ALARMS_CONDITIONS} BOOL)
quaserver_set_cache(UA_ENABLE_HISTORIZING                     ${QUASERVER_HISTORIZING}       BOOL)

quaserver_set_cache(UA_ENABLE_AMALGAMATION   OFF BOOL)
quaserver_set_cache(UA_ENABLE_PUBSUB         OFF BOOL)
quaserver_set_cache(UA_BUILD_EXAMPLES        OFF BOOL)
quaserver_set_cache(UA_BUILD_UNIT_TESTS      OFF BOOL)
quaserver_set_cache(UA_BUILD_TOOLS           OFF BOOL)
quaserver_set_cache(UA_ENABLE_DEBUG_SANITIZER OFF BOOL)
# Link against the same (dynamic) C runtime as Qt
quaserver_set_cache(UA_MSVC_FORCE_STATIC_CRT OFF BOOL)

# QUaServer is a static library and expects open62541 to be linked into it, whatever the host project builds
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(open62541
    GIT_REPOSITORY https://github.com/open62541/open62541.git
    GIT_TAG        ${QUASERVER_OPEN62541_VERSION}
    GIT_SHALLOW    TRUE
    # the submodules serve disabled features, except ua-nodeset which is fetched separately
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(open62541)
# open62541 is linked into QUaServer, so a host project's install must not ship its headers and library
set_property(DIRECTORY ${open62541_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)
