// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Focused tests for indicator metadata storage to diagnose x86_64 issues.
// These tests isolate each step of the metadata flow to identify where
// data is lost.

#include <catch2/catch_test_macros.hpp>

#include "beebium/indicators/Indicators.hpp"
#include "beebium/indicators/IndicatorFilter.hpp"

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace beebium;

// =============================================================================
// Step 1: Basic std::unordered_map construction
// =============================================================================

TEST_CASE("unordered_map constructed with emplace has content", "[indicator][metadata][step1]") {
    std::unordered_map<std::string, std::string> map;
    map.emplace("key1", "value1");
    map.emplace("key2", "value2");

    REQUIRE(map.size() == 2);
    CHECK(map.at("key1") == "value1");
    CHECK(map.at("key2") == "value2");
}

TEST_CASE("unordered_map constructed with braced initializer has content", "[indicator][metadata][step1]") {
    std::unordered_map<std::string, std::string> map{
        {"key1", "value1"},
        {"key2", "value2"}
    };

    REQUIRE(map.size() == 2);
    CHECK(map.at("key1") == "value1");
    CHECK(map.at("key2") == "value2");
}

TEST_CASE("unordered_map explicit construction with braced initializer has content", "[indicator][metadata][step1]") {
    auto map = std::unordered_map<std::string, std::string>{
        {"key1", "value1"},
        {"key2", "value2"}
    };

    REQUIRE(map.size() == 2);
    CHECK(map.at("key1") == "value1");
    CHECK(map.at("key2") == "value2");
}

// =============================================================================
// Step 2: Passing map by value to a function
// =============================================================================

namespace {
    size_t receive_map_by_value(std::unordered_map<std::string, std::string> map) {
        return map.size();
    }

    std::unordered_map<std::string, std::string> receive_and_return_map(
        std::unordered_map<std::string, std::string> map) {
        return map;
    }
}

TEST_CASE("map passed by value to function retains content", "[indicator][metadata][step2]") {
    std::unordered_map<std::string, std::string> map;
    map.emplace("key1", "value1");
    map.emplace("key2", "value2");

    size_t size = receive_map_by_value(map);
    REQUIRE(size == 2);
}

TEST_CASE("map passed by value using move retains content in function", "[indicator][metadata][step2]") {
    std::unordered_map<std::string, std::string> map;
    map.emplace("key1", "value1");
    map.emplace("key2", "value2");

    auto returned = receive_and_return_map(std::move(map));
    REQUIRE(returned.size() == 2);
    CHECK(returned.at("key1") == "value1");
}

TEST_CASE("temporary map passed directly to function retains content", "[indicator][metadata][step2]") {
    auto returned = receive_and_return_map(
        std::unordered_map<std::string, std::string>{
            {"key1", "value1"},
            {"key2", "value2"}
        }
    );
    REQUIRE(returned.size() == 2);
}

// =============================================================================
// Step 3: Move assignment
// =============================================================================

TEST_CASE("map move assignment preserves content", "[indicator][metadata][step3]") {
    std::unordered_map<std::string, std::string> source;
    source.emplace("key1", "value1");
    source.emplace("key2", "value2");

    std::unordered_map<std::string, std::string> dest;
    dest = std::move(source);

    REQUIRE(dest.size() == 2);
    CHECK(dest.at("key1") == "value1");
    CHECK(dest.at("key2") == "value2");
}

TEST_CASE("map move construction preserves content", "[indicator][metadata][step3]") {
    std::unordered_map<std::string, std::string> source;
    source.emplace("key1", "value1");
    source.emplace("key2", "value2");

    std::unordered_map<std::string, std::string> dest(std::move(source));

    REQUIRE(dest.size() == 2);
    CHECK(dest.at("key1") == "value1");
    CHECK(dest.at("key2") == "value2");
}

// =============================================================================
// Step 4: Struct containing map and move operations
// =============================================================================

namespace {
    struct TestState {
        std::string name;
        std::unordered_map<std::string, std::string> metadata;

        TestState() = default;

        TestState(TestState&& other) noexcept
            : name(std::move(other.name))
            , metadata(std::move(other.metadata))
        {}

        TestState& operator=(TestState&& other) noexcept {
            name = std::move(other.name);
            metadata = std::move(other.metadata);
            return *this;
        }
    };
}

