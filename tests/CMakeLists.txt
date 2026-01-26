# Beebium tests

# Test assets directory
set(BEEBIUM_TEST_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

# 6502 tests
add_executable(test_6502 test_6502.cpp)
target_link_libraries(test_6502 PRIVATE 6502_lib Catch2::Catch2WithMain)
target_compile_definitions(test_6502 PRIVATE
    BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
)
catch_discover_tests(test_6502)

# Memory tests
add_executable(test_memory test_memory.cpp)
target_link_libraries(test_memory PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_memory)

# Configurable slot tests
add_executable(test_configurable_slot test_configurable_slot.cpp)
target_link_libraries(test_configurable_slot PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_configurable_slot)

# Aliased banked memory tests (Model B 4-socket ROM configuration)
add_executable(test_aliased_banked_memory test_aliased_banked_memory.cpp)
target_link_libraries(test_aliased_banked_memory PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_aliased_banked_memory)

# Configurable banked memory tests (ROM/RAM board 16-slot configuration)
add_executable(test_configurable_banked_memory test_configurable_banked_memory.cpp)
target_link_libraries(test_configurable_banked_memory PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_configurable_banked_memory)

# Model B ROM/RAM Board hardware tests
add_executable(test_model_b_romram_hardware test_model_b_romram_hardware.cpp)
target_link_libraries(test_model_b_romram_hardware PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_model_b_romram_hardware)

# Disc geometry tests
add_executable(test_disc_geometry test_disc_geometry.cpp)
target_link_libraries(test_disc_geometry PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_geometry)

# Disc image tests
add_executable(test_disc_image test_disc_image.cpp)
target_link_libraries(test_disc_image PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_image)

# Memory disc image tests
add_executable(test_memory_disc_image test_memory_disc_image.cpp)
target_link_libraries(test_memory_disc_image PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_memory_disc_image)

# Disc drive tests
add_executable(test_disc_drive test_disc_drive.cpp)
target_link_libraries(test_disc_drive PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_drive)

# WD1770 disc controller tests
add_executable(test_wd1770 test_wd1770.cpp)
target_link_libraries(test_wd1770 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_wd1770)

# Disc controller socket tests
add_executable(test_disc_controller_socket test_disc_controller_socket.cpp)
target_link_libraries(test_disc_controller_socket PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_controller_socket)

# Acorn 1770 disc controller tests
add_executable(test_acorn_1770_controller test_acorn_1770_controller.cpp)
target_link_libraries(test_acorn_1770_controller PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_acorn_1770_controller)

# Memory histogram tests
add_executable(test_memory_histogram test_memory_histogram.cpp)
target_link_libraries(test_memory_histogram PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_memory_histogram)

# Machine tests
add_executable(test_machine test_machine.cpp)
target_link_libraries(test_machine PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_machine)

# VIA 6522 tests
add_executable(test_via6522 test_via6522.cpp)
target_link_libraries(test_via6522 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_via6522)

# SN76489 sound chip tests
add_executable(test_sn76489 test_sn76489.cpp)
target_link_libraries(test_sn76489 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sn76489)

# SN76489 MAME-inspired tests (hardware-verified behaviors)
add_executable(test_sn76489_mame test_sn76489_mame.cpp)
target_link_libraries(test_sn76489_mame PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sn76489_mame)

# SN76489 DC bias metadata tests
add_executable(test_sn76489_dc_bias test_sn76489_dc_bias.cpp)
target_link_libraries(test_sn76489_dc_bias PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sn76489_dc_bias)

# BBC BASIC SOUND command integration tests
add_executable(test_sound_basic test_sound_basic.cpp)
target_link_libraries(test_sound_basic PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_sound_basic PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_sound_basic)

# Direct VIA sound integration tests (no ROM required)
add_executable(test_sound_via test_sound_via.cpp)
target_link_libraries(test_sound_via PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sound_via)

# MOS sound system diagnostic test
add_executable(test_sound_mos_diagnostic test_sound_mos_diagnostic.cpp)
target_link_libraries(test_sound_mos_diagnostic PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_sound_mos_diagnostic PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_sound_mos_diagnostic)

# Sound chip boot trace
add_executable(test_sound_boot_trace test_sound_boot_trace.cpp)
target_link_libraries(test_sound_boot_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_sound_boot_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_sound_boot_trace)

# Sound edge case tests (inspired by beebjit)
add_executable(test_sound_edge_cases test_sound_edge_cases.cpp)
target_link_libraries(test_sound_edge_cases PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sound_edge_cases)

# Sound state consistency tests
add_executable(test_sound_state test_sound_state.cpp)
target_link_libraries(test_sound_state PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sound_state)

# Sound timing precision tests
add_executable(test_sound_timing test_sound_timing.cpp)
target_link_libraries(test_sound_timing PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_sound_timing)

# CRTC timing diagnostic
add_executable(test_crtc_timing test_crtc_timing.cpp)
target_link_libraries(test_crtc_timing PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_crtc_timing PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_crtc_timing)

# VSYNC frequency measurement
add_executable(test_vsync_frequency test_vsync_frequency.cpp)
target_link_libraries(test_vsync_frequency PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_vsync_frequency PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_vsync_frequency)

# VIA 6522 cycle-accurate timing tests (hardware-validated)
add_executable(test_via_timing test_via_timing.cpp)
target_link_libraries(test_via_timing PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_via_timing)

# Memory map tests
add_executable(test_memory_map test_memory_map.cpp)
target_link_libraries(test_memory_map PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_memory_map)

# Clock tests
add_executable(test_clock test_clock.cpp)
target_link_libraries(test_clock PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_clock)

# IRQ aggregator tests
add_executable(test_irq_aggregator test_irq_aggregator.cpp)
target_link_libraries(test_irq_aggregator PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_irq_aggregator)

# NMI aggregator tests
add_executable(test_nmi_aggregator test_nmi_aggregator.cpp)
target_link_libraries(test_nmi_aggregator PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_nmi_aggregator)

# Disc controller integration tests
add_executable(test_disc_integration test_disc_integration.cpp)
target_link_libraries(test_disc_integration PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_integration)

# Tests requiring ROM files
set(BEEBIUM_ROM_DIR "${CMAKE_SOURCE_DIR}/roms" CACHE PATH "Directory containing BBC Micro ROMs")

# Disc images directory for integration tests
set(BEEBIUM_DISCS_DIR "${CMAKE_SOURCE_DIR}/discs" CACHE PATH "Directory containing test disc images")

# Add ROM and disc dirs to disc integration tests
target_compile_definitions(test_disc_integration PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_DISCS_DIR="${BEEBIUM_DISCS_DIR}"
)

# Boot trace tests (uses ProgramCounterHistogram for instruction profiling)
add_executable(test_boot_trace test_boot_trace.cpp)
target_link_libraries(test_boot_trace PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_trace PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot_trace)

# Boot tests
add_executable(test_boot test_boot.cpp)
target_link_libraries(test_boot PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot)

# Boot sequence test (TDD-style boot progression)
add_executable(test_boot_sequence test_boot_sequence.cpp)
target_link_libraries(test_boot_sequence PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_sequence PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_boot_sequence)

# clearRAM test
add_executable(test_clearram test_clearram.cpp)
target_link_libraries(test_clearram PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_clearram PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_clearram)

# Reset behavior tests (soft reset, power-on reset, Break key)
add_executable(test_reset test_reset.cpp)
target_link_libraries(test_reset PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_reset PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_reset)

# Video output tests
add_executable(test_video test_video.cpp)
target_link_libraries(test_video PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_video PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_video)

# SAA5050 teletext chip tests
add_executable(test_saa5050 test_saa5050.cpp)
target_link_libraries(test_saa5050 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_saa5050)

# Addressable latch (IC32) tests
add_executable(test_addressable_latch test_addressable_latch.cpp)
target_link_libraries(test_addressable_latch PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_addressable_latch)

# CRTC 6845 tests
add_executable(test_crtc6845 test_crtc6845.cpp)
target_link_libraries(test_crtc6845 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_crtc6845)

# Video ULA tests
add_executable(test_video_ula test_video_ula.cpp)
target_link_libraries(test_video_ula PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_video_ula)

# Cursor blink tests
add_executable(test_cursor test_cursor.cpp)
target_link_libraries(test_cursor PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_cursor PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_cursor)

# Cursor blink timing tests (integration test for blink rate verification)
add_executable(test_cursor_blink_timing test_cursor_blink_timing.cpp)
target_link_libraries(test_cursor_blink_timing PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_cursor_blink_timing PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_cursor_blink_timing)

# Keyboard input tests
add_executable(test_keyboard_input test_keyboard_input.cpp)
target_link_libraries(test_keyboard_input PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_keyboard_input PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_keyboard_input)

# Keyboard thread safety tests
add_executable(test_keyboard_race test_keyboard_race.cpp)
target_link_libraries(test_keyboard_race PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_keyboard_race)

# MODE 7 teletext comprehensive tests
add_executable(test_mode7 test_mode7.cpp)
target_link_libraries(test_mode7 PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_mode7 PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_mode7)

# Bitmap modes (0-6) diagnostic tests
add_executable(test_bitmap_modes test_bitmap_modes.cpp)
target_link_libraries(test_bitmap_modes PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_bitmap_modes PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_bitmap_modes)

# Keyboard typing reliability tests
add_executable(test_keyboard_typing test_keyboard_typing.cpp)
target_link_libraries(test_keyboard_typing PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_keyboard_typing PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_keyboard_typing)

# Keyboard lock keys tests (Caps Lock and Shift Lock)
add_executable(test_keyboard_lock_keys test_keyboard_lock_keys.cpp)
target_link_libraries(test_keyboard_lock_keys PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_keyboard_lock_keys PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_keyboard_lock_keys)

# Indicator filter tests
add_executable(test_indicator_filter test_indicator_filter.cpp)
target_link_libraries(test_indicator_filter PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_indicator_filter)

# Indicators class tests
add_executable(test_indicators test_indicators.cpp)
target_link_libraries(test_indicators PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_indicators)

# Advertiser tests (discovery library)
add_executable(test_advertiser test_advertiser.cpp)
target_link_libraries(test_advertiser PRIVATE beebium_discovery Catch2::Catch2WithMain)
catch_discover_tests(test_advertiser)

# gRPC service tests (only if service layer is built)
if(BEEBIUM_BUILD_SERVICE)
    # Video service tests
    add_executable(test_grpc_video test_grpc_video.cpp)
    target_link_libraries(test_grpc_video PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_video PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_video)

    # Keyboard service tests
    add_executable(test_grpc_keyboard test_grpc_keyboard.cpp)
    target_link_libraries(test_grpc_keyboard PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_keyboard PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_keyboard)

    # Debugger service tests
    add_executable(test_grpc_debugger test_grpc_debugger.cpp)
    target_link_libraries(test_grpc_debugger PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_debugger PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_debugger)

    # Disc service tests
    add_executable(test_grpc_disc test_grpc_disc.cpp)
    target_link_libraries(test_grpc_disc PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_disc PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
        BEEBIUM_DISCS_DIR="${BEEBIUM_DISCS_DIR}"
    )
    catch_discover_tests(test_grpc_disc)

    # Indicator service tests
    add_executable(test_grpc_indicator test_grpc_indicator.cpp)
    target_link_libraries(test_grpc_indicator PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_indicator PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
        BEEBIUM_DISCS_DIR="${BEEBIUM_DISCS_DIR}"
    )
    catch_discover_tests(test_grpc_indicator)

    # Audio service tests
    add_executable(test_grpc_audio test_grpc_audio.cpp)
    target_link_libraries(test_grpc_audio PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_audio PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_audio)

    # Sideways service tests
    add_executable(test_grpc_sideways test_grpc_sideways.cpp)
    target_link_libraries(test_grpc_sideways PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_sideways PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_sideways)

    # System service tests
    add_executable(test_grpc_system test_grpc_system.cpp)
    target_link_libraries(test_grpc_system PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_system PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_system)

    # Server main helper function tests
    add_executable(test_server_main test_server_main.cpp)
    target_include_directories(test_server_main PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server/include
    )
    target_link_libraries(test_server_main PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_server_main PRIVATE
        BEEBIUM_VERSION="${PROJECT_VERSION}"
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_server_main)

    # CLI subcommand architecture tests
    add_executable(test_cli test_cli.cpp)
    target_include_directories(test_cli PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server/include
    )
    target_link_libraries(test_cli PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_cli PRIVATE
        BEEBIUM_VERSION="${PROJECT_VERSION}"
    )
    catch_discover_tests(test_cli)
endif()
