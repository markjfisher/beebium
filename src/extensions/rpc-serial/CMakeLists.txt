# rpc-serial: a client-driven serial peer where the RPC client is the device on
# the far end of the wire. Ships as a dynamically-loaded plugin, discovered from
# <exe-dir>/extensions/.
#
# The RpcSerial API is served through the core's ExtensionRpc channel: the
# plugin registers a hand-written RpcSerialDispatcher (see RpcSerialDispatcher.hpp)
# that (de)serializes the rpc_serial.proto MESSAGES and drives the endpoint. The
# plugin therefore links protobuf (for those messages) but NOT gRPC -- the one
# gRPC runtime stays in the core, so there is no exe<->plugin boundary for gRPC
# objects to cross (gRPC #39198 / the macOS duplicate-runtime crash cannot
# happen). See docs/discussion/extension-rpc-channel.md.

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_rpc_serial
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/rpc_serial.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Messages only: the dispatcher (not a gRPC service) (de)serializes these.
    # The generated .grpc.pb.cc stub is deliberately NOT compiled -- compiling
    # it is what would pull gRPC into the plugin.
    add_library(beebium_ext_rpc_serial_proto OBJECT
        ${beebium_ext_rpc_serial_PROTO_SRCS}
    )
    target_include_directories(beebium_ext_rpc_serial_proto
        PUBLIC ${beebium_ext_rpc_serial_OUT_DIR}
    )
    # protobuf only, no gRPC. Linked normally on every platform (the plugin is
    # self-contained: its protobuf is used only to (de)serialize its own
    # messages, which never cross the module boundary as protobuf objects).
    target_link_libraries(beebium_ext_rpc_serial_proto
        PUBLIC protobuf::libprotobuf
    )
    set_target_properties(beebium_ext_rpc_serial_proto PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Static library -- linked by the unit test, which drives the dispatcher and
# endpoint directly.
add_library(beebium_ext_rpc_serial STATIC
    RpcSerialExtension.cpp
)

target_include_directories(beebium_ext_rpc_serial
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_rpc_serial
    PUBLIC
        beebium_core
)

if(BEEBIUM_BUILD_SERVICE)
    target_compile_definitions(beebium_ext_rpc_serial PUBLIC BEEBIUM_BUILD_SERVICE)
    # The dispatcher ABI (ExtensionRpcDispatcher) lives in beebium_extension_api;
    # the proto messages come via the object lib (which pulls protobuf).
    target_link_libraries(beebium_ext_rpc_serial
        PUBLIC
            beebium_ext_rpc_serial_proto
            beebium_extension_api
    )

    # Shared library (the plugin). Links the extension-API surface + the proto
    # messages. No gRPC. The RpcSerial methods reach clients via the core's
    # ExtensionRpc channel, not a plugin-hosted gRPC service.
    add_library(beebium_ext_rpc_serial_plugin SHARED
        RpcSerialExtension.cpp
        plugin_entry.cpp
    )
    target_include_directories(beebium_ext_rpc_serial_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_compile_definitions(beebium_ext_rpc_serial_plugin PRIVATE BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_rpc_serial_plugin
        PRIVATE
            beebium_extension_api
            beebium_ext_rpc_serial_proto
    )
    set_target_properties(beebium_ext_rpc_serial_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "rpc-serial"
    )
    beebium_finalize_plugin(TARGET beebium_ext_rpc_serial_plugin NAME rpc-serial)
endif()
