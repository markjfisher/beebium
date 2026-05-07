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

/// Display style that paints four distinct coloured borders around the active
/// pixel area, with the overall picture (active + borders) aspect-fit into the
/// drawable. Used to make CRTC border tracking and overscan visible while
/// developing the emulator.
final class DebugDisplayStyle: DisplayStyle {
    let id = "debug"
    let displayName = "Debug"
    let hasOptions = false

    /// RGBA border colours. Defaults match the values that shipped before the
    /// display-style refactor so behaviour is unchanged after switching to this
    /// style. Mutable so a future "customise debug colours" UI can tweak them.
    var leftBorderColor = SIMD4<Float>(0.5, 0.0, 0.0, 1.0)   // Dark red
    var rightBorderColor = SIMD4<Float>(0.0, 0.5, 0.0, 1.0)  // Dark green
    var topBorderColor = SIMD4<Float>(0.0, 0.0, 0.5, 1.0)    // Dark blue
    var bottomBorderColor = SIMD4<Float>(0.5, 0.5, 0.0, 1.0) // Dark yellow

    func makePipelineState(device: MTLDevice,
                           pixelFormat: MTLPixelFormat) throws -> MTLRenderPipelineState {
        guard let library = device.makeDefaultLibrary() else {
            throw DisplayStyleError.defaultLibraryUnavailable
        }
        guard let vertexFn = library.makeFunction(name: "vertexShader") else {
            throw DisplayStyleError.shaderFunctionMissing("vertexShader")
        }
        guard let fragmentFn = library.makeFunction(name: "fragmentShader") else {
            throw DisplayStyleError.shaderFunctionMissing("fragmentShader")
        }
        let descriptor = MTLRenderPipelineDescriptor()
        descriptor.vertexFunction = vertexFn
        descriptor.fragmentFunction = fragmentFn
        descriptor.colorAttachments[0].pixelFormat = pixelFormat
        return try device.makeRenderPipelineState(descriptor: descriptor)
    }

    func makeUniforms(frame: FrameContext, drawable: DrawableContext) -> Uniforms {
        // Debug style aspect-fits the entire active+border rectangle so the
        // coloured borders are visible inside the window content area.
        let totalWidth = Float(frame.leftBorder + frame.displayWidth + frame.rightBorder)
        let totalHeight = Float(frame.topBorder + frame.displayHeight + frame.bottomBorder)

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
            displaySize: SIMD2<Float>(Float(frame.displayWidth), Float(frame.displayHeight)),
            totalSize: SIMD2<Float>(totalWidth, totalHeight),
            borderOffset: SIMD2<Float>(Float(frame.leftBorder), Float(frame.topBorder)),
            parScale: drawable.parScale,
            interlaced: frame.interlaced ? 1 : 0,
            leftBorderColor: leftBorderColor,
            rightBorderColor: rightBorderColor,
            topBorderColor: topBorderColor,
            bottomBorderColor: bottomBorderColor,
            // Debug's fragment shader does not read edgeMarginColor; zero
            // for hygiene so a future shader change can't pick up stale
            // values.
            edgeMarginColor: SIMD4<Float>(0, 0, 0, 0),
            regionCount: UInt32(regionCount),
            // Debug fills the drawable with active+border content; an extra
            // edge margin would be a margin around the borders, which is
            // confusing. Always zero.
            edgeMargin: 0,
            regions: regionUniforms
        )
    }

    @MainActor
    func makeOptionsView() -> AnyView {
        // No tweakable options today; `hasOptions == false` means the sidebar
        // does not render this view, but we provide an empty body to satisfy
        // the protocol and to be defensive against future callers that
        // forget to gate on `hasOptions`.
        AnyView(EmptyView())
    }
}
