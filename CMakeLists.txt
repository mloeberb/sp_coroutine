cmake_minimum_required(VERSION 3.15)

project(sp_coroutine
    VERSION 0.1.0
    DESCRIPTION "Asymmetric coroutine library using recursive stack frames"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Default to Release for single-config generators (Make, Ninja). Multi-config
# generators (Visual Studio, Xcode) ignore CMAKE_BUILD_TYPE; pass --config at
# build/test time instead.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

function(sp_co_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# Library
add_library(sp_coroutine STATIC sp_coroutine.c sp_coroutine.h)
target_include_directories(sp_coroutine PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)
sp_co_set_warnings(sp_coroutine)

# Tests
enable_testing()

set(SP_CO_TESTS
    test_simple
    test_multi_worker
    test_overflow
    test_no_overflow
    test_bad_resumer
    test_stack_isolation
    test_yield_from_main
    test_state_transitions
    test_remove_and_reuse
)

foreach(test_name IN LISTS SP_CO_TESTS)
    add_executable(${test_name} ${test_name}.c)
    target_link_libraries(${test_name} PRIVATE sp_coroutine)
    sp_co_set_warnings(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
