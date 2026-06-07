# rpc-serial: a client-driven serial peer where the RPC client is the device on
# the far end of the wire. Ships as a dynamically-loaded plugin (only host-serial
# is essential enough to be a built-in), discovered from <exe-dir>/extensions/.
#
# When the service layer is built, the RpcSerial gRPC service (Send/Receive) is
# registered through the plugin like acorn-rtc / scsi-hard-disc. The proto is
# compiled once into an OBJECT library shared by the static lib (unit test) and
# the shared plugin, avoiding a double-generation race.

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_rpc_serial
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/rpc_serial.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_library(beebium_ext_rpc_serial_proto OBJECT
        ${beebium_ext_rpc_serial_PROTO_SRCS}
        ${beebium_ext_rpc_serial_GRPC_SRCS}
    )
    target_include_directories(beebium_ext_rpc_serial_proto
        PUBLIC ${beebium_ext_rpc_serial_OUT_DIR}
    )
    target_link_libraries(beebium_ext_rpc_serial_proto
        PUBLIC
            gRPC::grpc++
            protobuf::libprotobuf
    )
    set_target_properties(beebium_ext_rpc_serial_proto PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endif()

# Static library -- linked by the unit test, which drives the RpcSerial service
# and endpoint directly.
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
    target_link_libraries(beebium_ext_rpc_serial PUBLIC beebium_ext_rpc_serial_proto)

    # Shared library (the plugin). RpcSerialExtension/Endpoint are header-only on
    # the serial seam, so the plugin links only the extension-API surface + the
    # proto; the gRPC service is collected via the plugin's grpc_services().
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
