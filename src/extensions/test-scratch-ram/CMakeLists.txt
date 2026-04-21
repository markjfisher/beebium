# TestScratchRam extension - 8 bytes of scratch RAM at 0xFC50 for framework testing

# Compile proto -- use an OBJECT library so the generated sources are
# built exactly once, avoiding MSBuild parallel-build races on Windows.
beebium_compile_proto(
    TARGET beebium_ext_test_scratch_ram
    PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/scratch_ram.proto
    PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
)

add_library(beebium_ext_test_scratch_ram_proto OBJECT
    ${beebium_ext_test_scratch_ram_PROTO_SRCS}
    ${beebium_ext_test_scratch_ram_GRPC_SRCS}
)

target_include_directories(beebium_ext_test_scratch_ram_proto
    PUBLIC ${beebium_ext_test_scratch_ram_OUT_DIR}
)

target_link_libraries(beebium_ext_test_scratch_ram_proto
    PUBLIC
        gRPC::grpc++
        protobuf::libprotobuf
)

set_target_properties(beebium_ext_test_scratch_ram_proto PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Static library (built-in extension)
add_library(beebium_ext_test_scratch_ram STATIC
    TestScratchRam.cpp
)

target_include_directories(beebium_ext_test_scratch_ram
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_test_scratch_ram
    PUBLIC
        beebium_core
        beebium_ext_test_scratch_ram_proto
)

# Shared library (plugin extension) - built alongside the static library
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_test_scratch_ram_plugin SHARED
        TestScratchRam.cpp
        plugin_entry.cpp
    )

    target_include_directories(beebium_ext_test_scratch_ram_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_ext_test_scratch_ram_plugin
        PRIVATE
            beebium_extension_api
            beebium_ext_test_scratch_ram_proto
    )

    set_target_properties(beebium_ext_test_scratch_ram_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "test-scratch-ram"
    )

    beebium_finalize_plugin(TARGET beebium_ext_test_scratch_ram_plugin NAME test-scratch-ram)
endif()
