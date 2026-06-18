# Host serial bridge (built-in extension).
#
# Bridges the BBC serial port (RS423) to a host PTY or serial device by
# attaching a HostSerialEndpoint (a SerialPortDevice) via the SerialPort
# handle. Builds as a STATIC library linked directly into the server.
#
# Built-in (rather than a plugin) for the same CLI-test ergonomics reason as
# AUN: many parse_start_arguments tests use a synthetic argv (argv[0] =
# "beebium") that cannot auto-discover plugins via <exe-dir>/extensions/, so
# --host-serial must be recognised without a plugin directory. See
# BuiltinExtensions.hpp.
#
# When the service layer is being built, compiles the host_serial.proto MESSAGES
# (not the gRPC stub); the typed config API (GetConfig / SetConfig, parallel to
# the GUI panel) is served through the core's ExtensionRpc channel via a
# hand-written HostSerialDispatcher (HostSerialDispatcher.hpp), so this library
# links protobuf but NOT gRPC -- the one gRPC runtime stays in the core. Without
# the service layer the extension still attaches its endpoint and serves its GUI
# panel. See docs/discussion/extension-rpc-channel.md.

set(HOST_SERIAL_SOURCES
    HostSerialExtension.cpp
    HostSerialUi.cpp
)

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_host_serial
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/host_serial.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )
    # Messages only: the dispatcher (not a gRPC service) (de)serializes these.
    # The generated .grpc.pb.cc stub is deliberately NOT compiled -- compiling
    # it is what would pull gRPC in.
    list(APPEND HOST_SERIAL_SOURCES
        ${beebium_ext_host_serial_PROTO_SRCS}
    )
endif()

add_library(beebium_ext_host_serial STATIC ${HOST_SERIAL_SOURCES})

target_include_directories(beebium_ext_host_serial
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_host_serial
    PUBLIC
        beebium_core
        # HostSerialUi.cpp pulls in extension_ui.pb.h for the
        # View / DispatchRequest types it composes.
        beebium_extension_ui_proto
)

if(BEEBIUM_BUILD_SERVICE)
    target_include_directories(beebium_ext_host_serial
        PUBLIC ${beebium_ext_host_serial_OUT_DIR}
    )
    target_compile_definitions(beebium_ext_host_serial PUBLIC BEEBIUM_BUILD_SERVICE)
    # protobuf for the host_serial.proto messages; beebium_extension_api for the
    # ExtensionRpcDispatcher ABI the HostSerialDispatcher implements. No gRPC.
    target_link_libraries(beebium_ext_host_serial
        PUBLIC
            protobuf::libprotobuf
            beebium_extension_api
    )
endif()
