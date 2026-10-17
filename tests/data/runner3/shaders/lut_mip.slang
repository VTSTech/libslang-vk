// libslang-vk M3 headless test shader: explicit-LOD sample of the mipmap
// LUT "avg" at level 1. avg.png is 2x2 with a single red corner, so the
// generated 1x1 level-1 texel is (63.75, 0, 0, 255) -> stores as
// (64, 0, 0, 255) — pins the at-upload mip chain generation (SPEC 3.5).
#pragma name slvtest_lut_mip

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
uniform sampler2D avg;

void main() {
	FragColor = textureLod(avg, vTexCoord, 1.0);
}
