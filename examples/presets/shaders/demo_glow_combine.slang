// libslang-vk demo shader: combine the original frame with the blurred
// pass-0 output (alias "blur") for a cheap glow/bloom look.
//
// Demonstrates the sampler names the translator resolves besides Source:
//   Original  — the input frame of the chain
//   blur      — alias0 of demo-glow.slangp (pass 0 output; here also what
//               Source points at, we sample the alias to exercise the
//               alias binding the Vulkan runner will create)
//   GLOW      — #pragma parameter, overridden to 0.60 by the preset
#pragma name demo_glow_combine

#pragma parameter GLOW "Glow strength" 0.5 0.0 2.0 0.05

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
layout(std140, set = 0, binding = 0) uniform UBO
{
        mat4 MVP;
} global;

void main() {
        gl_Position = global.MVP * Position;
        vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(std140, set = 0, binding = 1) uniform UBO
{
        vec4 SourceSize;
        vec4 OriginalSize;
        vec4 OutputSize;
        float GLOW;
} params;

uniform sampler2D Original;
uniform sampler2D blur;

void main() {
        vec3 base = texture(Original, vTexCoord).rgb;
        vec3 glow = texture(blur, vTexCoord).rgb;
        FragColor = vec4(base + glow * params.GLOW, 1.0);
}
