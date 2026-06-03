# rpc-serial (built-in extension): a client-driven serial peer where the RPC
# client is the device on the far end of the wire. Builds as a STATIC library
# linked directly into the server. Built-in for the same CLI-test ergonomics
# reason as the other serial extensions (see BuiltinExtensions.hpp).
#
# When the service layer is being built, compiles rpc_serial.proto and links the
# gRPC runtime; the RpcSerial service (RpcSerialService.hpp, header-only) is the
# typed-RPC home for Send/Receive. Without the service layer the extension still
# attaches its endpoint (but there is no RPC to drive it).

set(RPC_SERIAL_SOURCES
    RpcSerialExtension.cpp
    RpcSerialUi.cpp
)

if(BEEBIUM_BUILD_SERVICE)
    beebium_compile_proto(
        TARGET beebium_ext_rpc_serial
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/rpc_serial.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )
    list(APPEND RPC_SERIAL_SOURCES
        ${beebium_ext_rpc_serial_PROTO_SRCS}
        ${beebium_ext_rpc_serial_GRPC_SRCS}
    )
endif()

add_library(beebium_ext_rpc_serial STATIC ${RPC_SERIAL_SOURCES})

target_include_directories(beebium_ext_rpc_serial
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_rpc_serial
    PUBLIC
        beebium_core
        # RpcSerialUi.cpp pulls in extension_ui.pb.h for the
        # View / DispatchRequest types it composes.
        beebium_extension_ui_proto
)

if(BEEBIUM_BUILD_SERVICE)
    target_include_directories(beebium_ext_rpc_serial
        PUBLIC ${beebium_ext_rpc_serial_OUT_DIR}
    )
    target_compile_definitions(beebium_ext_rpc_serial PUBLIC BEEBIUM_BUILD_SERVICE)
    target_link_libraries(beebium_ext_rpc_serial
        PUBLIC
            gRPC::grpc++
            protobuf::libprotobuf
    )
endif()
