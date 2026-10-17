// libslang-vk M4 test shader: exact passthrough (nearest, scale 1).
// Same contract as the M1 runner blit fixture; duplicated so the runner4
// fixture dir is self-contained.
#pragma name slvtest_m4_blit

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
uniform sampler2D Source;

void main() {
	FragColor = texture(Source, vTexCoord);
}
