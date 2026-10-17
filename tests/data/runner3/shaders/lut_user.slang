// libslang-vk M3 headless test shader: samples the first DECLARED LUT via
// the spec-sanctioned positional alias User0. With textures = "zeta;alpha"
// the golden contract pins declaration-order materialization: User0 must
// bind zeta (tint.png), not the alphabetically-first alpha (avg.png).
#pragma name slvtest_lut_user

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
uniform sampler2D User0;

void main() {
	FragColor = texture(User0, vTexCoord);
}