TEST_CASE("struct with map moves correctly", "[indicator][metadata][step4]") {
    TestState state;
    state.name = "test";
    state.metadata.emplace("key1", "value1");
    state.metadata.emplace("key2", "value2");

    TestState moved(std::move(state));

    CHECK(moved.name == "test");
    REQUIRE(moved.metadata.size() == 2);
    CHECK(moved.metadata.at("key1") == "value1");
}

TEST_CASE("struct with map assigned metadata then moved", "[indicator][metadata][step4]") {
    std::unordered_map<std::string, std::string> metadata;
    metadata.emplace("key1", "value1");
    metadata.emplace("key2", "value2");

    TestState state;
    state.name = "test";
    state.metadata = std::move(metadata);

    // Verify after assignment
    REQUIRE(state.metadata.size() == 2);

    // Now move the struct
    TestState moved(std::move(state));

    REQUIRE(moved.metadata.size() == 2);
    CHECK(moved.metadata.at("key1") == "value1");
}

// =============================================================================
// Step 5: Vector of structs with maps
// =============================================================================

TEST_CASE("push_back struct with map preserves metadata", "[indicator][metadata][step5]") {
    std::vector<TestState> vec;

    TestState state;
    state.name = "test";
    state.metadata.emplace("key1", "value1");
    state.metadata.emplace("key2", "value2");

    vec.push_back(std::move(state));

    REQUIRE(vec.size() == 1);
    REQUIRE(vec.back().metadata.size() == 2);
    CHECK(vec.back().metadata.at("key1") == "value1");
}

TEST_CASE("multiple push_back preserves all metadata through reallocation", "[indicator][metadata][step5]") {
    std::vector<TestState> vec;

    // Force multiple reallocations
    for (int i = 0; i < 10; ++i) {
        TestState state;
        state.name = "test" + std::to_string(i);
        state.metadata.emplace("key", "value" + std::to_string(i));
        vec.push_back(std::move(state));
    }

    // Verify all elements still have their metadata
    for (int i = 0; i < 10; ++i) {
        INFO("Checking element " << i);
        REQUIRE(vec[i].metadata.size() == 1);
        CHECK(vec[i].metadata.at("key") == "value" + std::to_string(i));
    }
}

// =============================================================================
// Step 6: IndicatorState specifically (matches Indicators.hpp exactly)
// =============================================================================

namespace {
    // Copy of IndicatorState from Indicators.hpp for isolated testing
    struct IndicatorStateTest {
        std::string name;
        std::unique_ptr<IndicatorFilter> filter;
        std::unordered_map<std::string, std::string> metadata;
        std::atomic<uint8_t> published_value{0};

        IndicatorStateTest() = default;
        IndicatorStateTest(IndicatorStateTest&& other) noexcept
            : name(std::move(other.name))
            , filter(std::move(other.filter))
            , metadata(std::move(other.metadata))
            , published_value(other.published_value.load()) {}
    };
}

TEST_CASE("IndicatorState-like struct moves metadata correctly", "[indicator][metadata][step6]") {
    std::unordered_map<std::string, std::string> metadata;
    metadata.emplace("label", "CAPS LOCK");
    metadata.emplace("color", "625nm");

    IndicatorStateTest state;
    state.name = "caps-lock-led";
    state.filter = std::make_unique<PassthroughFilter>();
    state.metadata = std::move(metadata);
    state.published_value.store(0);

    REQUIRE(state.metadata.size() == 2);

    // Move into vector
    std::vector<IndicatorStateTest> vec;
    vec.push_back(std::move(state));

    REQUIRE(vec.size() == 1);
    REQUIRE(vec.back().metadata.size() == 2);
    CHECK(vec.back().metadata.at("label") == "CAPS LOCK");
    CHECK(vec.back().metadata.at("color") == "625nm");
}

