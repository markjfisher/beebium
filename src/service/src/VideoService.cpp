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

    // Pre-allocate buffers at capacity to handle any frame size
    std::vector<uint32_t> raw_buffer(frame_buffer_.capacity_pixels());
    std::vector<uint32_t> packed_buffer(frame_buffer_.capacity_pixels());

    while (!context->IsCancelled()) {
        uint64_t current_version = frame_buffer_.version();

        if (current_version != last_version) {
            // Get logical dimensions and metadata
            size_t width = frame_buffer_.width();
            size_t height = frame_buffer_.height();
            size_t stride = frame_buffer_.stride_pixels();
            const auto& meta = frame_buffer_.metadata();

            // Copy raw frame data (with stride padding)
            frame_buffer_.copy_frame(raw_buffer.data(), raw_buffer.size());

            // Pack pixels by removing stride padding (if any)
            size_t packed_size = width * height;
            if (width == stride) {
                // No padding, direct copy
                std::copy(raw_buffer.begin(), raw_buffer.begin() + packed_size, packed_buffer.begin());
            } else {
                // Remove padding by copying row by row
                for (size_t y = 0; y < height; ++y) {
                    std::copy(raw_buffer.begin() + y * stride,
                              raw_buffer.begin() + y * stride + width,
                              packed_buffer.begin() + y * width);
                }
            }

            // Build frame message
            Frame frame;
            frame.set_frame_number(current_version);
            frame.set_width(static_cast<uint32_t>(width));
            frame.set_height(static_cast<uint32_t>(height));
            frame.set_pixels(packed_buffer.data(), packed_size * sizeof(uint32_t));

            // Set field order based on interlace metadata
            if (meta.interlaced) {
                frame.set_field_order(FieldOrder::EVEN_FIRST);  // Our renderer writes even lines first
            } else {
                frame.set_field_order(FieldOrder::PROGRESSIVE);
            }

            // Set border dimensions from CRTC timing
            frame.set_left_border(meta.left_border);
            frame.set_right_border(meta.right_border);
            frame.set_top_border(meta.top_border);
            frame.set_bottom_border(meta.bottom_border);

            // Set target display resolution for client-side scaling
            frame.set_display_width(meta.display_width);
            frame.set_display_height(meta.display_height);

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
