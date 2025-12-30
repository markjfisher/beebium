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

import Foundation
import Metal
import MetalKit
import simd

/// Uniforms passed to the shader for aspect ratio correction and border rendering
/// Note: Struct layout must match Metal exactly. float4 requires 16-byte alignment.
struct Uniforms {
    var drawableSize: SIMD2<Float>   // offset 0: Size of the drawable in pixels
    var textureSize: SIMD2<Float>    // offset 8: Size of the texture (content area)
    var totalSize: SIMD2<Float>      // offset 16: Total size including borders
    var borderOffset: SIMD2<Float>   // offset 24: Offset of content within total (left, top)
    var parScale: Float              // offset 32: Pixel Aspect Ratio scale (0.96 for BBC)
    var padding1: Float = 0          // offset 36: Padding for alignment
    var padding2: SIMD2<Float> = .zero  // offset 40: Explicit padding for float4 alignment
    var leftBorderColor: SIMD4<Float>    // offset 48: RGBA color for left border
    var rightBorderColor: SIMD4<Float>   // offset 64: RGBA color for right border
    var topBorderColor: SIMD4<Float>     // offset 80: RGBA color for top border
    var bottomBorderColor: SIMD4<Float>  // offset 96: RGBA color for bottom border
}

/// Renders emulator video frames using Metal
final class MetalRenderer: NSObject {
    private let device: MTLDevice
    private let commandQueue: MTLCommandQueue
    private let pipelineState: MTLRenderPipelineState

    private var frameTexture: MTLTexture?
    private var textureWidth: Int = 0
    private var textureHeight: Int = 0
    private var drawableSize: CGSize = .zero

    // Border dimensions
    private var leftBorder: Int = 0
    private var rightBorder: Int = 0
    private var topBorder: Int = 0
    private var bottomBorder: Int = 0

    /// Pixel Aspect Ratio scale - BBC pixels are 0.96 as wide as they are tall
    private let parScale: Float = 0.96

    // Border colors (distinct colors for visual debugging)
    private let leftBorderColor = SIMD4<Float>(0.5, 0.0, 0.0, 1.0)    // Dark red
    private let rightBorderColor = SIMD4<Float>(0.0, 0.5, 0.0, 1.0)   // Dark green
    private let topBorderColor = SIMD4<Float>(0.0, 0.0, 0.5, 1.0)     // Dark blue
    private let bottomBorderColor = SIMD4<Float>(0.5, 0.5, 0.0, 1.0)  // Dark yellow

    /// Initialize the Metal renderer
    /// - Parameter device: Metal device to use for rendering
    init?(device: MTLDevice) {
        self.device = device

        guard let queue = device.makeCommandQueue() else {
            return nil
        }
        self.commandQueue = queue

        // Load shaders
        guard let library = device.makeDefaultLibrary(),
              let vertexFunction = library.makeFunction(name: "vertexShader"),
              let fragmentFunction = library.makeFunction(name: "fragmentShader") else {
            return nil
        }

        // Create pipeline state
        let pipelineDescriptor = MTLRenderPipelineDescriptor()
        pipelineDescriptor.vertexFunction = vertexFunction
        pipelineDescriptor.fragmentFunction = fragmentFunction
        pipelineDescriptor.colorAttachments[0].pixelFormat = .bgra8Unorm

        do {
            self.pipelineState = try device.makeRenderPipelineState(descriptor: pipelineDescriptor)
        } catch {
            print("Failed to create pipeline state: \(error)")
            return nil
        }

        super.init()
    }

    private var updateCount: UInt64 = 0

    /// Update the frame texture with new pixel data
    /// - Parameters:
    ///   - data: BGRA32 pixel data
    ///   - width: Frame width in pixels
    ///   - height: Frame height in pixels
    ///   - leftBorder: Left border width in pixels
    ///   - rightBorder: Right border width in pixels
    ///   - topBorder: Top border height in pixels
    ///   - bottomBorder: Bottom border height in pixels
    func updateFrame(data: Data, width: Int, height: Int,
                     leftBorder: Int, rightBorder: Int,
                     topBorder: Int, bottomBorder: Int) {
        updateCount += 1
        if updateCount % 50 == 0 {
            NSLog("[MetalRenderer] updateFrame #%llu: %dx%d, borders: L=%d R=%d T=%d B=%d",
                  updateCount, width, height, leftBorder, rightBorder, topBorder, bottomBorder)
        }

        // Store border dimensions
        self.leftBorder = leftBorder
        self.rightBorder = rightBorder
        self.topBorder = topBorder
        self.bottomBorder = bottomBorder

        // Create or recreate texture if dimensions changed
        if frameTexture == nil || textureWidth != width || textureHeight != height {
            let descriptor = MTLTextureDescriptor.texture2DDescriptor(
                pixelFormat: .bgra8Unorm,
                width: width,
                height: height,
                mipmapped: false
            )
            descriptor.usage = [.shaderRead]
            descriptor.storageMode = .shared  // Unified memory - no CPU/GPU sync needed

            guard let texture = device.makeTexture(descriptor: descriptor) else {
                print("Failed to create texture")
                return
            }

            frameTexture = texture
            textureWidth = width
            textureHeight = height
        }

        guard let texture = frameTexture else { return }

        // Upload pixel data to texture
        data.withUnsafeBytes { rawBuffer in
            guard let baseAddress = rawBuffer.baseAddress else { return }
            texture.replace(
                region: MTLRegion(
                    origin: MTLOrigin(x: 0, y: 0, z: 0),
                    size: MTLSize(width: width, height: height, depth: 1)
                ),
                mipmapLevel: 0,
                withBytes: baseAddress,
                bytesPerRow: width * 4
            )
        }
    }
}

// MARK: - MTKViewDelegate
extension MetalRenderer: MTKViewDelegate {
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        drawableSize = size
    }

    func draw(in view: MTKView) {
        guard let drawable = view.currentDrawable,
              let renderPassDescriptor = view.currentRenderPassDescriptor,
              let commandBuffer = commandQueue.makeCommandBuffer(),
              let renderEncoder = commandBuffer.makeRenderCommandEncoder(descriptor: renderPassDescriptor) else {
            return
        }

        renderEncoder.setRenderPipelineState(pipelineState)

        // Bind frame texture if available
        if let texture = frameTexture {
            // Calculate total size including borders
            let totalWidth = Float(leftBorder + textureWidth + rightBorder)
            let totalHeight = Float(topBorder + textureHeight + bottomBorder)

            // Create uniforms for aspect ratio correction and border rendering
            var uniforms = Uniforms(
                drawableSize: SIMD2<Float>(Float(view.drawableSize.width), Float(view.drawableSize.height)),
                textureSize: SIMD2<Float>(Float(textureWidth), Float(textureHeight)),
                totalSize: SIMD2<Float>(totalWidth, totalHeight),
                borderOffset: SIMD2<Float>(Float(leftBorder), Float(topBorder)),
                parScale: parScale,
                padding1: 0,
                padding2: .zero,
                leftBorderColor: leftBorderColor,
                rightBorderColor: rightBorderColor,
                topBorderColor: topBorderColor,
                bottomBorderColor: bottomBorderColor
            )

            renderEncoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.size, index: 0)
            renderEncoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.size, index: 0)
            renderEncoder.setFragmentTexture(texture, index: 0)
            renderEncoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 6)
        }

        renderEncoder.endEncoding()
        commandBuffer.present(drawable)
        commandBuffer.commit()
    }
}
