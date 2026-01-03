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

#include <concepts>
#include <cstdint>
#include <tuple>

namespace beebium {

// Concept for devices that can generate NMIs (e.g., disc controller)
template<typename T>
concept NmiSource = requires(const T& device) {
    { device.nmi_pending() } -> std::convertible_to<bool>;
};

// NmiBinding associates a device with its NMI bit position
template<typename Device, uint8_t Bit>
    requires NmiSource<Device>
struct NmiBinding {
    Device& device;
    static constexpr uint8_t bit = Bit;

    uint8_t poll() const {
        return device.nmi_pending() ? (1 << bit) : 0;
    }
};

namespace detail {

// Recursive fold to aggregate all NMI bits
template<size_t I, typename Tuple>
uint8_t fold_nmis(const Tuple& bindings) {
    if constexpr (I < std::tuple_size_v<Tuple>) {
        return std::get<I>(bindings).poll() | fold_nmis<I + 1>(bindings);
    } else {
        return 0;
    }
}

} // namespace detail

// NmiAggregator - collects NMI status from all bound devices
// Usage:
//   auto aggregator = make_nmi_aggregator(
//       make_nmi_binding<0>(disc_controller)
//   );
//   uint8_t nmi_mask = aggregator.poll();
//
template<typename... Bindings>
class NmiAggregator {
    std::tuple<Bindings...> bindings_;

public:
    explicit NmiAggregator(Bindings... bindings)
        : bindings_{std::move(bindings)...} {}

    // Poll all NMI sources and return aggregated mask
    uint8_t poll() const {
        return detail::fold_nmis<0>(bindings_);
    }

    // Number of NMI sources
    static constexpr size_t size() { return sizeof...(Bindings); }

    // Access specific binding by index (for testing/debugging)
    template<size_t I>
    auto& get() { return std::get<I>(bindings_); }

    template<size_t I>
    const auto& get() const { return std::get<I>(bindings_); }
};

// Deduction guide for NmiAggregator
template<typename... Bindings>
NmiAggregator(Bindings...) -> NmiAggregator<Bindings...>;

// Helper to create NMI binding with specified bit position
template<uint8_t Bit, NmiSource Device>
constexpr auto make_nmi_binding(Device& device) {
    return NmiBinding<Device, Bit>{device};
}

// Helper to create aggregator with bindings
template<typename... Bindings>
constexpr auto make_nmi_aggregator(Bindings... bindings) {
    return NmiAggregator<Bindings...>{std::move(bindings)...};
}

// Empty NMI aggregator for hardware configs without NMI sources
class NullNmiAggregator {
public:
    uint8_t poll() const { return 0; }
    static constexpr size_t size() { return 0; }
};

} // namespace beebium
