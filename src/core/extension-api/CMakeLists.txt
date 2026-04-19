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
    ExtensionUi.cpp
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
#
# Test executables live in build/tests/$<CONFIG>/ and also link
# beebium_core (which PUBLIC-links beebium_extension_api), so they
# need a sibling copy of the DLL to load at runtime. A post-build
# custom command copies the DLL into the tests directory so ctest
# (including catch_discover_tests, which invokes each test exe to
# enumerate cases) can load them. This matches the existing
# copy-alongside-server arrangement above for deployment targets.
if(WIN32)
    set_target_properties(beebium_extension_api PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/server"
    )
    add_custom_command(TARGET beebium_extension_api POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${CMAKE_BINARY_DIR}/tests/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:beebium_extension_api>
            "${CMAKE_BINARY_DIR}/tests/$<CONFIG>/"
        COMMENT "Copying beebium_extension_api.dll to tests/$<CONFIG> for runtime loading"
    )
endif()
