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

// MARK: - Shader-facing Value Types

/// Per-region display geometry for split-screen modes.
/// Layout MUST match the Metal `RegionUniforms` struct in Shaders.metal exactly.
struct RegionUniforms {
    var startLine: UInt32 = 0
    var endLine: UInt32 = 0
    var pixelWidth: UInt32 = 0
    var padding: UInt32 = 0
}

/// Maximum number of display regions supported by the shader.
/// Must match the Metal `MAX_REGIONS` constant.
let maxDisplayRegions = 8

/// Display region descriptor sourced from the gRPC frame stream.
struct DisplayRegion: Equatable {
    let startLine: Int
    let endLine: Int
    let pixelWidth: Int
}

/// Uniforms passed to the shader.
/// Layout MUST match the Metal `Uniforms` struct in Shaders.metal exactly
/// (float4 requires 16-byte alignment; explicit padding fields preserve alignment).
struct Uniforms {
    var drawableSize: SIMD2<Float>
    var textureSize: SIMD2<Float>
    var displaySize: SIMD2<Float>
    var totalSize: SIMD2<Float>
    var borderOffset: SIMD2<Float>
    var parScale: Float
    var interlaced: UInt32 = 0
    var leftBorderColor: SIMD4<Float>
    var rightBorderColor: SIMD4<Float>
    var topBorderColor: SIMD4<Float>
    var bottomBorderColor: SIMD4<Float>
    var regionCount: UInt32 = 0
    var _pad0: UInt32 = 0
    var _pad1: UInt32 = 0
    var _pad2: UInt32 = 0
    var regions: (RegionUniforms, RegionUniforms, RegionUniforms, RegionUniforms,
                  RegionUniforms, RegionUniforms, RegionUniforms, RegionUniforms) =
        (RegionUniforms(), RegionUniforms(), RegionUniforms(), RegionUniforms(),
         RegionUniforms(), RegionUniforms(), RegionUniforms(), RegionUniforms())
}

// MARK: - Per-Frame Context

/// Per-frame data delivered by the emulator core via gRPC.
///
/// Pure value type with no Metal dependencies, so display style implementations
/// can be unit-tested without an MTLDevice.
struct FrameContext: Equatable {
    /// Logical texture dimensions (the BGRA buffer received from the server).
    let textureWidth: Int
    let textureHeight: Int
    /// Display dimensions (target after horizontal scaling, e.g. MODE 1 320 -> 640).
    let displayWidth: Int
    let displayHeight: Int
    /// Border dimensions (blanking around the active pixel area).
    let leftBorder: Int
    let rightBorder: Int
    let topBorder: Int
    let bottomBorder: Int
    /// True for interlaced modes (MODE 7); false for progressive bitmap modes.
    let interlaced: Bool
    /// Per-region pixel widths for split-screen modes (e.g. Elite).
    let regions: [DisplayRegion]
}

/// Per-frame data driven by the host window and user preferences.
struct DrawableContext: Equatable {
    /// Window drawable size in pixels (post backing-scale).
    let drawableSize: CGSize
    /// Pixel Aspect Ratio scale.
    /// 0.96 = Authentic (BBC PAL CRT). 1.0 = Crisp (square pixels).
    let parScale: Float

    static func == (lhs: DrawableContext, rhs: DrawableContext) -> Bool {
        return lhs.drawableSize.width == rhs.drawableSize.width
            && lhs.drawableSize.height == rhs.drawableSize.height
            && lhs.parScale == rhs.parScale
    }
}

// MARK: - Display Style Protocol

/// Encapsulates the rendering of an emulator frame: which Metal pipeline to use
/// and how to fill the per-frame uniform buffer.
///
/// Each style is a long-lived object (one instance per app session). The renderer
/// caches the pipeline state by `id` so switching styles at runtime is cheap after
/// the first activation.
///
/// `makeUniforms(frame:drawable:)` MUST be a pure function of its inputs - tests
/// verify the produced values without a Metal device.
protocol DisplayStyle: AnyObject {
    /// Stable identifier (e.g. "debug", "standard").
    /// Used for pipeline caching and persistence keys.
    var id: String { get }

    /// User-visible name (e.g. "Debug", "Standard").
    var displayName: String { get }

    /// Build the Metal pipeline state used by this style. The renderer caches
    /// the result; called at most once per `(style, device)` pair per session.
    func makePipelineState(device: MTLDevice,
                           pixelFormat: MTLPixelFormat) throws -> MTLRenderPipelineState

    /// Produce the uniform values to pass to the shader for this frame.
    ///
    /// MUST be pure - no Metal calls, no side effects. Tests rely on this.
    func makeUniforms(frame: FrameContext, drawable: DrawableContext) -> Uniforms

    /// SwiftUI options panel exposed under the Video sidebar when this style is
    /// active. Return an empty view if the style has no tweakable options.
    @MainActor func makeOptionsView() -> AnyView
}

/// Errors emitted while building a display style's pipeline.
enum DisplayStyleError: Error {
    /// A required vertex or fragment function was not found in the default library.
    case shaderFunctionMissing(String)
    /// The default Metal library could not be loaded from the bundle.
    case defaultLibraryUnavailable
}
