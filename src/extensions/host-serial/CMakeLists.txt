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
# When the service layer is being built, compiles host_serial.proto and links the
# gRPC runtime; the HostSerial service (HostSerialService.hpp, header-only) is the
# typed config API (GetConfig / SetConfig) parallel to the GUI panel. Without the
# service layer the extension still attaches its endpoint and serves its GUI panel.

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
    list(APPEND HOST_SERIAL_SOURCES
        ${beebium_ext_host_serial_PROTO_SRCS}
        ${beebium_ext_host_serial_GRPC_SRCS}
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
    target_link_libraries(beebium_ext_host_serial
        PUBLIC
            gRPC::grpc++
            protobuf::libprotobuf
    )
endif()
