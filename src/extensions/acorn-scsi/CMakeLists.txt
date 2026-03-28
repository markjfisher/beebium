# Acorn SCSI Host Adapter extension

# Core SCSI bus protocol library (no gRPC dependency)
add_library(beebium_scsi_core STATIC
    ScsiBus.cpp
    ScsiTestDevice.cpp
    ScsiTargetRegistry.cpp
    HardDiskImage.cpp
    ScsiHardDisc.cpp
)

target_include_directories(beebium_scsi_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_scsi_core
    PUBLIC beebium_core
)

# Full extension with gRPC service (depends on service layer)
if(BEEBIUM_BUILD_SERVICE)
    # Compile proto
    beebium_compile_proto(
        TARGET beebium_ext_acorn_scsi
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/scsi_host_adapter.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # Static library (built-in extension)
    add_library(beebium_ext_acorn_scsi STATIC
        AcornScsiHostAdapter.cpp
        ${beebium_ext_acorn_scsi_PROTO_SRCS}
        ${beebium_ext_acorn_scsi_GRPC_SRCS}
    )

    target_include_directories(beebium_ext_acorn_scsi
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${beebium_ext_acorn_scsi_OUT_DIR}
    )

    target_link_libraries(beebium_ext_acorn_scsi
        PUBLIC
            beebium_scsi_core
            gRPC::grpc++
            protobuf::libprotobuf
    )

    # Shared library (plugin extension)
    add_library(beebium_plugin_acorn_scsi SHARED
        AcornScsiHostAdapter.cpp
        plugin_entry.cpp
        ${beebium_ext_acorn_scsi_PROTO_SRCS}
        ${beebium_ext_acorn_scsi_GRPC_SRCS}
    )

    target_include_directories(beebium_plugin_acorn_scsi
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${beebium_ext_acorn_scsi_OUT_DIR}
    )

    target_link_libraries(beebium_plugin_acorn_scsi
        PRIVATE
            beebium_scsi_core
            gRPC::grpc++
            protobuf::libprotobuf
    )

    set_target_properties(beebium_plugin_acorn_scsi PROPERTIES
        PREFIX ""
        OUTPUT_NAME "acorn-scsi"
    )

    add_custom_command(TARGET beebium_plugin_acorn_scsi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
            $<TARGET_FILE_DIR:beebium_plugin_acorn_scsi>/manifest.json
        COMMENT "Copying manifest.json for acorn-scsi plugin"
    )
endif()
