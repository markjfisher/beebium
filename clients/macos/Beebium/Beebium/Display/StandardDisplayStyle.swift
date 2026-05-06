// Copyright © 2025-2026 Robert Smallshire <robert@smallshire.org.uk>
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

import Foundation
import Metal
import SwiftUI
import simd

/// Display style that fits only the active pixel area into the drawable, with
/// no coloured debug borders. The blanking around the active area appears as
/// the window's background colour (letterbox/pillarbox).
///
/// The active area's apparent size therefore stays constant when the BBC
/// switches screen modes, even though the CRTC border tracking varies.
final class StandardDisplayStyle: DisplayStyle {
    let id = "standard"
    let displayName = "Standard"

    func makePipelineState(device: MTLDevice,
                           pixelFormat: MTLPixelFormat) throws -> MTLRenderPipelineState {
        guard let library = device.makeDefaultLibrary() else {
            throw DisplayStyleError.defaultLibraryUnavailable
        }
        // Reuses the shared vertexShader (the aspect-fit math is identical
        // once totalSize == displaySize). The Standard fragment shader has no
        // border-drawing branches.
        guard let vertexFn = library.makeFunction(name: "vertexShader") else {
            throw DisplayStyleError.shaderFunctionMissing("vertexShader")
        }
        guard let fragmentFn = library.makeFunction(name: "fragmentShaderStandard") else {
            throw DisplayStyleError.shaderFunctionMissing("fragmentShaderStandard")
        }
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = vertexFn
        descriptor.fragmentFunction = fragmentFn
        descriptor.colorAttachments[0].pixelFormat = pixelFormat
        return try device.makeRenderPipelineState(descriptor: descriptor)
    }

    func makeUniforms(frame: FrameContext, drawable: DrawableContext) -> Uniforms {
        // Standard fits the active pixel area only; CRTC borders are not part
        // of the picture. totalSize == displaySize and borderOffset == (0, 0).
        let displaySize = SIMD2<Float>(Float(frame.displayWidth), Float(frame.displayHeight))

        var regionUniforms = (
            RegionUniforms(), RegionUniforms(), RegionUniforms(), RegionUniforms(),
            RegionUniforms(), RegionUniforms(), RegionUniforms(), RegionUniforms()
        )
        let regionCount = min(frame.regions.count, maxDisplayRegions)
        withUnsafeMutablePointer(to: &regionUniforms) { ptr in
            let base = UnsafeMutableRawPointer(ptr).bindMemory(
                to: RegionUniforms.self, capacity: maxDisplayRegions)
            for i in 0..<regionCount {
                base[i] = RegionUniforms(
                    startLine: UInt32(frame.regions[i].startLine),
                    endLine: UInt32(frame.regions[i].endLine),
                    pixelWidth: UInt32(frame.regions[i].pixelWidth)
                )
            }
        }

        return Uniforms(
            drawableSize: SIMD2<Float>(Float(drawable.drawableSize.width),
                                       Float(drawable.drawableSize.height)),
            textureSize: SIMD2<Float>(Float(frame.textureWidth), Float(frame.textureHeight)),
            displaySize: displaySize,
            totalSize: displaySize,
            borderOffset: SIMD2<Float>(0, 0),
            parScale: drawable.parScale,
            interlaced: frame.interlaced ? 1 : 0,
            // Standard does not draw the four debug borders; the fragment
            // shader doesn't read the colour fields. Zero them for hygiene
            // so a future shader change can't accidentally pick up stale
            // values.
            leftBorderColor: SIMD4<Float>(0, 0, 0, 0),
            rightBorderColor: SIMD4<Float>(0, 0, 0, 0),
            topBorderColor: SIMD4<Float>(0, 0, 0, 0),
            bottomBorderColor: SIMD4<Float>(0, 0, 0, 0),
            regionCount: UInt32(regionCount),
            regions: regionUniforms
        )
    }

    @MainActor
    func makeOptionsView() -> AnyView {
        // No tweakable parameters yet; commit C adds the overscan amount.
        AnyView(
            Text("The active pixel area is fitted to the window. Blanking "
                 + "appears as the window background colour.")
                .font(.caption)
                .foregroundColor(.secondary)
                .fixedSize(horizontal: false, vertical: true)
        )
    }
}
