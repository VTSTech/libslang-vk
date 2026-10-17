// libslang-vk M3 headless test shader: samples the declared LUT "tint"
// 1:1 (nearest) — the golden contract is output == LUT texels byte-exact.
#pragma name slvtest_lut

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
uniform sampler2D tint;

void main() {
	FragColor = texture(tint, vTexCoord);
}
