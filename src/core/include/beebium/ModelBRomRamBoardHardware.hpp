// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "AddressableLatch.hpp"
#include "AudioBuffer.hpp"
#include "ClockTypes.hpp"
#include "IrqAggregator.hpp"
#include "PacingConfig.hpp"
#include "MemoryMap.hpp"
#include "MemoryRegion.hpp"
#include "OutputQueue.hpp"
#include "Saa5050.hpp"
#include "SystemViaPeripheral.hpp"
#include "Via6522.hpp"
#include "PixelBatch.hpp"
#include "devices/ConfigurableBankedMemory.hpp"
#include "devices/Crtc6845.hpp"
#include "devices/Ram.hpp"
#include "devices/Rom.hpp"
#include "devices/Sn76489.hpp"
#include "devices/VideoUla.hpp"
#include "disc/Acorn1770DiscController.hpp"
#include "disc/DiscControllerSocket.hpp"
#include "disc/DiscDrive.hpp"
#include "indicators/Indicators.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace beebium {

// BBC Model B with ROM/RAM expansion board providing 16 independent sideways slots.
//
// This configuration emulates a Model B fitted with a third-party ROM/RAM board
// (such as Watford Electronics or similar) that provides:
// - Full 4-bit ROMSEL decoding (16 independent slots, no aliasing)
// - Each slot can be configured as ROM or RAM at runtime
//
// Default jsbeeb-style layout:
//   Slots 0-7:  Sideways RAM (8 x 16KB banks)
//   Slots 8-12: Empty (available for user ROMs)
//   Slot 13:    ADFS ROM
//   Slot 14:    DFS ROM
//   Slot 15:    BASIC ROM
//
// Unlike stock Model B (which has 4-way aliasing), each slot here is independent.
//
class ModelBRomRamBoardHardware {
public:
    // Machine identification and region names (compile-time constants)
    static constexpr std::string_view MACHINE_TYPE = "ModelBRomRamBoard";
    static constexpr std::string_view MACHINE_DISPLAY_NAME = "BBC Model B with ROM/RAM Board";
    static constexpr std::string_view REGION_MAIN_RAM = "main_ram";
    static constexpr std::string_view REGION_MOS_ROM = "mos_rom";

    // Default ROM filenames for this machine
    static constexpr std::string_view DEFAULT_MOS_ROM = "acorn-mos_1_20.rom";
    static constexpr std::string_view DEFAULT_LANGUAGE_ROM = "bbc-basic_2.rom";
    static constexpr uint8_t DEFAULT_LANGUAGE_SLOT = 15;
    static constexpr std::string_view DEFAULT_DFS_ROM = "acorn-dfs_2_26.rom";
    static constexpr uint8_t DEFAULT_DFS_SLOT = 14;

    // Default pacing configuration for this machine
    static constexpr PacingConfig default_pacing_config() {
        return {
            .base_clock_hz = timing::CPU_HZ,  // 2 MHz
            .pacing_hz = 200,                  // 200 Hz sync rate
            .speed_multiplier = 1.0            // Real-time
        };
    }

    // Hardware devices (owned by this struct)
    Ram<32768> main_ram;
    Rom<16384> mos_rom;

    // Sideways ROM/RAM using ConfigurableBankedMemory (16 independent slots)
    using SidewaysType = ConfigurableBankedMemory;
    SidewaysType sideways{ConfigurableBankedMemory::Layout::JsbeebStyle};

    Via6522 system_via;
    Via6522 user_via;

    // IRQ aggregator type - polls VIAs for IRQ status
    using IrqAggregatorType = IrqAggregator<
        IrqBinding<Via6522, 0>,  // System VIA → bit 0
        IrqBinding<Via6522, 1>   // User VIA → bit 1
    >;

    // Video hardware
    Crtc6845 crtc;
    VideoUla video_ula;
    Saa5050 saa5050;

    // Video output queue (optional - only created if video output is enabled)
    std::optional<OutputQueue<PixelBatch>> video_output;

