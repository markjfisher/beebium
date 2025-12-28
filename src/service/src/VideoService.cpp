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

#include "beebium/service/VideoService.hpp"
#include "beebium/FrameBuffer.hpp"

namespace beebium::service {

VideoServiceImpl::VideoServiceImpl(FrameBuffer& frame_buffer)
    : frame_buffer_(frame_buffer) {
}

VideoServiceImpl::~VideoServiceImpl() = default;

grpc::Status VideoServiceImpl::SubscribeFrames(
    grpc::ServerContext* context,
    const SubscribeFramesRequest* /*request*/,
    grpc::ServerWriter<Frame>* writer) {

    uint64_t last_version = 0;

    // Pre-allocate buffer for frame copies to avoid race condition between
    // read_frame() returning a span and swap() changing the buffer.
    // Using copy_frame() ensures the data is fully copied while the lock is held.
    std::vector<uint32_t> pixel_buffer(frame_buffer_.pixel_count());

    while (!context->IsCancelled()) {
        uint64_t current_version = frame_buffer_.version();

        if (current_version != last_version) {
            // New frame available
            Frame frame;
            frame.set_frame_number(current_version);
            frame.set_width(frame_buffer_.width());
            frame.set_height(frame_buffer_.height());

            // Copy frame data safely (holds lock during entire copy)
            frame_buffer_.copy_frame(pixel_buffer.data(), pixel_buffer.size());
            frame.set_pixels(pixel_buffer.data(), pixel_buffer.size() * sizeof(uint32_t));

            if (!writer->Write(frame)) {
                // Client disconnected
                break;
            }

            last_version = current_version;
        }

        // Brief sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return grpc::Status::OK;
}

grpc::Status VideoServiceImpl::GetConfig(
    grpc::ServerContext* /*context*/,
    const GetConfigRequest* /*request*/,
    VideoConfig* response) {

    response->set_width(frame_buffer_.width());
    response->set_height(frame_buffer_.height());
    response->set_framerate_hz(50);  // PAL

    return grpc::Status::OK;
}

} // namespace beebium::service
