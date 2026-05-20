# SCSI Hard Disc extension -- attaches a DAT+DSC disc image to a SCSI adapter

# Static library (for tests and potential built-in use on Master AIV)
add_library(beebium_ext_scsi_hard_disc STATIC
    ScsiHardDiscExtension.cpp
    ScsiHardDiscUi.cpp
)

target_include_directories(beebium_ext_scsi_hard_disc
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_scsi_hard_disc
    PUBLIC
        beebium_scsi_core
        beebium_ext_acorn_scsi
        # ScsiHardDiscUi.cpp pulls in extension_ui.pb.h for the
        # View / DispatchRequest types it composes.
        beebium_extension_ui_proto
)

# Shared library (plugin)
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_ext_scsi_hard_disc_plugin SHARED
        ScsiHardDiscExtension.cpp
        ScsiHardDiscUi.cpp
        plugin_entry.cpp
    )

    target_include_directories(beebium_ext_scsi_hard_disc_plugin
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            # Need AcornScsiHostAdapter.hpp for dynamic_cast, but must NOT link
            # the acorn-scsi library (it contains proto descriptors that would
            # conflict with the already-loaded acorn-scsi plugin)
            ${CMAKE_SOURCE_DIR}/src/extensions/acorn-scsi
    )

    target_link_libraries(beebium_ext_scsi_hard_disc_plugin
        PRIVATE
            beebium_scsi_core
            # ScsiHardDiscUi.cpp pulls in extension_ui.pb.h for the
            # View / DispatchRequest types it composes.
            beebium_extension_ui_proto
    )

    set_target_properties(beebium_ext_scsi_hard_disc_plugin PROPERTIES
        PREFIX ""
        OUTPUT_NAME "scsi-hard-disc"
    )

    beebium_finalize_plugin(TARGET beebium_ext_scsi_hard_disc_plugin NAME scsi-hard-disc)
endif()
