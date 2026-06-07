# RFC 2217 serial server: exposes the emulated BBC serial port (RS423) to RFC
# 2217 clients. Ships as a dynamically-loaded plugin (optional), discovered from
# <exe-dir>/extensions/. Links beebium_core (for net::TcpServerSerialPort) and
# the shared beebium_ext_rfc2217_common (the Telnet/COM-PORT codec).

set(RFC2217_SERVER_SOURCES
    Rfc2217ServerEndpoint.cpp
    Rfc2217ServerExtension.cpp
)

# Static library -- linked by the unit tests.
add_library(beebium_ext_rfc2217_server STATIC ${RFC2217_SERVER_SOURCES})

target_include_directories(beebium_ext_rfc2217_server
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_rfc2217_server
    PUBLIC
        beebium_core
        beebium_ext_rfc2217_common
)

# Shared library (the plugin).
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_rfc2217_server_plugin SHARED
        ${RFC2217_SERVER_SOURCES}
        plugin_entry.cpp
    )
    target_include_directories(beebium_ext_rfc2217_server_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(beebium_ext_rfc2217_server_plugin
        PRIVATE
            beebium_core
            beebium_ext_rfc2217_common
    )
    set_target_properties(beebium_ext_rfc2217_server_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "rfc2217-server-serial"
    )
    beebium_finalize_plugin(TARGET beebium_ext_rfc2217_server_plugin
                            NAME rfc2217-server-serial)
endif()
