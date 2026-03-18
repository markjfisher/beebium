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

# MC6854 ADLC tests
add_executable(test_mc6854 test_mc6854.cpp)
target_link_libraries(test_mc6854 PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_mc6854)

# Econet socket tests
add_executable(test_econet_socket test_econet_socket.cpp)
target_link_libraries(test_econet_socket PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_econet_socket)

# Four-way handshake (AUN mode) tests
add_executable(test_four_way_handshake test_four_way_handshake.cpp)
target_link_libraries(test_four_way_handshake PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_four_way_handshake)

# AUN packet encode/decode tests
add_executable(test_aun_packet test_aun_packet.cpp)
target_link_libraries(test_aun_packet PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_aun_packet)

# AUN backend UDP loopback tests
add_executable(test_aun_backend test_aun_backend.cpp)
target_link_libraries(test_aun_backend PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_aun_backend)

# Disc controller socket tests
add_executable(test_disc_controller_socket test_disc_controller_socket.cpp)
target_link_libraries(test_disc_controller_socket PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_disc_controller_socket)

# Acorn 1770 disc controller tests
add_executable(test_acorn_1770_controller test_acorn_1770_controller.cpp)
target_link_libraries(test_acorn_1770_controller PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_acorn_1770_controller)

# Expression engine tests
add_executable(test_expression test_expression.cpp)
target_link_libraries(test_expression PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_expression)

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

# Shipping ROMs (tracked in roms/ at repo root).
set(BEEBIUM_ROM_DIR "${CMAKE_SOURCE_DIR}/roms" CACHE PATH "Directory containing BBC Micro ROMs")

