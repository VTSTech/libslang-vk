// libslang-vk drill case shader: live-samples 'gone', the name of a LUT
// texture whose image file is absent — the load must fail in the LUT stage.
#pragma name slvdrill_gone

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
uniform sampler2D gone;

void main() {
	FragColor = texture(gone, vTexCoord);
}
