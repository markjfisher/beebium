# Extension API shared library
#
# The formal ABI surface that extension plugins link against. Contains the
# extension framework types (PeripheralExtension, ExtensionContext, ports)
# and the plugin loader. Plugins resolve these symbols from this shared
# library at load time, ensuring exactly one copy exists at runtime.
#
# The bulk of beebium_core (6502, VIA, CRTC, video, audio, disc, etc.)
# remains static in the main executable and is NOT part of this library.

add_library(beebium_extension_api SHARED
    ExtensionContext.cpp
    OneMHzBusPort.cpp
    EconetTransportExtension.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/UserPort.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/PluginLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ExtensionArgParser.cpp
)

target_include_directories(beebium_extension_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# nlohmann_json is needed by PluginLoader for manifest parsing
target_link_libraries(beebium_extension_api
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE ${CMAKE_DL_LIBS}
)

set_target_properties(beebium_extension_api PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    MACOSX_RPATH ON
    # On Linux/macOS, place the shared object in build/lib/. The server
    # executables have an RPATH (or @rpath on macOS) pointing here.
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Windows has no RPATH mechanism: DLLs are searched only in the directory
# of the loading executable (and on PATH/system dirs). Override
# RUNTIME_OUTPUT_DIRECTORY to place beebium_extension_api.dll directly
# alongside the server executables so they can load it without manual
# PATH setup or a deployment copy step.
if(WIN32)
    set_target_properties(beebium_extension_api PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/server"
    )
endif()
