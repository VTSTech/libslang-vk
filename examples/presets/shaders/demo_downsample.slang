// libslang-vk demo shader: 4-tap box blur used as the glow source.
// Runs at half resolution (scale 0.5, linear filter) as pass 0 of
// demo-glow.slangp; its output is exposed to pass 1 under the alias "blur".
#pragma name demo_downsample

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
} params;

uniform sampler2D Source;

void main() {
	vec2 texel = 1.0 / params.SourceSize.xy;
	vec3 sum = texture(Source, vTexCoord + vec2(-texel.x, -texel.y)).rgb
	    + texture(Source, vTexCoord + vec2(texel.x, -texel.y)).rgb
	    + texture(Source, vTexCoord + vec2(-texel.x, texel.y)).rgb
	    + texture(Source, vTexCoord + vec2(texel.x, texel.y)).rgb;
	FragColor = vec4(sum * 0.25, 1.0);
}