TEST_CASE("IndicatorState-like struct survives vector reallocation", "[indicator][metadata][step6]") {
    std::vector<IndicatorStateTest> vec;

    // Add first element
    {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "CAPS LOCK");
        metadata.emplace("color", "625nm");

        IndicatorStateTest state;
        state.name = "caps-lock-led";
        state.filter = std::make_unique<PassthroughFilter>();
        state.metadata = std::move(metadata);

        vec.push_back(std::move(state));
    }

    // Check first element before adding more
    REQUIRE(vec[0].metadata.size() == 2);

    // Add more elements to force reallocation
    for (int i = 0; i < 5; ++i) {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "LED " + std::to_string(i));

        IndicatorStateTest state;
        state.name = "led-" + std::to_string(i);
        state.filter = std::make_unique<PassthroughFilter>();
        state.metadata = std::move(metadata);

        vec.push_back(std::move(state));
    }

    // Check first element AFTER reallocation
    INFO("Checking first element after reallocation");
    REQUIRE(vec[0].metadata.size() == 2);
    CHECK(vec[0].metadata.at("label") == "CAPS LOCK");
    CHECK(vec[0].metadata.at("color") == "625nm");

    // Check all elements
    for (size_t i = 1; i < vec.size(); ++i) {
        INFO("Checking element " << i);
        REQUIRE(vec[i].metadata.size() == 1);
    }
}

// =============================================================================
// Step 7: Test actual Indicators class registration
// =============================================================================

TEST_CASE("Indicators::register_indicator stores metadata", "[indicator][metadata][step7]") {
    Indicators indicators;

    std::unordered_map<std::string, std::string> metadata;
    metadata.emplace("label", "Test LED");
    metadata.emplace("color", "625nm");

    auto id = indicators.register_indicator(
        "test-led",
        std::make_unique<PassthroughFilter>(),
        std::move(metadata)
    );

    CHECK(id == 0);

    auto retrieved = indicators.metadata("test-led");
    REQUIRE(retrieved.size() == 2);
    CHECK(retrieved.at("label") == "Test LED");
    CHECK(retrieved.at("color") == "625nm");
}

TEST_CASE("Indicators::register_indicator stores metadata for multiple indicators", "[indicator][metadata][step7]") {
    Indicators indicators;

    // Register first indicator
    {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "CAPS LOCK");
        metadata.emplace("color", "625nm");

        indicators.register_indicator(
            "caps-lock-led",
            std::make_unique<PassthroughFilter>(),
            std::move(metadata)
        );
    }

    // Register second indicator
    {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "SHIFT LOCK");
        metadata.emplace("color", "625nm");

        indicators.register_indicator(
            "shift-lock-led",
            std::make_unique<PassthroughFilter>(),
            std::move(metadata)
        );
    }

    // Check both indicators still have metadata
    auto caps_metadata = indicators.metadata("caps-lock-led");
    REQUIRE(caps_metadata.size() == 2);
    CHECK(caps_metadata.at("label") == "CAPS LOCK");

    auto shift_metadata = indicators.metadata("shift-lock-led");
    REQUIRE(shift_metadata.size() == 2);
    CHECK(shift_metadata.at("label") == "SHIFT LOCK");
}

TEST_CASE("Indicators::register_indicator with 4 indicators like ModelBPlusHardware", "[indicator][metadata][step7]") {
    Indicators indicators;

    // Mimic the exact registration pattern from SystemViaPeripheral and DiscDrive

    // 1. caps-lock-led
    {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "CAPS LOCK");
        metadata.emplace("color", "625nm");
        metadata.emplace("shape", "domed");
        metadata.emplace("related_key", "Caps Lock");

        indicators.register_indicator(
            "caps-lock-led",
            std::make_unique<PassthroughFilter>(),
            std::move(metadata)
        );
    }

    // 2. shift-lock-led
    {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "SHIFT LOCK");
        metadata.emplace("color", "625nm");
        metadata.emplace("shape", "domed");
        metadata.emplace("related_key", "Shift Lock");

        indicators.register_indicator(
            "shift-lock-led",
            std::make_unique<PassthroughFilter>(),
            std::move(metadata)
        );
    }

    // 3. floppy-0-activity-led
    {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "Floppy 0");
        metadata.emplace("color", "568nm");
        metadata.emplace("shape", "rectangular");

        indicators.register_indicator(
            "floppy-0-activity-led",
            std::make_unique<PassthroughFilter>(),
            std::move(metadata)
        );
    }

    // 4. floppy-1-activity-led
    {
        std::unordered_map<std::string, std::string> metadata;
        metadata.emplace("label", "Floppy 1");
        metadata.emplace("color", "568nm");
        metadata.emplace("shape", "rectangular");

        indicators.register_indicator(
            "floppy-1-activity-led",
            std::make_unique<PassthroughFilter>(),
            std::move(metadata)
        );
    }

    // Now verify ALL indicators have their metadata
    auto names = indicators.names();
    REQUIRE(names.size() == 4);

    // Check caps lock
    auto caps = indicators.metadata("caps-lock-led");
    INFO("caps-lock-led metadata size: " << caps.size());
    REQUIRE(caps.size() == 4);
    CHECK(caps.at("label") == "CAPS LOCK");

    // Check shift lock
    auto shift = indicators.metadata("shift-lock-led");
    INFO("shift-lock-led metadata size: " << shift.size());
    REQUIRE(shift.size() == 4);
    CHECK(shift.at("label") == "SHIFT LOCK");

    // Check floppy 0
    auto floppy0 = indicators.metadata("floppy-0-activity-led");
    INFO("floppy-0-activity-led metadata size: " << floppy0.size());
    REQUIRE(floppy0.size() == 3);
    CHECK(floppy0.at("label") == "Floppy 0");

    // Check floppy 1
    auto floppy1 = indicators.metadata("floppy-1-activity-led");
    INFO("floppy-1-activity-led metadata size: " << floppy1.size());
    REQUIRE(floppy1.size() == 3);
    CHECK(floppy1.at("label") == "Floppy 1");
}

