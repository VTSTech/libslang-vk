// libslang-vk runner regression canary (field round 5, crt-beans-s-video):
// a push-constant block that ENDS with float parameters whose names end in
// "Size" — the calculate_widths.slang shape (MaxSpotSize/MinSpotSize).
//
// The runner's uniform fill matches "<TextureName>Size" by suffix; the old
// code claimed these parameter names as texture sizes (texName "SpotScale"),
// zeroed them, and — as block-final floats — wrote the semantic's y component
// 4 bytes past the staging buffer (ASAN heap-buffer-overflow in WriteFloat,
// heap corruption + abort at exit on native builds).
//
// The fix pins BOTH halves: parameters fall through to the parameter path
// (pragma defaults fill them) and writes clamp to the declared std140 size.
// R = SpotScaleMax * 64 - 1 = 95, G = SpotScaleMin * 64 - 1 = 47 at the
// pragma defaults; exact in fp32, byte-exact through the UNORM store.
#pragma name slvtest_sizeparam

#pragma parameter SpotScaleMax "Spot scale max" 1.5 0.0 4.0 0.05
#pragma parameter SpotScaleMin "Spot scale min" 0.75 0.0 4.0 0.05

layout(push_constant) uniform Push
{
        vec4 SourceSize;
        float SpotScaleMax;
        float SpotScaleMin;
} params;

layout(std140, set = 0, binding = 0) uniform UBO
{
        mat4 MVP;
} global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main() {
        gl_Position = global.MVP * Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main() {
        // Read the block fields so SPIR-V dead-code elimination cannot strip
        // them (an unused member hides the fill entirely). Outputs are scaled
        // into 0..1 so the UNORM store keeps them byte-exact: 95 -> 95/255.
        vec3 src = texture(Source, vTexCoord).rgb;
        FragColor = vec4((params.SpotScaleMax * 64.0 - 1.0) / 255.0, (params.SpotScaleMin * 64.0 - 1.0) / 255.0,
            src.b * 0.0 + params.SourceSize.z * 0.0, 1.0);
}
