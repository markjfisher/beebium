# Extension API shared library
#
# The formal ABI surface that extension plugins link against. Contains the
# extension framework types (PeripheralExtension, ExtensionContext, ports)
# and the plugin loader. Plugins resolve these symbols from this shared
# library at load time, ensuring exactly one copy exists at runtime.
#
# The bulk of beebium_core (6502, VIA, CRTC, video, audio, disc, etc.)
# remains static in the main executable and is NOT part of this library.

# Generate extension_ui.pb.{cc,h} and extension_ui.grpc.pb.{cc,h} from the
# proto file owned by the extension framework. The schema is part of the
# extension ABI (extensions produce View trees the framework streams to
# clients), so it lives next to the rest of the extension API.
#
# These compile into a *separate* shared library (beebium_extension_ui_proto)
# rather than into beebium_extension_api itself, because beebium_extension_api
# uses CXX_VISIBILITY_PRESET=hidden to control its ABI surface -- which
# would hide the proto symbols from external consumers (plugins, the
# server's ExtensionUiServiceImpl). The dedicated proto library keeps
# default visibility so the proto types are reachable from any consumer
# while still living in exactly one .so in the process.
set(EXT_UI_PROTO_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${EXT_UI_PROTO_OUT_DIR})

set(EXT_UI_PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/proto/extension_ui.proto)
set(EXT_UI_PROTO_SRCS
    ${EXT_UI_PROTO_OUT_DIR}/extension_ui.pb.cc
    ${EXT_UI_PROTO_OUT_DIR}/extension_ui.grpc.pb.cc
)
set(EXT_UI_PROTO_HDRS
    ${EXT_UI_PROTO_OUT_DIR}/extension_ui.pb.h
    ${EXT_UI_PROTO_OUT_DIR}/extension_ui.grpc.pb.h
)

add_custom_command(
    OUTPUT ${EXT_UI_PROTO_SRCS} ${EXT_UI_PROTO_HDRS}
    COMMAND ${PROTOC_EXECUTABLE}
        --cpp_out=${EXT_UI_PROTO_OUT_DIR}
        --grpc_out=${EXT_UI_PROTO_OUT_DIR}
        --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
        -I${CMAKE_CURRENT_SOURCE_DIR}/proto
        ${EXT_UI_PROTO_FILE}
    DEPENDS ${EXT_UI_PROTO_FILE}
    COMMENT "Generating gRPC/Protobuf sources for extension_ui.proto"
)

# SHARED on POSIX, STATIC on Windows. The reason for SHARED at all is
# to keep a single copy of proto descriptors and class definitions in
# the process when an out-of-tree plugin (currently just Piconet on
# POSIX) loaded via dlopen also needs to use these types: two copies
# of the same proto registered into protobuf's global descriptor pool
# can abort. On Windows the Piconet plugin doesn't build (there is no
# Win32SerialPort yet), so no plugins are dlopen'd at runtime and
# there's no second consumer to share with. STATIC on Windows
# sidesteps two MSVC pain points: protobuf-generated code has no
# __declspec(dllexport) annotations (so a SHARED build leaves the
# import .lib empty / absent and beebium_extension_api fails to link
# with LNK1181), and the proto-generated _DefaultTypeInternal data
# globals can't be resolved across DLL boundaries without proper
# dllimport markings on the consumer side.
#
# *** This is a contingent choice. *** It assumes Piconet remains
# Windows-unsupported. When Win32SerialPort lands (and with it a
# Windows piconet plugin), this split must be revisited -- the
# proper Windows-friendly fix is protoc's dllexport_decl option
# combined with a generate_export_header()-style macro. Full
# write-up at docs/discussion/win32-piconet-gap.md.
if(WIN32)
    add_library(beebium_extension_ui_proto STATIC ${EXT_UI_PROTO_SRCS})
else()
    add_library(beebium_extension_ui_proto SHARED ${EXT_UI_PROTO_SRCS})
endif()

target_include_directories(beebium_extension_ui_proto PUBLIC
    ${EXT_UI_PROTO_OUT_DIR}
)

target_link_libraries(beebium_extension_ui_proto
    PUBLIC
        gRPC::grpc++
        protobuf::libprotobuf
)

set_target_properties(beebium_extension_ui_proto PROPERTIES
    MACOSX_RPATH ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    POSITION_INDEPENDENT_CODE ON
)

# Suppress warnings in generated proto/gRPC code.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set_source_files_properties(
        ${EXT_UI_PROTO_SRCS}
        PROPERTIES
        COMPILE_FLAGS "-Wno-unused-parameter -Wno-shadow"
    )
elseif(MSVC)
    set_source_files_properties(
        ${EXT_UI_PROTO_SRCS}
        PROPERTIES
        COMPILE_FLAGS "/wd4100 /wd4127 /wd4244 /wd4267"
    )
endif()

add_library(beebium_extension_api SHARED
    ExtensionContext.cpp
    OneMHzBusPort.cpp
    EconetTransportExtension.cpp
    ExtensionUi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/UserPort.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/PluginLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ExtensionArgParser.cpp
)

target_include_directories(beebium_extension_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# nlohmann_json is needed by PluginLoader for manifest parsing.
# beebium_extension_ui_proto is PUBLIC so plugins (and the server) get
# the View / DispatchRequest headers transitively when they link
# beebium_extension_api.
target_link_libraries(beebium_extension_api
    PUBLIC
        nlohmann_json::nlohmann_json
        beebium_extension_ui_proto
    PRIVATE ${CMAKE_DL_LIBS}
)

set_target_properties(beebium_extension_api PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    MACOSX_RPATH ON
    # On Linux/macOS, place the shared object in build/lib/. The server
    # executables have an RPATH (or @rpath on macOS) pointing here.
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Windows has no RPATH mechanism: DLLs are searched only in the directory
# of the loading executable (and on PATH/system dirs). Override
# RUNTIME_OUTPUT_DIRECTORY to place beebium_extension_api.dll directly
# alongside the server executables so they can load it without manual
# PATH setup or a deployment copy step.
#
# Test executables live in build/tests/$<CONFIG>/ and also link
# beebium_core (which PUBLIC-links beebium_extension_api), so they
# need a sibling copy of the DLL to load at runtime. A post-build
# custom command copies the DLL into the tests directory so ctest
# (including catch_discover_tests, which invokes each test exe to
# enumerate cases) can load them. This matches the existing
# copy-alongside-server arrangement above for deployment targets.
if(WIN32)
    set_target_properties(beebium_extension_api PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/server"
    )
    add_custom_command(TARGET beebium_extension_api POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/tests/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:beebium_extension_api>
            "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/"
        COMMENT "Copying beebium_extension_api.dll to tests/$<CONFIG> for runtime loading"
    )
endif()
