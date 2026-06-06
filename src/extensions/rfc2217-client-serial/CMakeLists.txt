# RFC 2217 serial client: connects the BBC serial port (RS423) to a remote RFC
# 2217 access server. Ships as a dynamically-loaded plugin (optional), discovered
# from <exe-dir>/extensions/. Links beebium_core (for net::TcpClientSerialPort)
# and the shared beebium_ext_rfc2217_common (the Telnet/COM-PORT codec).

set(RFC2217_CLIENT_SOURCES
    Rfc2217ClientEndpoint.cpp
    Rfc2217ClientExtension.cpp
)

# Static library -- linked by the unit tests.
add_library(beebium_ext_rfc2217_client STATIC ${RFC2217_CLIENT_SOURCES})

target_include_directories(beebium_ext_rfc2217_client
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_rfc2217_client
    PUBLIC
        beebium_core
        beebium_ext_rfc2217_common
)

# Shared library (the plugin).
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_rfc2217_client_plugin SHARED
        ${RFC2217_CLIENT_SOURCES}
        plugin_entry.cpp
    )
    target_include_directories(beebium_ext_rfc2217_client_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(beebium_ext_rfc2217_client_plugin
        PRIVATE
            beebium_core
            beebium_ext_rfc2217_common
    )
    set_target_properties(beebium_ext_rfc2217_client_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "rfc2217-client-serial"
    )
    beebium_finalize_plugin(TARGET beebium_ext_rfc2217_client_plugin
                            NAME rfc2217-client-serial)
endif()
