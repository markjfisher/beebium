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

# Shared library (plugin extension) - built alongside the static library
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_test_scratch_ram_plugin SHARED
        TestScratchRam.cpp
        plugin_entry.cpp
        ${beebium_ext_test_scratch_ram_PROTO_SRCS}
        ${beebium_ext_test_scratch_ram_GRPC_SRCS}
    )

    target_include_directories(beebium_ext_test_scratch_ram_plugin
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${beebium_ext_test_scratch_ram_OUT_DIR}
    )

    target_link_libraries(beebium_ext_test_scratch_ram_plugin
        PRIVATE
            beebium_core
            gRPC::grpc++
            protobuf::libprotobuf
    )

    set_target_properties(beebium_ext_test_scratch_ram_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "test-scratch-ram"
    )

    # Copy manifest.json alongside the shared library
    add_custom_command(TARGET beebium_ext_test_scratch_ram_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
            $<TARGET_FILE_DIR:beebium_ext_test_scratch_ram_plugin>/manifest.json
        COMMENT "Copying manifest.json for test-scratch-ram plugin"
    )
endif()
