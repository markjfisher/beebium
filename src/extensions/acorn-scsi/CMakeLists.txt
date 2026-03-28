# Acorn SCSI Host Adapter extension

# Phase 1: Core SCSI bus protocol library (no gRPC dependency)
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
