# beebium server executables - one per machine type

# Common include directory for server templates
set(SERVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Presets output directory (alongside executables). On single-config
# generators this equals $<TARGET_FILE_DIR:...> for the server executables; it
# is used below by the install() rules, which cannot take generator
# expressions.
set(PRESETS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/presets")
file(MAKE_DIRECTORY ${PRESETS_OUTPUT_DIR})

# Generate a system preset and its thumbnail at build time, using the same
# create-preset code path that users create their own presets with. Extra
# create-preset arguments (e.g. --fdc, --sideways) are forwarded via ARGN.
#
# Presets are written into a presets/ directory next to the actual executable
# (via $<TARGET_FILE_DIR:...>) so the server's own exe-relative discovery finds
# them. Multi-config generators (e.g. Visual Studio) nest the executable in a
# per-config subdirectory, so a fixed configure-time path would not sit beside
# the binary.
function(add_system_preset EXECUTABLE PRESET_ID PRESET_NAME RELEASE_DATE)
    set(preset_dirpath "$<TARGET_FILE_DIR:${EXECUTABLE}>/presets")
    set(preset_filepath "${preset_dirpath}/${PRESET_ID}.preset.beebium")
    set(thumbnail_filepath "${preset_dirpath}/${PRESET_ID}.thumbnail.png")
    add_custom_command(TARGET ${EXECUTABLE} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${preset_dirpath}"
        COMMAND ${EXECUTABLE} create-preset
            --name "${PRESET_NAME}"
            --release-date "${RELEASE_DATE}"
            ${ARGN}
            --output "${preset_filepath}"
        COMMAND ${EXECUTABLE} capture-screenshot
            --preset "${preset_filepath}"
            --output "${thumbnail_filepath}"
            --rom-dir "${BEEBIUM_ROM_DIR}"
        COMMENT "Generating preset ${PRESET_ID} and its thumbnail"
        VERBATIM
    )
endfunction()

# Function to create a beebium server executable for a specific machine type
function(add_beebium_server TARGET_NAME SOURCE_FILE)
    add_executable(${TARGET_NAME} ${SOURCE_FILE})

    target_include_directories(${TARGET_NAME}
        PRIVATE
            ${SERVER_INCLUDE_DIR}
    )

    target_compile_definitions(${TARGET_NAME}
        PRIVATE
            BEEBIUM_VERSION="${PROJECT_VERSION}"
    )

    # Most Beebium extensions ship as plugins, deployed via
    # beebium_finalize_plugin() to <exe-dir>/extensions/<name>/, and
    # loaded at runtime through PluginLoader. Plugin targets are
    # aggregated into beebium-servers (the convenience target below)
    # so a single build invocation produces a complete extension tree.
    #
    # The two built-in exceptions (acorn-65c02-coprocessor and aun)
    # are statically linked. See BuiltinExtensions.hpp for their
    # rationale and what would be required to convert each to a plugin.
    target_link_libraries(${TARGET_NAME}
        PRIVATE
            beebium_service
            beebium_ext_acorn_65c02
            beebium_ext_aun
            beebium_ext_host_serial
            beebium_ext_loopback_serial
            beebium_ext_rpc_serial
            beebium_ext_ip232_serial
            nlohmann_json::nlohmann_json
            stb_image_write
    )
endfunction()

# Model-specific executables
add_beebium_server(beebium-model-b main_model_b.cpp)
add_beebium_server(beebium-model-b-plus main_model_b_plus.cpp)
add_beebium_server(beebium-model-b-plus-128k main_model_b_plus_128k.cpp)
add_beebium_server(beebium-model-b-romram main_model_b_romram.cpp)

# System presets are generated below (after the ROM copy) via add_system_preset,
# which runs the create-preset subcommand as a POST_BUILD step.

# Convenience target to build all server executables and their bundled
# extension plugins. Plugins are loaded at runtime from
# <exe-dir>/extensions/<name>/ via beebium_finalize_plugin() post-build
# deployment, not linked, so we list them here to ensure a complete
# build in one invocation.
set(BEEBIUM_SERVERS_DEPS
    beebium-model-b
    beebium-model-b-plus
    beebium-model-b-plus-128k
    beebium-model-b-romram
)
if(BEEBIUM_BUILD_SERVICE)
    list(APPEND BEEBIUM_SERVERS_DEPS
        beebium_ext_acorn_rtc_plugin
        beebium_ext_acorn_scsi_plugin
        beebium_ext_piconet_plugin
        beebium_ext_scsi_hard_disc_plugin
        beebium_ext_test_scratch_ram_plugin
    )
endif()
add_custom_target(beebium-servers DEPENDS ${BEEBIUM_SERVERS_DEPS})

# Copy ROMs to build output directory for development
set(ROM_OUTPUT_DIR "${CMAKE_BINARY_DIR}/roms")
file(MAKE_DIRECTORY ${ROM_OUTPUT_DIR})

add_custom_command(TARGET beebium-model-b POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${BEEBIUM_ROM_DIR}/acorn-mos_1_20.rom"
        "${BEEBIUM_ROM_DIR}/acorn-mos_2_0.rom"
        "${BEEBIUM_ROM_DIR}/bbc-basic_2.rom"
        "${BEEBIUM_ROM_DIR}/acorn-dfs_2_26.rom"
        "${BEEBIUM_ROM_DIR}/acorn-tube-6502_1_10.rom"
        "${ROM_OUTPUT_DIR}/"
    COMMENT "Copying ROMs to build directory"
)

# Generate the shipped system presets (and their thumbnails). Model B serves
# two presets: a bare cassette machine and a disc-equipped variant. A filing
# system is provisioned explicitly via --sideways; no machine auto-loads DFS
# except the Model B+, whose disc controller and DFS are integral hardware.
add_system_preset(beebium-model-b "model-b" "BBC Model B" "1981-12")
add_system_preset(beebium-model-b "model-b-disc" "BBC Model B (Disc)" "1982"
    --fdc acorn-1770 --sideways 14:rom:acorn-dfs_2_26.rom)
add_system_preset(beebium-model-b-romram "model-b-romram-disc"
    "BBC Model B with ROM/RAM Board (Disc)" "1984"
    --fdc acorn-1770 --sideways 14:rom:acorn-dfs_2_26.rom)
add_system_preset(beebium-model-b-plus "model-b-plus" "BBC Model B+ 64K" "1985-06")
add_system_preset(beebium-model-b-plus-128k "model-b-plus-128k"
    "BBC Model B+ 128K" "1985-06")

# Install rules
install(TARGETS
        beebium-model-b
        beebium-model-b-plus
        beebium-model-b-plus-128k
        beebium-model-b-romram
    RUNTIME DESTINATION bin
)
install(DIRECTORY ${BEEBIUM_ROM_DIR}/
    DESTINATION share/beebium/roms
    FILES_MATCHING PATTERN "*.rom"
)
install(DIRECTORY ${PRESETS_OUTPUT_DIR}/
    DESTINATION share/beebium/presets
    FILES_MATCHING
        PATTERN "*.preset.beebium"
        PATTERN "*.thumbnail.png"
)