    // Sound hardware
    Sn76489 sound_chip{4'000'000, 48'000};  // 4 MHz clock, 48 kHz sample rate

    // Audio output buffer (optional - only created if audio output is enabled)
    std::optional<AudioBuffer> audio_buffer;

    // Hardware indicators (LEDs) - must be declared before SystemViaPeripheral
    Indicators indicators;

    // System VIA peripherals
    AddressableLatch addressable_latch;
    SystemViaPeripheral system_via_peripheral{addressable_latch, indicators};

    // Disc subsystem
    DiscDrive disc_drive_0{indicators, "floppy-0-activity-led", "Floppy 0"};
    DiscDrive disc_drive_1{indicators, "floppy-1-activity-led", "Floppy 1"};

    // Disc controller socket at 0xFE80-0xFE9F
    DiscControllerSocket disc_socket;

    // Registry ID of installed controller
    std::string installed_controller_id_;

    // ROMSEL register wrapper
    struct RomselRegister {
        SidewaysType& sideways;

        uint8_t read(uint16_t) { return 0xFF; }  // Write-only register
        void write(uint16_t, uint8_t value) {
            sideways.select_bank(value & 0x0F);
        }
    };

    RomselRegister romsel{sideways};

    // Memory map type
    using MemoryMapType = decltype(
        MemoryMap{
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(std::declval<Crtc6845&>()),
            make_region<0xFE20, 0xFE2F, Mirror<0x01>>(std::declval<VideoUla&>()),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(std::declval<Via6522&>()),
            make_region<0xFE30, 0xFE3F, Mirror<0x0F>>(std::declval<RomselRegister&>()),
            make_region<0xFE80, 0xFE9F, Mirror<0x1F>>(std::declval<DiscControllerSocket&>()),
            make_region<0x0000, 0x7FFF>(std::declval<Ram<32768>&>()),
            make_region<0x8000, 0xBFFF>(std::declval<SidewaysType&>()),
            make_region<0xC000, 0xFFFF>(std::declval<Rom<16384>&>())
        }
    );

    // Default constructor
    ModelBRomRamBoardHardware()
        : system_via()
        , user_via()
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
        system_via.set_peripheral(&system_via_peripheral);
        system_via_peripheral.set_sound_chip(&sound_chip);
        indicators.start();
    }

    // Constructor with custom peripherals (for testing)
    ModelBRomRamBoardHardware(ViaPeripheral& system_peripheral, ViaPeripheral& user_peripheral)
        : system_via(system_peripheral)
        , user_via(user_peripheral)
        , memory_map_(make_memory_map())
        , irq_aggregator_(make_irq_aggregator())
    {
    }

    ~ModelBRomRamBoardHardware() {
        indicators.stop();
    }

    // MemoryMappedDevice interface
    uint8_t read(uint16_t addr) {
        return memory_map_.read(addr);
    }

    void write(uint16_t addr, uint8_t value) {
        memory_map_.write(addr, value);
    }

    uint8_t peek(uint16_t addr) const {
        if (addr >= 0xFE40 && addr <= 0xFE5F) {
            return system_via.peek(addr & 0x0F);
        }
        if (addr >= 0xFE60 && addr <= 0xFE7F) {
            return user_via.peek(addr & 0x0F);
        }
        return memory_map_.read(addr);
    }

    uint8_t peek_video(uint16_t addr) const {
        return main_ram.read(addr);
    }

    void reset() {
        main_ram.clear();
        system_via.reset();
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        sound_chip.reset();
        addressable_latch.reset();
        sideways.select_bank(0);
        disc_socket.reset();
    }

    void soft_reset() {
        user_via.reset();
        crtc.reset();
        video_ula.reset();
        saa5050.reset();
        sound_chip.reset();
        addressable_latch.reset();
        disc_socket.reset();
    }

    void enable_video_output(size_t capacity = OutputQueue<PixelBatch>::DEFAULT_CAPACITY) {
        video_output.emplace(capacity);
    }

    void disable_video_output() {
        video_output.reset();
    }

    bool video_output_enabled() const {
        return video_output.has_value();
    }

    void enable_audio_output(size_t capacity = AudioBuffer::DEFAULT_CAPACITY) {
        if (!audio_buffer) {
            audio_buffer.emplace(capacity);
        }
    }

    void disable_audio_output() {
        audio_buffer.reset();
    }

    bool audio_output_enabled() const {
        return audio_buffer.has_value();
    }

    uint8_t poll_irq() {
        return irq_aggregator_.poll();
    }

    uint8_t poll_nmi() {
        uint8_t nmi = disc_socket.nmi_pending() ? 0x01 : 0x00;
        disc_socket.tick();
        return nmi;
    }

