# Piconet USB-CDC Econet transport extension.
#
# beebium_piconet_core builds on every platform: POSIX picks up
# PosixSerialPort.cpp; Windows picks up Win32SerialPort.cpp. The plugin
# half below (beebium_ext_piconet_plugin) is activated on Windows in a
# later commit on this branch, once the proto library's dllexport_decl
# plumbing is in place -- see docs/discussion/win32-piconet-gap.md.
if(WIN32)
    set(PICONET_SERIAL_SRCS src/Win32SerialPort.cpp)
else()
    set(PICONET_SERIAL_SRCS src/PosixSerialPort.cpp)
endif()

# Core protocol library: Piconet USB-CDC protocol parsing/formatting,
# the PiconetBackend, and the platform serial transport. PUBLIC headers
# under include/beebium/econet/{,piconet/} so test code that wants
# direct access to PiconetBackend etc. can link against this and use
# the same #include paths it always did.
add_library(beebium_piconet_core STATIC
    src/Base64.cpp
    src/Events.cpp
    src/PiconetBackend.cpp
    ${PICONET_SERIAL_SRCS}
    src/TxResult.cpp
)

target_include_directories(beebium_piconet_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(beebium_piconet_core
    PUBLIC beebium_core
)

set_target_properties(beebium_piconet_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# The Piconet plugin half (beebium_ext_piconet_plugin) depends on
# beebium_extension_api, which transitively consumes proto types from
# beebium_extension_ui_proto. On Windows that proto library is still
# STATIC (see src/core/extension-api/CMakeLists.txt) and proto-generated
# data globals carry no dllexport annotations, so the plugin DLL cannot
# resolve them. The follow-up commit on this branch adds protoc's
# dllexport_decl plumbing and flips the proto library to unconditional
# SHARED; the early-out below is removed in the commit after that.
if(WIN32)
    message(STATUS "Piconet plugin deferred on Windows pending proto dllexport_decl")
    return()
endif()

# gRPC service. Compiled into the plugin only when the service layer
# is part of the build. The proto file is in the same directory.
set(PICONET_PLUGIN_SOURCES
    PiconetEconetTransportExtension.cpp
    PiconetUi.cpp
    plugin_entry.cpp
)

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_piconet_plugin
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/piconet_service.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )
    list(APPEND PICONET_PLUGIN_SOURCES
        PiconetService.cpp
        ${beebium_ext_piconet_plugin_PROTO_SRCS}
        ${beebium_ext_piconet_plugin_GRPC_SRCS}
    )
endif()

# The shared-library plugin: links the protocol core and the
# EconetTransportExtension wrapper, exports beebium_create_extension.
add_library(beebium_ext_piconet_plugin SHARED ${PICONET_PLUGIN_SOURCES})

target_include_directories(beebium_ext_piconet_plugin
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_piconet_plugin
    PRIVATE
        beebium_piconet_core
        beebium_extension_api
)

if(BEEBIUM_BUILD_SERVICE)
    # PUBLIC for the proto OUT_DIR so test targets that link this
    # shared library can include the generated piconet_service.pb.h
    # / .grpc.pb.h. Without PUBLIC the generated headers are private
    # to the plugin and tests can't compile against the service stubs.
    target_include_directories(beebium_ext_piconet_plugin
        PUBLIC ${beebium_ext_piconet_plugin_OUT_DIR}
    )
    target_compile_definitions(beebium_ext_piconet_plugin PRIVATE BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_piconet_plugin
        PUBLIC
            gRPC::grpc++
            protobuf::libprotobuf
    )
endif()

set_target_properties(beebium_ext_piconet_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "piconet"
)

add_custom_command(TARGET beebium_ext_piconet_plugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
        $<TARGET_FILE_DIR:beebium_ext_piconet_plugin>/manifest.json
    COMMENT "Copying manifest.json for piconet plugin"
)
