# beebium server executables - one per machine type

# Common include directory for server templates
set(SERVER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Presets output directory (alongside executables)
set(PRESETS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/presets")
file(MAKE_DIRECTORY ${PRESETS_OUTPUT_DIR})

# Function to generate a bare preset file for a model
function(generate_bare_preset MODEL_ID RELEASE_DATE)
    set(PRESET_FILEPATH "${PRESETS_OUTPUT_DIR}/${MODEL_ID}.preset.beebium")
    if(RELEASE_DATE)
        file(WRITE "${PRESET_FILEPATH}" "{\n  \"model\": \"${MODEL_ID}\",\n  \"release_date\": \"${RELEASE_DATE}\"\n}\n")
    else()
        file(WRITE "${PRESET_FILEPATH}" "{\n  \"model\": \"${MODEL_ID}\"\n}\n")
    endif()
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

    target_link_libraries(${TARGET_NAME}
        PRIVATE
            beebium_service
            beebium_ext_test_scratch_ram
            beebium_ext_acorn_65c02
            nlohmann_json::nlohmann_json
            stb_image_write
    )
endfunction()

# Model-specific executables
add_beebium_server(beebium-model-b main_model_b.cpp)
add_beebium_server(beebium-model-b-plus main_model_b_plus.cpp)
add_beebium_server(beebium-model-b-romram main_model_b_romram.cpp)

# Generate bare preset files for each model (with historical release dates)
# TODO: Replace with post-build commands once create-preset subcommand is implemented:
#   add_custom_command(TARGET beebium-model-b POST_BUILD
#       COMMAND beebium-model-b create-preset --name "BBC Model B" --output "${PRESETS_OUTPUT_DIR}/model-b.preset.beebium"
#   )
# This ensures presets are created using the same code path as user presets.
generate_bare_preset("model-b" "1981-12")
generate_bare_preset("model-b-plus" "1985-06")
generate_bare_preset("model-b-romram" "1984")

# Convenience target to build all server executables (add new models here)
add_custom_target(beebium-servers DEPENDS
    beebium-model-b
    beebium-model-b-plus
    beebium-model-b-romram
)

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

# Generate preset thumbnails via capture-screenshot subcommand
add_custom_command(TARGET beebium-model-b POST_BUILD
    COMMAND beebium-model-b capture-screenshot
        --preset "${PRESETS_OUTPUT_DIR}/model-b.preset.beebium"
        --output "${PRESETS_OUTPUT_DIR}/model-b.thumbnail.png"
        --rom-dir "${BEEBIUM_ROM_DIR}"
    COMMENT "Generating thumbnail for model-b preset"
)
add_custom_command(TARGET beebium-model-b-plus POST_BUILD
    COMMAND beebium-model-b-plus capture-screenshot
        --preset "${PRESETS_OUTPUT_DIR}/model-b-plus.preset.beebium"
        --output "${PRESETS_OUTPUT_DIR}/model-b-plus.thumbnail.png"
        --rom-dir "${BEEBIUM_ROM_DIR}"
    COMMENT "Generating thumbnail for model-b-plus preset"
)
add_custom_command(TARGET beebium-model-b-romram POST_BUILD
    COMMAND beebium-model-b-romram capture-screenshot
        --preset "${PRESETS_OUTPUT_DIR}/model-b-romram.preset.beebium"
        --output "${PRESETS_OUTPUT_DIR}/model-b-romram.thumbnail.png"
        --rom-dir "${BEEBIUM_ROM_DIR}"
    COMMENT "Generating thumbnail for model-b-romram preset"
)

# Install rules
install(TARGETS beebium-model-b beebium-model-b-plus beebium-model-b-romram
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
