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
# BuiltinExtensions.hpp. No proto/gRPC service yet: serial status is served by
# the core SerialService; a typed RPC for this extension can be added later.

add_library(beebium_ext_host_serial STATIC
    HostSerialExtension.cpp
    HostSerialUi.cpp
)

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
