// libslang-vk demo shader: brightness + cheap scanline modulation.
//
// Shows the minimal slang conventions the translator expects: #pragma name,
// #pragma parameter, one #pragma stage vertex / #pragma stage fragment pair,
// the MVP UBO in the vertex stage and the sizes UBO in the fragment stage.
// BRIGHTNESS is a #pragma parameter; the translator fills it from the preset
// override (demo-bright.slangp sets 1.25) or the pragma default (1.2).
#pragma name demo_bright

#pragma parameter BRIGHTNESS "Brightness" 1.2 0.5 3.0 0.05

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
	vec4 OutputSize;
	float BRIGHTNESS;
} params;

uniform sampler2D Source;

void main() {
	vec3 rgb = texture(Source, vTexCoord).rgb;
	// Every other output row slightly dimmed — flavor, not conformance.
	float scan = mix(0.85, 1.0, step(0.5, fract(vTexCoord.y * params.OutputSize.y * 0.5)));
	FragColor = vec4(rgb * params.BRIGHTNESS * scan, 1.0);
}
