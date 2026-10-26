# Applies the patches given in PATCHES to the current source directory; skips the ones already applied,
# since FetchContent may run the patch step again on an existing checkout.

find_package(Git REQUIRED)

foreach(_patch IN LISTS PATCHES)
    execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${_patch}
                    RESULT_VARIABLE _applied OUTPUT_QUIET ERROR_QUIET)
    if(_applied EQUAL 0)
        continue()
    endif()
    execute_process(COMMAND ${GIT_EXECUTABLE} apply ${_patch} RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Could not apply ${_patch}")
    endif()
endforeach()