// =============================================================================
// Step 8: Test with default-constructed Indicators in a struct
// (mimics ModelBHardware member initialization)
// =============================================================================

namespace {
    struct MockHardware {
        Indicators indicators;

        // Simulates what SystemViaPeripheral does
        void register_leds() {
            std::unordered_map<std::string, std::string> caps_metadata;
            caps_metadata.emplace("label", "CAPS LOCK");
            caps_metadata.emplace("color", "625nm");

            indicators.register_indicator(
                "caps-lock-led",
                std::make_unique<PassthroughFilter>(),
                std::move(caps_metadata)
            );

            std::unordered_map<std::string, std::string> shift_metadata;
            shift_metadata.emplace("label", "SHIFT LOCK");
            shift_metadata.emplace("color", "625nm");

            indicators.register_indicator(
                "shift-lock-led",
                std::make_unique<PassthroughFilter>(),
                std::move(shift_metadata)
            );
        }
    };
}

TEST_CASE("Indicators as struct member stores metadata", "[indicator][metadata][step8]") {
    MockHardware hw;
    hw.register_leds();

    auto caps = hw.indicators.metadata("caps-lock-led");
    REQUIRE(caps.size() == 2);
    CHECK(caps.at("label") == "CAPS LOCK");

    auto shift = hw.indicators.metadata("shift-lock-led");
    REQUIRE(shift.size() == 2);
    CHECK(shift.at("label") == "SHIFT LOCK");
}

// =============================================================================
// Step 9: Test registration during default member initialization
// (exactly mimics ModelBHardware pattern where registration happens in
// component constructors that are invoked via default member initializers)
// =============================================================================

namespace {
    // Mimics DiscDrive: registers an indicator in its constructor
    struct RegisteringComponent {
        Indicators* indicators_;
        uint16_t indicator_id_;

        RegisteringComponent(Indicators& ind, const std::string& name, const std::string& label)
            : indicators_(&ind)
        {
            std::unordered_map<std::string, std::string> metadata;
            metadata.emplace("label", label);
            metadata.emplace("color", "625nm");

            indicator_id_ = indicators_->register_indicator(
                name,
                std::make_unique<PassthroughFilter>(),
                std::move(metadata)
            );
        }
    };

    // Mimics SystemViaPeripheral: registers multiple indicators in constructor
    struct MultiRegisteringComponent {
        Indicators* indicators_;
        uint16_t led1_id_;
        uint16_t led2_id_;

        MultiRegisteringComponent(Indicators& ind)
            : indicators_(&ind)
        {
            std::unordered_map<std::string, std::string> meta1;
            meta1.emplace("label", "CAPS LOCK");
            meta1.emplace("color", "625nm");
            meta1.emplace("shape", "domed");
            meta1.emplace("related_key", "Caps Lock");

            led1_id_ = indicators_->register_indicator(
                "caps-lock-led",
                std::make_unique<PassthroughFilter>(),
                std::move(meta1)
            );

            std::unordered_map<std::string, std::string> meta2;
            meta2.emplace("label", "SHIFT LOCK");
            meta2.emplace("color", "625nm");
            meta2.emplace("shape", "domed");
            meta2.emplace("related_key", "Shift Lock");

            led2_id_ = indicators_->register_indicator(
                "shift-lock-led",
                std::make_unique<PassthroughFilter>(),
                std::move(meta2)
            );
        }
    };

