# IP232 serial bridge (built-in extension).
#
# Connects the BBC serial port (RS423) to a tcpser-style IP232 server over TCP
# by attaching an Ip232SerialEndpoint (a SerialPortDevice) via the SerialPort
# handle. Builds as a STATIC library linked directly into the server, for the
# same CLI-test ergonomics reason as the other serial extensions (synthetic
# argv cannot auto-discover plugins). See BuiltinExtensions.hpp.
#
# No proto / gRPC service in v1: the endpoint attaches and runs from CLI config.
# The shared TCP transport and socket platform shim live in beebium_core
# (beebium/net), so this extension just layers the IP232 codec on top.

set(IP232_SERIAL_SOURCES
    Ip232SerialEndpoint.cpp
    Ip232SerialExtension.cpp
)

add_library(beebium_ext_ip232_serial STATIC ${IP232_SERIAL_SOURCES})

target_include_directories(beebium_ext_ip232_serial
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_ip232_serial
    PUBLIC beebium_core
)
