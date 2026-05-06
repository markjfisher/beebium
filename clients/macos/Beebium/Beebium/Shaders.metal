#include <metal_stdlib>
using namespace metal;

// Per-region display geometry for split-screen modes
struct RegionUniforms {
    uint startLine;     // First scanline (inclusive, 0-based)
    uint endLine;       // Last scanline (exclusive)
    uint pixelWidth;    // Logical pixel width for this region
    uint padding;       // 16-byte alignment
};

// Maximum number of display regions supported
constant uint MAX_REGIONS = 8;

// Uniforms passed from CPU - must match Swift Uniforms struct exactly
// Note: float4 requires 16-byte alignment, so we add explicit padding
struct Uniforms {
    float2 drawableSize;      // offset 0: Size of the drawable in pixels
    float2 textureSize;       // offset 8: Logical texture size (for sampling)
    float2 displaySize;       // offset 16: Display size (for layout, e.g. 640x256)
    float2 totalSize;         // offset 24: Total size including borders
    float2 borderOffset;      // offset 32: Offset of content within total (left, top)
    float parScale;           // offset 40: Pixel Aspect Ratio scale (0.96 for BBC)
    uint interlaced;          // offset 44: 1 if interlaced (MODE 7), 0 if progressive (line-doubled)
    float4 leftBorderColor;   // offset 48: RGBA color for left border
    float4 rightBorderColor;  // offset 64: RGBA color for right border
    float4 topBorderColor;    // offset 80: RGBA color for top border
    float4 bottomBorderColor; // offset 96: RGBA color for bottom border
    uint regionCount;         // Number of active display regions
    float edgeMargin;         // Per-edge margin as fraction (0.02 = 2% per side)
    uint _pad1;
    uint _pad2;
    RegionUniforms regions[MAX_REGIONS];
};

// Vertex data for a full-screen quad
struct VertexOut {
    float4 position [[position]];
    float2 texCoord;          // Normalized coordinates in total area [0,1]
};

// Texture coordinates for the quad (Y flipped for Metal)
constant float2 quadTexCoords[] = {
    float2(0, 1), // bottom-left
    float2(1, 1), // bottom-right
    float2(0, 0), // top-left
    float2(1, 1), // bottom-right
    float2(1, 0), // top-right
    float2(0, 0), // top-left
};

// Unit quad vertices (will be scaled by vertex shader)
constant float2 unitQuad[] = {
    float2(-1, -1), // bottom-left
    float2( 1, -1), // bottom-right
    float2(-1,  1), // top-left
    float2( 1, -1), // bottom-right
    float2( 1,  1), // top-right
    float2(-1,  1), // top-left
};

// Vertex shader: compute aspect-ratio-correct quad with PAR and letterboxing
// Uses totalSize (including borders) for aspect ratio calculation
// Applies line-doubling for non-interlaced modes to match CRT behavior
vertex VertexOut vertexShader(uint vertexID [[vertex_id]],
                               constant Uniforms& uniforms [[buffer(0)]]) {
    // Calculate the display aspect ratio with PAR correction
    // BBC pixels are parScale (0.96) as wide as they are tall
    // Use total size (content + borders) for aspect ratio
    float contentWidth = uniforms.totalSize.x * uniforms.parScale;

    // Line-doubling for non-interlaced modes:
    // - Interlaced MODE 7: ~500 scanlines displayed as-is
    // - Non-interlaced bitmap modes: 256 scanlines line-doubled to ~512
    // This matches physical CRT behavior where both fill the same vertical space
    float contentHeight = uniforms.totalSize.y;
    if (uniforms.interlaced == 0) {
        contentHeight *= 2.0;  // Line-doubling for progressive modes
    }
    float contentAspect = contentWidth / contentHeight;

    // Calculate drawable aspect ratio
    float drawableAspect = uniforms.drawableSize.x / uniforms.drawableSize.y;

    // Calculate scale to fit content in drawable while maintaining aspect ratio
    float2 scale;
    if (drawableAspect > contentAspect) {
        // Drawable is wider than content - pillarbox (black bars on sides)
        scale = float2(contentAspect / drawableAspect, 1.0);
    } else {
        // Drawable is taller than content - letterbox (black bars top/bottom)
        scale = float2(1.0, drawableAspect / contentAspect);
    }

    // Apply per-edge margin: shrink the content rectangle inwards so the
    // window background shows as a thin frame. Clamped defensively so a
    // misconfigured uniform cannot collapse the picture.
    float margin = clamp(uniforms.edgeMargin, 0.0, 0.45);
    scale *= (1.0 - 2.0 * margin);

    VertexOut out;
    out.position = float4(unitQuad[vertexID] * scale, 0, 1);
    out.texCoord = quadTexCoords[vertexID];
    return out;
}