    // Mimics ModelBHardware: Indicators member followed by components that
    // register indicators in their constructors via default member initializers
    struct HardwareWithDefaultMemberInit {
        Indicators indicators;  // Constructed first
        RegisteringComponent component{indicators, "test-led", "Test LED"};  // Registration during init
    };

    // Full pattern: multiple components registering during default member init
    struct HardwareWithMultipleComponents {
        Indicators indicators;  // Must be first
        MultiRegisteringComponent via_peripheral{indicators};  // Registers 2 LEDs
        RegisteringComponent drive0{indicators, "floppy-0-led", "Floppy 0"};  // Registers 1 LED
        RegisteringComponent drive1{indicators, "floppy-1-led", "Floppy 1"};  // Registers 1 LED
    };
}

TEST_CASE("Registration during default member init - single component", "[indicator][metadata][step9]") {
    HardwareWithDefaultMemberInit hw;

    auto names = hw.indicators.names();
    INFO("Registered indicator names: " << names.size());
    REQUIRE(names.size() == 1);

    auto meta = hw.indicators.metadata("test-led");
    INFO("test-led metadata size: " << meta.size());
    REQUIRE(meta.size() == 2);
    CHECK(meta.at("label") == "Test LED");
    CHECK(meta.at("color") == "625nm");
}

TEST_CASE("Registration during default member init - multiple components", "[indicator][metadata][step9]") {
    HardwareWithMultipleComponents hw;

    auto names = hw.indicators.names();
    INFO("Registered indicator count: " << names.size());
    REQUIRE(names.size() == 4);

    // Check caps-lock from MultiRegisteringComponent
    auto caps = hw.indicators.metadata("caps-lock-led");
    INFO("caps-lock-led metadata size: " << caps.size());
    REQUIRE(caps.size() == 4);
    CHECK(caps.at("label") == "CAPS LOCK");

    // Check shift-lock from MultiRegisteringComponent
    auto shift = hw.indicators.metadata("shift-lock-led");
    INFO("shift-lock-led metadata size: " << shift.size());
    REQUIRE(shift.size() == 4);
    CHECK(shift.at("label") == "SHIFT LOCK");

    // Check floppy-0 from RegisteringComponent
    auto floppy0 = hw.indicators.metadata("floppy-0-led");
    INFO("floppy-0-led metadata size: " << floppy0.size());
    REQUIRE(floppy0.size() == 2);
    CHECK(floppy0.at("label") == "Floppy 0");

    // Check floppy-1 from RegisteringComponent
    auto floppy1 = hw.indicators.metadata("floppy-1-led");
    INFO("floppy-1-led metadata size: " << floppy1.size());
    REQUIRE(floppy1.size() == 2);
    CHECK(floppy1.at("label") == "Floppy 1");
}

TEST_CASE("Registration during default member init - verify order independence", "[indicator][metadata][step9]") {
    // Create multiple instances to verify no static/global state issues
    HardwareWithMultipleComponents hw1;
    HardwareWithMultipleComponents hw2;

    // Each should have independent indicators
    REQUIRE(hw1.indicators.names().size() == 4);
    REQUIRE(hw2.indicators.names().size() == 4);

    // Both should have valid metadata
    REQUIRE(hw1.indicators.metadata("caps-lock-led").size() == 4);
    REQUIRE(hw2.indicators.metadata("caps-lock-led").size() == 4);
}

// =============================================================================
// Step 10: Test with nesting structure similar to Machine/MachineState/Hardware
// This mimics the actual runtime structure more closely
// =============================================================================

namespace {
    // Similar to MachineState<HardwareWithMultipleComponents>
    struct StateLike {
        uint64_t dummy_cpu_state[128]{};  // Simulate M6502 struct size
        HardwareWithMultipleComponents memory;
        uint64_t cycle_count = 0;
    };

    // Similar to Machine<..., HardwareWithMultipleComponents>
    class MachineLike {
    public:
        MachineLike() {
            // Simulate what Machine constructor does
            setup();
        }

