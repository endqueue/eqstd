cmake_minimum_required(VERSION 3.30)

# The version is written once, in include/eqstd.h; read it from there.
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/include/eqstd.h" _eqstd_version_lines
     REGEX "^#define EQSTD_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(_part MAJOR MINOR PATCH)
    string(REGEX REPLACE ".*EQSTD_VERSION_${_part} ([0-9]+).*" "\\1"
           EQSTD_VERSION_${_part} "${_eqstd_version_lines}")
    if(NOT EQSTD_VERSION_${_part} MATCHES "^[0-9]+$")
        message(FATAL_ERROR "eqstd: no EQSTD_VERSION_${_part} line in include/eqstd.h")
    endif()
endforeach()
project(eqstd
        VERSION ${EQSTD_VERSION_MAJOR}.${EQSTD_VERSION_MINOR}.${EQSTD_VERSION_PATCH}
        LANGUAGES C CXX)

# Don't do anything if another project already added eqstd.
if(NOT TARGET eqstd)
    # Header-only: an interface target that carries the include folder and
    # the language levels the headers need (C11, C++20 for std::span).
    add_library(eqstd INTERFACE)
    target_include_directories(eqstd INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(eqstd INTERFACE c_std_11 cxx_std_20)
    target_sources(eqstd INTERFACE
        FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
        FILES include/eqstd.h include/eqstd.hpp)
    # _DEBUG in a Debug build, for every compiler, as the workspace has it:
    # MSVC's debug CRT defines it; for GCC and Clang it is added here.
    # EQ_DEBUG (the index checks) follows it.
    if(NOT MSVC)
        target_compile_definitions(eqstd INTERFACE $<$<CONFIG:Debug>:_DEBUG>)
    endif()

    option(EQSTD_BUILD_TESTS "Build eqstd's tests" ${PROJECT_IS_TOP_LEVEL})
    if(EQSTD_BUILD_TESTS)
        enable_testing()

        # Warnings, all of them errors. The headers are included as ordinary
        # (not system) headers, so they are held to these too.
        if(MSVC)
            set(_eqstd_warnings /W4 /WX)
        else()
            set(_eqstd_warnings -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion
                -Wsign-conversion -Wcast-qual -Wcast-align -Wundef -Wdouble-promotion
                -Wformat=2 -Wnull-dereference)
            set(_eqstd_cxx_warnings -Wold-style-cast -Wnon-virtual-dtor
                -Woverloaded-virtual -Wzero-as-null-pointer-constant)
        endif()

        # eqstd_add_test(<name> <language standard property> <standard> <source>...)
        function(eqstd_add_test name standard_property standard)
            add_executable(${name} ${ARGN})
            target_link_libraries(${name} PRIVATE eqstd)
            target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
            set_target_properties(${name} PROPERTIES
                ${standard_property} ${standard}
                ${standard_property}_REQUIRED ON
                C_EXTENSIONS OFF
                CXX_EXTENSIONS OFF)
            target_compile_options(${name} PRIVATE ${_eqstd_warnings})
            if(NOT MSVC)
                target_compile_options(${name} PRIVATE
                    $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:${_eqstd_cxx_warnings}>)
            endif()
            add_test(NAME ${name} COMMAND ${name})
        endfunction()

        # The C half: as C11, as C++ (unchanged), and its Debug index checks.
        eqstd_add_test(eqstd_c_test C_STANDARD 11 tests/eqstd_c_test.c)
        eqstd_add_test(eqstd_c_as_cpp_test CXX_STANDARD 20 tests/eqstd_c_as_cpp_test.cpp)
        eqstd_add_test(eqstd_index_test C_STANDARD 11 tests/eqstd_index_test.c)
        eqstd_add_test(eqstd_index_cpp_test CXX_STANDARD 20 tests/eqstd_index_cpp_test.cpp)

        # The C++ half at the oldest standard it supports and at the
        # workspace's.
        eqstd_add_test(eqstd_cpp_test CXX_STANDARD 20 tests/eqstd_cpp_test.cpp)
        eqstd_add_test(eqstd_cpp23_test CXX_STANDARD 23 tests/eqstd_cpp_test.cpp)

        # Containers on several threads at once; what the -tsan presets are for.
        find_package(Threads REQUIRED)
        eqstd_add_test(eqstd_threads_test CXX_STANDARD 20 tests/eqstd_threads_test.cpp)
        target_link_libraries(eqstd_threads_test PRIVATE Threads::Threads)

        # Objective-C and Objective-C++, unchanged, where Apple's compiler is.
        if(APPLE)
            enable_language(OBJC)
            enable_language(OBJCXX)
            eqstd_add_test(eqstd_objc_test OBJC_STANDARD 11 tests/eqstd_objc_test.m)
            eqstd_add_test(eqstd_objcxx_test OBJCXX_STANDARD 20 tests/eqstd_objcxx_test.mm)
        endif()
    endif()
endif()
