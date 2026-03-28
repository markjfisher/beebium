# Acorn SCSI Host Adapter extension

# Core SCSI bus protocol library (no gRPC dependency)
add_library(beebium_scsi_core STATIC
    ScsiBus.cpp
    ScsiTestDevice.cpp
    ScsiTargetRegistry.cpp
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
endif()
