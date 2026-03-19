# Build the 6502 code generator from B2.
#
# Copyright (C) 2016-2024 by Tom Seddon
# License: GPL v3 (see COPYING in this directory)
#
# This builds a minimal subset of B2's shared library and the 6502_gen
# executable. The generator reads 6502.c and produces 6502_internal.inl.

cmake_minimum_required(VERSION 3.16)

include(CheckCXXSourceCompiles)

# --- Minimal shared library (subset of B2's shared_lib) ---

set(B2_SHARED_H ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/h)
set(B2_SHARED_C ${CMAKE_CURRENT_SOURCE_DIR}/src/shared/c)

# Detect strlcpy (available on macOS/BSD, not Linux/Windows)
check_cxx_source_compiles("#include <string.h>
typedef decltype(&strlcpy) T;int main(void){return 0;}" B2_HAVE_STRLCPY)

set(B2_SHARED_SRCS
    ${B2_SHARED_C}/system.cpp
    ${B2_SHARED_C}/log.cpp
    ${B2_SHARED_C}/CommandLineParser.cpp
    ${B2_SHARED_C}/mutex.cpp
    ${B2_SHARED_C}/debug.cpp
    ${B2_SHARED_C}/path.cpp
)

if(APPLE)
    list(APPEND B2_SHARED_SRCS
        ${B2_SHARED_C}/system_osx.cpp
        ${B2_SHARED_C}/system_posix.cpp
        ${B2_SHARED_C}/path_osx.cpp
    )
elseif(UNIX)
    list(APPEND B2_SHARED_SRCS
        ${B2_SHARED_C}/system_linux.cpp
        ${B2_SHARED_C}/system_posix.cpp
        ${B2_SHARED_C}/path_posix.cpp
        ${B2_SHARED_C}/path_linux.cpp
    )
elseif(WIN32)
    list(APPEND B2_SHARED_SRCS
        ${B2_SHARED_C}/system_windows.cpp
        ${B2_SHARED_C}/path_windows.cpp
    )
endif()

add_library(b2_shared_lib STATIC ${B2_SHARED_SRCS})

target_include_directories(b2_shared_lib PUBLIC ${B2_SHARED_H})
target_include_directories(b2_shared_lib PRIVATE ${B2_SHARED_H})

set_target_properties(b2_shared_lib PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

if(B2_HAVE_STRLCPY)
    target_compile_definitions(b2_shared_lib PUBLIC SYSTEM_HAVE_STRLCPY=1)
endif()

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(b2_shared_lib INTERFACE ${CMAKE_THREAD_LIBS_INIT})
    if(NOT APPLE)
        target_link_libraries(b2_shared_lib INTERFACE rt uuid)
    endif()
endif()

if(WIN32)
    target_link_libraries(b2_shared_lib INTERFACE rpcrt4.lib)
endif()

# Suppress warnings in third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(b2_shared_lib PRIVATE -w)
endif()

# --- 6502 code generator ---

add_executable(6502_gen
    src/6502/6502_gen.cpp
    src/6502/6502_gen.inl
)

target_link_libraries(6502_gen b2_shared_lib)

set_target_properties(6502_gen PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Suppress warnings in third-party code
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(6502_gen PRIVATE -w)
endif()