# Extended test ROM directory with additional NFS/ANFS versions for Econet testing
# (tracked in tests/assets/roms/).
set(BEEBIUM_TEST_ROM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets/roms")

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

# Econet boot integration tests
add_executable(test_boot_econet test_boot_econet.cpp)
target_link_libraries(test_boot_econet PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_econet PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ROM_DIR="${BEEBIUM_TEST_ROM_DIR}"
)
catch_discover_tests(test_boot_econet)

# Econet file server integration tests (discretionary — need BEEBIUM_FILESERVER env var)
add_executable(test_econet_fileserver test_econet_fileserver.cpp)
target_link_libraries(test_econet_fileserver PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_econet_fileserver PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ROM_DIR="${BEEBIUM_TEST_ROM_DIR}"
)
catch_discover_tests(test_econet_fileserver
    PROPERTIES SKIP_RETURN_CODE 4
)

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
# Allow skipped tests to pass (mDNS may not be available on CI)
catch_discover_tests(test_advertiser EXTRA_ARGS --allow-running-no-tests)

# Tube ULA tests
add_executable(test_tube_ula test_tube_ula.cpp)
target_link_libraries(test_tube_ula PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_ula)

# Tube socket tests
add_executable(test_tube_socket test_tube_socket.cpp)
target_link_libraries(test_tube_socket PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_socket)

# Tube shared memory layout tests
add_executable(test_tube_shared test_tube_shared.cpp)
target_link_libraries(test_tube_shared PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_shared)

# Tube host port tests
add_executable(test_tube_host_port test_tube_host_port.cpp)
target_link_libraries(test_tube_host_port PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_host_port)

# Parasite memory map tests (6502 second processor)
add_executable(test_parasite_memory_map test_parasite_memory_map.cpp)
target_link_libraries(test_parasite_memory_map PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_parasite_memory_map)

# Tube parasite port tests
add_executable(test_tube_parasite_port test_tube_parasite_port.cpp)
target_link_libraries(test_tube_parasite_port PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_parasite_port)

# Parasite CPU tests
add_executable(test_parasite_cpu test_parasite_cpu.cpp)
target_link_libraries(test_parasite_cpu PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_parasite_cpu)

# Parasite runner tests
add_executable(test_parasite_runner test_parasite_runner.cpp)
target_link_libraries(test_parasite_runner PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_parasite_runner)

# Parasite boot integration tests (real ROM)
add_executable(test_parasite_boot test_parasite_boot.cpp)
target_link_libraries(test_parasite_boot PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_parasite_boot PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_parasite_boot)

# Tube POSIX shared memory manager tests
add_executable(test_tube_shared_memory test_tube_shared_memory.cpp)
target_link_libraries(test_tube_shared_memory PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_shared_memory)

# Tube end-to-end tests (host + parasite via shared memory, real ROM)
add_executable(test_tube_end_to_end test_tube_end_to_end.cpp)
target_link_libraries(test_tube_end_to_end PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_tube_end_to_end PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
)
catch_discover_tests(test_tube_end_to_end)

# Tube boot screen integration tests (host + parasite, real ROMs)
add_executable(test_boot_tube test_boot_tube.cpp)
target_link_libraries(test_boot_tube PRIVATE beebium_core Catch2::Catch2WithMain)
target_compile_definitions(test_boot_tube PRIVATE
    BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    BEEBIUM_TEST_ROM_DIR="${BEEBIUM_TEST_ROM_DIR}"
)
catch_discover_tests(test_boot_tube)

# Tube shared memory cross-thread tests
add_executable(test_tube_shared_threads test_tube_shared_threads.cpp)
target_link_libraries(test_tube_shared_threads PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_shared_threads)

# Tube cross-process tests (fork-based, POSIX shared memory)
add_executable(test_tube_cross_process test_tube_cross_process.cpp)
target_link_libraries(test_tube_cross_process PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_cross_process)

# Tube R1 transfer with real 65C02 CPU
add_executable(test_tube_r1_6502 test_tube_r1_6502.cpp)
target_link_libraries(test_tube_r1_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r1_6502)

# Tube R3 NMI transfer with real 65C02 CPU
add_executable(test_tube_r3_nmi_6502 test_tube_r3_nmi_6502.cpp)
target_link_libraries(test_tube_r3_nmi_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r3_nmi_6502)

# Tube R2 transfer with real 65C02 CPU
add_executable(test_tube_r2_6502 test_tube_r2_6502.cpp)
target_link_libraries(test_tube_r2_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r2_6502)

# Tube R4 transfer with real 65C02 CPU
add_executable(test_tube_r4_6502 test_tube_r4_6502.cpp)
target_link_libraries(test_tube_r4_6502 PRIVATE beebium_core 6502_lib Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r4_6502)

# Tube R3 register race condition regression tests
add_executable(test_tube_r3_race test_tube_r3_race.cpp)
target_link_libraries(test_tube_r3_race PRIVATE beebium_core Catch2::Catch2WithMain)
catch_discover_tests(test_tube_r3_race)

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

    # Econet service tests
    add_executable(test_grpc_econet test_grpc_econet.cpp)
    target_link_libraries(test_grpc_econet PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_econet PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_econet)

    # Tube service tests
    add_executable(test_grpc_tube test_grpc_tube.cpp)
    target_link_libraries(test_grpc_tube PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_grpc_tube PRIVATE
        BEEBIUM_ROM_DIR="${BEEBIUM_ROM_DIR}"
    )
    catch_discover_tests(test_grpc_tube)

    # Parasite gRPC service tests
    add_executable(test_parasite_grpc test_parasite_grpc.cpp)
    target_link_libraries(test_parasite_grpc PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_parasite_grpc)

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
        nlohmann_json::nlohmann_json
        stb_image_write
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
        nlohmann_json::nlohmann_json
        stb_image_write
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_cli PRIVATE
        BEEBIUM_VERSION="${PROJECT_VERSION}"
        BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
    )
    catch_discover_tests(test_cli)

    # Preset loader tests
    add_executable(test_preset_loader test_preset_loader.cpp)
    target_include_directories(test_preset_loader PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server/include
    )
    target_link_libraries(test_preset_loader PRIVATE
        beebium_service
        nlohmann_json::nlohmann_json
        Catch2::Catch2WithMain
    )
    target_compile_definitions(test_preset_loader PRIVATE
        BEEBIUM_TEST_ASSETS_DIR="${BEEBIUM_TEST_ASSETS_DIR}"
    )
    catch_discover_tests(test_preset_loader)

    # Preset paths utility tests
    add_executable(test_preset_paths test_preset_paths.cpp)
    target_include_directories(test_preset_paths PRIVATE
        ${CMAKE_SOURCE_DIR}/src/server/include
    )
    target_link_libraries(test_preset_paths PRIVATE
        beebium_service
        Catch2::Catch2WithMain
    )
    catch_discover_tests(test_preset_paths)

    # Preset subcommand integration tests
    add_executable(test_preset_subcommands test_preset_subcommands.cpp)
    target_link_libraries(test_preset_subcommands PRIVATE
        beebium_core
        Catch2::Catch2WithMain
    )
    # Integration tests depend on executables being built
    add_dependencies(test_preset_subcommands beebium-servers)
    # These tests share temp files and a user presets directory, so they must
    # run sequentially to avoid races when CTest runs tests in parallel.
    catch_discover_tests(test_preset_subcommands PROPERTIES RESOURCE_LOCK preset_dir)
endif()
