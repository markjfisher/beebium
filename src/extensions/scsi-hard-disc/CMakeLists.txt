# SCSI Hard Disc extension -- attaches a DAT+DSC disc image to a SCSI adapter

# Static library (for tests and potential built-in use on Master AIV)
add_library(beebium_ext_scsi_hard_disc STATIC
    ScsiHardDiscExtension.cpp
)

target_include_directories(beebium_ext_scsi_hard_disc
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(beebium_ext_scsi_hard_disc
    PUBLIC
        beebium_scsi_core
        beebium_ext_acorn_scsi
)

# Shared library (plugin)
if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_plugin_scsi_hard_disc SHARED
        ScsiHardDiscExtension.cpp
        plugin_entry.cpp
    )

    target_include_directories(beebium_plugin_scsi_hard_disc
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(beebium_plugin_scsi_hard_disc
        PRIVATE
            beebium_scsi_core
            beebium_ext_acorn_scsi
    )

    set_target_properties(beebium_plugin_scsi_hard_disc PROPERTIES
        PREFIX ""
        OUTPUT_NAME "scsi-hard-disc"
    )

    add_custom_command(TARGET beebium_plugin_scsi_hard_disc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
            $<TARGET_FILE_DIR:beebium_plugin_scsi_hard_disc>/manifest.json
        COMMENT "Copying manifest.json for scsi-hard-disc plugin"
    )
endif()
