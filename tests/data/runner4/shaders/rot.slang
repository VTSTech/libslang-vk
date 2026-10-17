// libslang-vk M4 test shader: encodes the Rotation uniform (SPEC gap #5,
// uint 0..3) into the output color. Pins the per-stage UBO fill of the
// input frame's rotation.
#pragma name slvtest_m4_rot

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
	uint Rotation;
} params;

void main() {
	FragColor = vec4(vec3(float(params.Rotation) / 3.0), 1.0);
}
