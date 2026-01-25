cmake_minimum_required(VERSION 3.16)

project(beebium
    VERSION 0.1.0
    DESCRIPTION "BBC Micro emulator with headless core and IPC architecture"
    LANGUAGES C CXX
)

# C++ Standard (C++20 for concepts, constexpr improvements, std::span)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C Standard (for 6502 library)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror=return-type
    )

    # Additional warnings for C++
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
        $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
    )
elseif(MSVC)
    add_compile_options(/W4)
endif()

# Sanitizers for Debug builds
option(BEEBIUM_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
if(BEEBIUM_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Code coverage (use as guidance, not a target)
option(BEEBIUM_ENABLE_COVERAGE "Enable code coverage reporting" OFF)
if(BEEBIUM_ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
        add_link_options(--coverage)
    endif()
endif()

# ROM directory for development and installation
set(BEEBIUM_ROM_DIR "${CMAKE_SOURCE_DIR}/roms" CACHE PATH "Source ROM directory")

# Testing
option(BEEBIUM_BUILD_TESTS "Build tests" ON)
if(BEEBIUM_BUILD_TESTS)
    enable_testing()

    # Fetch Catch2 (archive download, no git clone)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.5.0.tar.gz
        URL_HASH SHA256=f6d4f8d78a9b59ec72a81d49f58d18eb317372ac07f8d9432710a079e69fd66a
    )
    FetchContent_MakeAvailable(Catch2)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
endif()

# gRPC Service Layer
option(BEEBIUM_BUILD_SERVICE "Build gRPC service layer" ON)
option(BEEBIUM_BUILD_SERVER "Build beebium-server executable" ON)

if(BEEBIUM_BUILD_SERVICE)
    # Find gRPC and Protobuf
    # Try CONFIG mode first (macOS Homebrew), fall back to Module mode (Linux apt)
    find_package(Protobuf CONFIG QUIET)
    if(NOT Protobuf_FOUND)
        find_package(Protobuf REQUIRED)
    endif()

    find_package(gRPC CONFIG QUIET)
    if(NOT gRPC_FOUND)
        # On Linux, gRPC may need to be found via pkg-config
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GRPC REQUIRED grpc++ grpc)

        # Create imported target for gRPC if not already defined
        if(NOT TARGET gRPC::grpc++)
            add_library(gRPC::grpc++ INTERFACE IMPORTED)
            set_target_properties(gRPC::grpc++ PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${GRPC_INCLUDE_DIRS}"
                INTERFACE_LINK_LIBRARIES "${GRPC_LIBRARIES}"
            )
        endif()
    endif()

    # Get the protoc executable
    if(TARGET protobuf::protoc)
        get_target_property(PROTOC_EXECUTABLE protobuf::protoc LOCATION)
    else()
        find_program(PROTOC_EXECUTABLE protoc REQUIRED)
    endif()

    # Get the grpc_cpp_plugin executable
    if(TARGET gRPC::grpc_cpp_plugin)
        get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin LOCATION)
    else()
        find_program(GRPC_CPP_PLUGIN grpc_cpp_plugin REQUIRED)
    endif()
endif()

# Subdirectories
add_subdirectory(src/6502)
add_subdirectory(src/core)

if(BEEBIUM_BUILD_SERVICE)
    add_subdirectory(src/service)
endif()

if(BEEBIUM_BUILD_SERVER)
    add_subdirectory(src/server)
endif()

if(BEEBIUM_BUILD_TESTS)
    add_subdirectory(tests)
endif()