        StateLike& state() { return state_; }
        const StateLike& state() const { return state_; }

    private:
        void setup() {
            // Machine constructor does various setup here
            // The key point is that state_ is already fully constructed
            // before this runs
        }

        StateLike state_;
        int other_member_1_ = 0;
        int other_member_2_ = 0;
    };
}

TEST_CASE("Nested structure like Machine/MachineState/Hardware", "[indicator][metadata][step10]") {
    MachineLike machine;

    auto names = machine.state().memory.indicators.names();
    INFO("Registered indicator count: " << names.size());
    REQUIRE(names.size() == 4);

    // Access metadata through the nested structure path
    auto caps = machine.state().memory.indicators.metadata("caps-lock-led");
    INFO("caps-lock-led metadata size: " << caps.size());
    REQUIRE(caps.size() == 4);
    CHECK(caps.at("label") == "CAPS LOCK");
    CHECK(caps.at("color") == "625nm");

    auto shift = machine.state().memory.indicators.metadata("shift-lock-led");
    INFO("shift-lock-led metadata size: " << shift.size());
    REQUIRE(shift.size() == 4);

    auto floppy0 = machine.state().memory.indicators.metadata("floppy-0-led");
    INFO("floppy-0-led metadata size: " << floppy0.size());
    REQUIRE(floppy0.size() == 2);

    auto floppy1 = machine.state().memory.indicators.metadata("floppy-1-led");
    INFO("floppy-1-led metadata size: " << floppy1.size());
    REQUIRE(floppy1.size() == 2);
}

// =============================================================================
// Step 11: Test with actual ModelBHardware if available
// This is the most realistic test - uses the real hardware struct
// =============================================================================

#include "beebium/ModelBHardware.hpp"

TEST_CASE("ModelBHardware indicators have metadata", "[indicator][metadata][step11]") {
    beebium::ModelBHardware hw;

    auto names = hw.indicators.names();
    INFO("Registered indicator count: " << names.size());
    // Model B has 4 indicators: caps-lock, shift-lock, floppy-0, floppy-1
    REQUIRE(names.size() == 4);

    // Check caps-lock metadata
    auto caps = hw.indicators.metadata("caps-lock-led");
    INFO("caps-lock-led metadata size: " << caps.size());
    REQUIRE(caps.size() == 4);
    CHECK(caps.at("label") == "CAPS LOCK");
    CHECK(caps.at("color") == "625nm");
    CHECK(caps.at("shape") == "domed");
    CHECK(caps.at("related_key") == "Caps Lock");

    // Check shift-lock metadata
    auto shift = hw.indicators.metadata("shift-lock-led");
    INFO("shift-lock-led metadata size: " << shift.size());
    REQUIRE(shift.size() == 4);
    CHECK(shift.at("label") == "SHIFT LOCK");

    // Check floppy-0 metadata
    auto floppy0 = hw.indicators.metadata("floppy-0-activity-led");
    INFO("floppy-0-activity-led metadata size: " << floppy0.size());
    REQUIRE(floppy0.size() == 3);
    CHECK(floppy0.at("label") == "Floppy 0");
    CHECK(floppy0.at("color") == "568nm");
    CHECK(floppy0.at("shape") == "rectangular");

    // Check floppy-1 metadata
    auto floppy1 = hw.indicators.metadata("floppy-1-activity-led");
    INFO("floppy-1-activity-led metadata size: " << floppy1.size());
    REQUIRE(floppy1.size() == 3);
    CHECK(floppy1.at("label") == "Floppy 1");
}

TEST_CASE("ModelBHardware in MachineState-like wrapper", "[indicator][metadata][step11]") {
    // Wrap ModelBHardware in a struct like MachineState does
    struct State {
        uint64_t dummy[128]{};  // Simulates M6502
        beebium::ModelBHardware memory;
        uint64_t cycle = 0;
    };

    State state;

    auto names = state.memory.indicators.names();
    INFO("Registered indicator count: " << names.size());
    REQUIRE(names.size() == 4);

    auto caps = state.memory.indicators.metadata("caps-lock-led");
    INFO("caps-lock-led metadata size: " << caps.size());
    REQUIRE(caps.size() == 4);
    CHECK(caps.at("label") == "CAPS LOCK");
}
