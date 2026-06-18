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

# Plugin builds link against the extension API shared library only.
# Built-in (static) builds link against beebium_core via their own targets.
target_link_libraries(beebium_scsi_core
    PUBLIC beebium_extension_api
)

# Full extension with gRPC service (depends on service layer)
if(BEEBIUM_BUILD_SERVICE)
    # Compile proto -- use an OBJECT library so the generated sources are
    # built exactly once, avoiding MSBuild parallel-build races on Windows.
    beebium_compile_proto(
        TARGET beebium_ext_acorn_scsi
        PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/scsi_host_adapter.proto
        PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_library(beebium_ext_acorn_scsi_proto OBJECT
        ${beebium_ext_acorn_scsi_PROTO_SRCS}
    )

    target_include_directories(beebium_ext_acorn_scsi_proto
        PUBLIC ${beebium_ext_acorn_scsi_OUT_DIR}
    )

    # Messages only (no .grpc.pb.cc, no macOS Option 2 headers-only linkage):
    # that is what pulled gRPC in. The SCSI host-adapter API is served over the
    # core's ExtensionRpc channel. See docs/discussion/extension-rpc-channel.md.
    target_link_libraries(beebium_ext_acorn_scsi_proto
        PUBLIC protobuf::libprotobuf
    )

    set_target_properties(beebium_ext_acorn_scsi_proto PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )

    # Static library (built-in extension)
    add_library(beebium_ext_acorn_scsi STATIC
        AcornScsiHostAdapter.cpp
    )

    target_include_directories(beebium_ext_acorn_scsi
        PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_ext_acorn_scsi
        PUBLIC
            beebium_scsi_core
            beebium_core
            beebium_ext_acorn_scsi_proto
    )

    # Shared library (plugin extension)
    add_library(beebium_ext_acorn_scsi_plugin SHARED
        AcornScsiHostAdapter.cpp
        plugin_entry.cpp
    )

    target_include_directories(beebium_ext_acorn_scsi_plugin
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_ext_acorn_scsi_plugin
        PRIVATE
            beebium_scsi_core
            beebium_ext_acorn_scsi_proto
    )

    set_target_properties(beebium_ext_acorn_scsi_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "acorn-scsi"
    )

    beebium_finalize_plugin(TARGET beebium_ext_acorn_scsi_plugin NAME acorn-scsi)
endif()
