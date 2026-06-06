# Serial loopback extension: a TX->RX echo plug on the BBC serial port. A
# self-contained "does my serial path work at all" diagnostic; zero config, no
# proto/gRPC service. Ships as a dynamically-loaded plugin (only host-serial is
# essential enough to be a built-in); discovered from <exe-dir>/extensions/.

# Static library -- linked by the unit test, which constructs the extension
# directly.
add_library(beebium_ext_loopback_serial STATIC
    LoopbackSerialExtension.cpp
)

target_include_directories(beebium_ext_loopback_serial
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_loopback_serial
    PUBLIC
        beebium_core
)

# Shared library (the plugin). All of the serial seam it uses is header-only, so
# it links only the extension-API surface. Deployed to <exe-dir>/extensions/.
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_loopback_serial_plugin SHARED
        LoopbackSerialExtension.cpp
        plugin_entry.cpp
    )

    target_include_directories(beebium_ext_loopback_serial_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_ext_loopback_serial_plugin
        PRIVATE
            beebium_extension_api
    )

    set_target_properties(beebium_ext_loopback_serial_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "loopback-serial"
    )

    beebium_finalize_plugin(TARGET beebium_ext_loopback_serial_plugin NAME loopback-serial)
endif()
