# Piconet USB-CDC Econet transport extension.
#
# beebium_piconet_core and the plugin both build on every platform:
# POSIX picks up PosixSerialPort.cpp, Windows picks up Win32SerialPort.cpp.
if(WIN32)
    set(PICONET_SERIAL_SRCS src/Win32SerialPort.cpp)
else()
    set(PICONET_SERIAL_SRCS src/PosixSerialPort.cpp)
endif()

# Core protocol library: Piconet USB-CDC protocol parsing/formatting,
# the PiconetBackend, and the platform serial transport. PUBLIC headers
# under include/beebium/econet/{,piconet/} so test code that wants
# direct access to PiconetBackend etc. can link against this and use
# the same #include paths it always did.
add_library(beebium_piconet_core STATIC
    src/Base64.cpp
    src/Events.cpp
    src/PiconetBackend.cpp
    ${PICONET_SERIAL_SRCS}
    src/TxResult.cpp
)

target_include_directories(beebium_piconet_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(beebium_piconet_core
    PUBLIC beebium_core
)

set_target_properties(beebium_piconet_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# piconet plugin sources. The plugin DLL contains only the
# EconetTransportExtension implementation, the UI, and the platform
# serial wiring -- it deliberately does NOT link gRPC.
#
# Why: vcpkg builds gRPC as a static library on Windows. When two
# modules in the same process each link gRPC::grpc++ statically, each
# gets its own private copy of gRPC's internal state (thread-local
# ExecCtx, closure lists, iomgr). When the EXE-side gRPC server
# dispatches a server-streaming RPC handler that crosses into the
# plugin DLL (e.g. a gRPC worker thread invokes
# PiconetUi::handle_event), heap corruption manifests as a 32-bit
# zeroing of nearby PiconetBackend members -- specifically the
# serial_factory_ std::function's stored fn-ptr -- yielding an AV at
# the next reopen call. See docs/discussion/grpc-windows-streaming-race.md.
#
# The PiconetService gRPC stub used to live here; it was extracted
# (TODO: a beebium_ext_piconet_service STATIC linked into the EXE
# would restore the GetStatus RPC). The macOS frontend's
# device-path-editor flow only needs ExtensionUiService, which is
# unaffected by piconet.dll's gRPC absence.
set(PICONET_PLUGIN_SOURCES
    PiconetEconetTransportExtension.cpp
    PiconetUi.cpp
    plugin_entry.cpp
)

# The shared-library plugin: links the protocol core and the
# EconetTransportExtension wrapper, exports beebium_create_extension.
add_library(beebium_ext_piconet_plugin SHARED ${PICONET_PLUGIN_SOURCES})

target_include_directories(beebium_ext_piconet_plugin
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_piconet_plugin
    PRIVATE
        beebium_piconet_core
        beebium_extension_api
)

set_target_properties(beebium_ext_piconet_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "piconet"
    # On Windows, test binaries (test_grpc_piconet_service, test_piconet_ui)
    # link against this plugin DLL and directly construct
    # PiconetEconetTransportExtension. That class isn't annotated with
    # __declspec(dllexport), so MSVC normally hides it from the import lib.
    # Export all function symbols to give the tests access without having
    # to sprinkle annotations through the plugin sources. No effect on POSIX.
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)

beebium_finalize_plugin(TARGET beebium_ext_piconet_plugin NAME piconet)
