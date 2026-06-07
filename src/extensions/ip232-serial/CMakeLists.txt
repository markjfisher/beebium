# IP232 serial bridge: connects the BBC serial port (RS423) to a tcpser-style
# IP232 server over TCP by attaching an Ip232SerialEndpoint (a SerialPortDevice)
# via the SerialPort handle. Ships as a dynamically-loaded plugin (only
# host-serial is essential enough to be a built-in), discovered from
# <exe-dir>/extensions/.
#
# No proto / gRPC service: the endpoint attaches and runs from CLI config. The
# shared TCP transport and socket-platform shim live in beebium_core
# (beebium/net), so this extension just layers the IP232 codec on top -- which
# is why the plugin links beebium_core (for net::TcpClientSerialPort), like the
# piconet plugin does for the serial host-port primitives.

set(IP232_SERIAL_SOURCES
    Ip232SerialEndpoint.cpp
    Ip232SerialExtension.cpp
)

# Static library -- linked by the unit tests, which construct the endpoint /
# extension directly.
add_library(beebium_ext_ip232_serial STATIC ${IP232_SERIAL_SOURCES})

target_include_directories(beebium_ext_ip232_serial
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_ip232_serial
    PUBLIC beebium_core
)

# Shared library (the plugin). Deployed to <exe-dir>/extensions/.
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_ip232_serial_plugin SHARED
        ${IP232_SERIAL_SOURCES}
        plugin_entry.cpp
    )

    target_include_directories(beebium_ext_ip232_serial_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_ext_ip232_serial_plugin
        PRIVATE beebium_core
    )

    set_target_properties(beebium_ext_ip232_serial_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "ip232-serial"
    )

    beebium_finalize_plugin(TARGET beebium_ext_ip232_serial_plugin NAME ip232-serial)
endif()
