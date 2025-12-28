#include <metal_stdlib>
using namespace metal;

// Uniforms passed from CPU
struct Uniforms {
    float2 drawableSize;    // Size of the drawable in pixels
    float2 textureSize;     // Size of the texture (736x576)
    float parScale;         // Pixel Aspect Ratio scale (0.96 for BBC)
};

// Vertex data for a full-screen quad
struct VertexOut {
    float4 position [[position]];
    float2 texCoord;
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
vertex VertexOut vertexShader(uint vertexID [[vertex_id]],
                               constant Uniforms& uniforms [[buffer(0)]]) {
    // Calculate the display aspect ratio with PAR correction
    // BBC pixels are parScale (0.96) as wide as they are tall
    float contentWidth = uniforms.textureSize.x * uniforms.parScale;
    float contentHeight = uniforms.textureSize.y;
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

    VertexOut out;
    out.position = float4(unitQuad[vertexID] * scale, 0, 1);
    out.texCoord = quadTexCoords[vertexID];
    return out;
}

// Fragment shader: sample the emulator framebuffer texture
fragment float4 fragmentShader(VertexOut in [[stage_in]],
                                texture2d<float> texture [[texture(0)]]) {
    // Use nearest for magnification (sharp pixels when enlarged)
    // Use linear for minification (blend when shrunk) to prevent thin features
    // like the 2-scanline cursor from being skipped at certain window sizes
    constexpr sampler textureSampler(mag_filter::nearest,
                                      min_filter::linear,
                                      address::clamp_to_edge);
    return texture.sample(textureSampler, in.texCoord);
}