    // Startup Options
    void set_startup_options(uint8_t options) {
        system_via_peripheral.keyboard().set_startup_options(options);
    }

    uint8_t startup_options() const {
        return system_via_peripheral.keyboard().startup_options();
    }

    void set_screen_mode(uint8_t mode) {
        system_via_peripheral.keyboard().set_screen_mode(mode);
    }

    uint8_t screen_mode() const {
        return system_via_peripheral.keyboard().screen_mode();
    }

    void set_auto_boot(bool enabled) {
        system_via_peripheral.keyboard().set_auto_boot(enabled);
    }

    bool auto_boot() const {
        return system_via_peripheral.keyboard().auto_boot();
    }

    // Disc Controller Management
    void install_disc_controller(std::unique_ptr<DiscControllerInterface> controller,
                                 std::string_view controller_id = "") {
        disc_socket.install(std::move(controller));
        disc_socket.attach_drive(0, &disc_drive_0);
        disc_socket.attach_drive(1, &disc_drive_1);
        installed_controller_id_ = controller_id;
    }

    void install_acorn_1770() {
        install_disc_controller(std::make_unique<Acorn1770DiscController>(), "acorn-1770");
    }

    std::unique_ptr<DiscControllerInterface> remove_disc_controller() {
        installed_controller_id_.clear();
        return disc_socket.remove();
    }

    bool has_disc_controller() const {
        return disc_socket.has_controller();
    }

    std::string_view installed_controller_id() const {
        return installed_controller_id_;
    }

    void set_spin_up_delay_enabled(bool enabled) {
        if (auto* ctrl = disc_socket.controller()) {
            ctrl->set_spin_up_delay_enabled(enabled);
        }
    }

    bool spin_up_delay_enabled() const {
        if (auto* ctrl = disc_socket.controller()) {
            return ctrl->spin_up_delay_enabled();
        }
        return false;
    }

    // ROM Loading
    void load_mos(const uint8_t* data, size_t size) {
        mos_rom.load(data, size);
    }

    void load_basic(const uint8_t* data, size_t size) {
        sideways.configure_slot(DEFAULT_LANGUAGE_SLOT, SlotType::Rom);
        sideways.load_rom(DEFAULT_LANGUAGE_SLOT, data, size);
        sideways.set_slot_image_name(DEFAULT_LANGUAGE_SLOT, DEFAULT_LANGUAGE_ROM);
    }

    void load_dfs(const uint8_t* data, size_t size) {
        sideways.configure_slot(DEFAULT_DFS_SLOT, SlotType::Rom);
        sideways.load_rom(DEFAULT_DFS_SLOT, data, size);
        sideways.set_slot_image_name(DEFAULT_DFS_SLOT, DEFAULT_DFS_ROM);
    }

    // Load ROM into a specific slot
    void load_rom_to_slot(uint8_t slot, const uint8_t* data, size_t size,
                         std::string_view image_name = "") {
        sideways.configure_slot(slot, SlotType::Rom);
        sideways.load_rom(slot, data, size);
        if (!image_name.empty()) {
            sideways.set_slot_image_name(slot, image_name);
        }
    }

    // Configure a slot's type
    void configure_slot(uint8_t slot, SlotType type) {
        sideways.configure_slot(slot, type);
    }

    // Load ROM by slot number
    void load_rom(uint8_t slot, const uint8_t* data, size_t size) {
        sideways.configure_slot(slot, SlotType::Rom);
        sideways.load_rom(slot, data, size);
    }

    // Memory region discovery
    std::vector<MemoryRegionDescriptor> get_memory_regions() const {
        std::vector<MemoryRegionDescriptor> regions;

        regions.push_back({
            REGION_MAIN_RAM,
            0x0000,
            32768,
            RegionFlags::Readable | RegionFlags::Writable | RegionFlags::Populated
        });

        regions.push_back({
            REGION_MOS_ROM,
            0xC000,
            16384,
            RegionFlags::Readable | RegionFlags::Populated
        });

        for (uint8_t bank = 0; bank < 16; ++bank) {
            RegionFlags flags = RegionFlags::Readable;

            if (sideways.bank_type(bank) == SlotType::Ram) {
                flags = flags | RegionFlags::Writable;
            }

            if (sideways.is_bank_populated(bank)) {
                flags = flags | RegionFlags::Populated;
            }
            if (bank == sideways.selected_bank()) {
                flags = flags | RegionFlags::Active;
            }

            regions.push_back({
                bank_names_[bank],
                0x8000,
                16384,
                flags
            });
        }

        return regions;
    }

