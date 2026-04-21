# Acorn 65C02 3 MHz second processor extension (Tube co-processor).
#
# Built-in-only. The server has link-time knowledge of this extension:
# ServerMain iterates the extension registry and does
# dynamic_cast<SecondProcessor65C02Extension*> to wire Tube-specific
# cross-processor debugger callbacks (breakpoint-stops-counterpart). The
# class's integration methods (running(), parasite_pause_callback(),
# wire_counterpart_stop()) are inline non-virtual and reach into
# private members, and the extension PUBLIC-links beebium_service for
# the DebuggerService interface -- both would need a virtual-interface
# refactor on the extension API before 65C02 could load as a plugin
# without the server pulling in duplicate proto descriptors or losing
# type access.
#
# See BuiltinExtensions.hpp.

add_library(beebium_ext_acorn_65c02 STATIC
    SecondProcessor65C02Extension.cpp
)

target_include_directories(beebium_ext_acorn_65c02
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/src/server/include
)

target_link_libraries(beebium_ext_acorn_65c02
    PUBLIC beebium_core
)

if(BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_acorn_65c02
        PUBLIC beebium_service
    )
endif()

# Copy extension ROMs to the build output so the extension can find them
# at runtime relative to the server executable.
file(GLOB EXTENSION_ROMS "${CMAKE_CURRENT_SOURCE_DIR}/roms/*.rom")
foreach(ROM_FILE ${EXTENSION_ROMS})
    get_filename_component(ROM_NAME ${ROM_FILE} NAME)
    configure_file(${ROM_FILE}
        ${CMAKE_BINARY_DIR}/roms/${ROM_NAME} COPYONLY)
endforeach()
