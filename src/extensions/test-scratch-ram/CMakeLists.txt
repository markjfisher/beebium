# TestScratchRam extension - 8 bytes of scratch RAM at 0xFC50 for framework testing

# Compile proto
beebium_compile_proto(
    TARGET beebium_ext_test_scratch_ram
    PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/scratch_ram.proto
    PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
)

# Static library (built-in extension)
add_library(beebium_ext_test_scratch_ram STATIC
    TestScratchRam.cpp
    ${beebium_ext_test_scratch_ram_PROTO_SRCS}
    ${beebium_ext_test_scratch_ram_GRPC_SRCS}
)

target_include_directories(beebium_ext_test_scratch_ram
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${beebium_ext_test_scratch_ram_OUT_DIR}
)

target_link_libraries(beebium_ext_test_scratch_ram
    PUBLIC
        beebium_core
        gRPC::grpc++
        protobuf::libprotobuf
)