    uint8_t peek_region(std::string_view name, uint32_t address) const {
        if (name == REGION_MAIN_RAM) {
            if (address < 0x8000) {
                return main_ram.read(static_cast<uint16_t>(address));
            }
            return 0xFF;
        }
        if (name == REGION_MOS_ROM) {
            if (address >= 0xC000) {
                return mos_rom.read(static_cast<uint16_t>(address - 0xC000));
            }
            return 0xFF;
        }
        if (name.substr(0, 5) == "bank_" && name.size() <= 7) {
            uint8_t bank = parse_bank_number(name);
            if (bank < 16 && address >= 0x8000 && address < 0xC000) {
                return sideways.peek_bank(bank, static_cast<uint16_t>(address - 0x8000));
            }
        }
        return 0xFF;
    }

    uint8_t read_region(std::string_view name, uint32_t address) {
        if (name == REGION_MAIN_RAM) {
            if (address < 0x8000) {
                return main_ram.read(static_cast<uint16_t>(address));
            }
            return 0xFF;
        }
        if (name == REGION_MOS_ROM) {
            if (address >= 0xC000) {
                return mos_rom.read(static_cast<uint16_t>(address - 0xC000));
            }
            return 0xFF;
        }
        if (name.substr(0, 5) == "bank_" && name.size() <= 7) {
            uint8_t bank = parse_bank_number(name);
            if (bank < 16 && address >= 0x8000 && address < 0xC000) {
                return sideways.read_bank(bank, static_cast<uint16_t>(address - 0x8000));
            }
        }
        return 0xFF;
    }

    void write_region(std::string_view name, uint32_t address, uint8_t value) {
        if (name == REGION_MAIN_RAM) {
            if (address < 0x8000) {
                main_ram.write(static_cast<uint16_t>(address), value);
            }
            return;
        }
        if (name == REGION_MOS_ROM) {
            return;
        }
        if (name.substr(0, 5) == "bank_" && name.size() <= 7) {
            uint8_t bank = parse_bank_number(name);
            if (bank < 16 && address >= 0x8000 && address < 0xC000) {
                sideways.write_bank(bank, static_cast<uint16_t>(address - 0x8000), value);
            }
        }
    }

private:
    static constexpr std::string_view bank_names_[16] = {
        "bank_0", "bank_1", "bank_2", "bank_3",
        "bank_4", "bank_5", "bank_6", "bank_7",
        "bank_8", "bank_9", "bank_10", "bank_11",
        "bank_12", "bank_13", "bank_14", "bank_15"
    };

    static uint8_t parse_bank_number(std::string_view name) {
        if (name.size() < 6) return 255;
        if (name.size() == 6) {
            char c = name[5];
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        } else if (name.size() == 7) {
            if (name[5] == '1') {
                char c = name[6];
                if (c >= '0' && c <= '5') return static_cast<uint8_t>(10 + (c - '0'));
            }
        }
        return 255;
    }

private:
    MemoryMapType memory_map_;
    IrqAggregatorType irq_aggregator_;

    IrqAggregatorType make_irq_aggregator() {
        return beebium::make_irq_aggregator(
            make_irq_binding<0>(system_via),
            make_irq_binding<1>(user_via)
        );
    }

    MemoryMapType make_memory_map() {
        return MemoryMap{
            make_region<0xFE00, 0xFE07, Mirror<0x07>>(crtc),
            make_region<0xFE20, 0xFE2F, Mirror<0x01>>(video_ula),
            make_region<0xFE40, 0xFE5F, Mirror<0x0F>>(system_via),
            make_region<0xFE60, 0xFE7F, Mirror<0x0F>>(user_via),
            make_region<0xFE30, 0xFE3F, Mirror<0x0F>>(romsel),
            make_region<0xFE80, 0xFE9F, Mirror<0x1F>>(disc_socket),
            make_region<0x0000, 0x7FFF>(main_ram),
            make_region<0x8000, 0xBFFF>(sideways),
            make_region<0xC000, 0xFFFF>(mos_rom)
        };
    }
};

} // namespace beebium