// Per-region horizontal scaling shared by the Debug and Standard fragment
// shaders. Maps a fragment's content-area position into a texture U coordinate,
// honouring split-screen mode bands when present. contentCoord is the fragment
// position relative to the displaySize rectangle origin.
static inline float resolveTextureU(float2 contentCoord,
                                    constant Uniforms& uniforms) {
    float texV = contentCoord.y / uniforms.displaySize.y;
    if (uniforms.regionCount > 1) {
        int scanline = int(texV * uniforms.textureSize.y);
        scanline = clamp(scanline, 0, int(uniforms.textureSize.y) - 1);
        uint regionPixelWidth = uint(uniforms.textureSize.x);  // Fallback
        for (uint i = 0; i < uniforms.regionCount && i < MAX_REGIONS; ++i) {
            if (uint(scanline) >= uniforms.regions[i].startLine &&
                uint(scanline) < uniforms.regions[i].endLine) {
                regionPixelWidth = uniforms.regions[i].pixelWidth;
                break;
            }
        }
        return (contentCoord.x / uniforms.displaySize.x)
             * float(regionPixelWidth) / uniforms.textureSize.x;
    } else {
        return contentCoord.x / uniforms.displaySize.x;
    }
}

// Fragment shader: sample the emulator framebuffer texture with border rendering
// Handles scaling from logical texture size to display size (e.g., 320->640 for MODE 1)
// Supports split-screen modes via per-region horizontal scaling
fragment float4 fragmentShader(VertexOut in [[stage_in]],
                                constant Uniforms& uniforms [[buffer(0)]],
                                texture2d<float> texture [[texture(0)]]) {
    // Convert normalized coordinates to pixel coordinates in total area
    float2 pixelCoord = in.texCoord * uniforms.totalSize;

    // Calculate border boundaries using displaySize (scaled dimensions)
    float leftEdge = uniforms.borderOffset.x;
    float topEdge = uniforms.borderOffset.y;
    float rightEdge = leftEdge + uniforms.displaySize.x;
    float bottomEdge = topEdge + uniforms.displaySize.y;

    // Determine which region we're in
    bool inLeftBorder = pixelCoord.x < leftEdge;
    bool inRightBorder = pixelCoord.x >= rightEdge;
    bool inTopBorder = pixelCoord.y < topEdge;
    bool inBottomBorder = pixelCoord.y >= bottomEdge;

    // Render borders with distinct colors (corners use vertical border colors)
    if (inTopBorder) {
        return uniforms.topBorderColor;
    }
    if (inBottomBorder) {
        return uniforms.bottomBorderColor;
    }
    if (inLeftBorder) {
        return uniforms.leftBorderColor;
    }
    if (inRightBorder) {
        return uniforms.rightBorderColor;
    }

    // Inside content area - sample texture with scaling
    float2 contentCoord = pixelCoord - uniforms.borderOffset;
    float texU = resolveTextureU(contentCoord, uniforms);
    float texV = contentCoord.y / uniforms.displaySize.y;
    float2 texUV = float2(texU, texV);

    // Use nearest for magnification (sharp pixels when enlarged)
    // Use linear for minification (blend when shrunk) to prevent thin features
    // like the 2-scanline cursor from being skipped at certain window sizes
    constexpr sampler textureSampler(mag_filter::nearest,
                                      min_filter::linear,
                                      address::clamp_to_edge);
    return texture.sample(textureSampler, texUV);
}

// Standard fragment shader: no debug borders. The vertex shader has already
// fitted displaySize into the drawable, so the fragment is fully inside the
// active pixel area. Per-region horizontal scaling is identical to the debug
// shader.
fragment float4 fragmentShaderStandard(VertexOut in [[stage_in]],
                                        constant Uniforms& uniforms [[buffer(0)]],
                                        texture2d<float> texture [[texture(0)]]) {
    // The Standard style sets totalSize == displaySize and borderOffset == 0,
    // so contentCoord is just texCoord * displaySize.
    float2 contentCoord = in.texCoord * uniforms.displaySize;
    float texU = resolveTextureU(contentCoord, uniforms);
    float texV = contentCoord.y / uniforms.displaySize.y;
    float2 texUV = float2(texU, texV);

    constexpr sampler textureSampler(mag_filter::nearest,
                                      min_filter::linear,
                                      address::clamp_to_edge);
    return texture.sample(textureSampler, texUV);
}
