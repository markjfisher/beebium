// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include "TestScratchRam.hpp"
#include "ScratchRamService.hpp"

namespace beebium {

TestScratchRam::TestScratchRam() = default;
TestScratchRam::~TestScratchRam() = default;

std::unique_ptr<TestScratchRam> TestScratchRam::create() {
    return std::unique_ptr<TestScratchRam>(new TestScratchRam());
}

void TestScratchRam::init(ExtensionContext& ctx) {
    ctx.get<OneMHzBusPort>().claim_addresses(kBaseOffset, kEndOffset, *this);
    service_ = std::make_unique<ScratchRamServiceImpl>(*this);
}

void TestScratchRam::shutdown() {
    service_.reset();
}

std::vector<grpc::Service*> TestScratchRam::grpc_services() {
    if (service_) {
        return {service_.get()};
    }
    return {};
}

}  // namespace beebium
