// libslang-vk drill case shader: live-samples 'ghost', a sampler the
// preset never declares — the load must hard-fail.
#pragma name slvdrill_ghost

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
uniform sampler2D ghost;

void main() {
	FragColor = texture(ghost, vTexCoord);
}
