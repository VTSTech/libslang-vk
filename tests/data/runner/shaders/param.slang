// libslang-vk M1 headless test shader: multiplies the source by the GAIN
// parameter — pins the params-UBO fill (preset override -> pragma default).
#pragma name slvtest_param

#pragma parameter GAIN "Gain" 1.0 0.0 4.0 0.05

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
	float GAIN;
} params;

uniform sampler2D Source;

void main() {
	FragColor = texture(Source, vTexCoord) * params.GAIN;
}
