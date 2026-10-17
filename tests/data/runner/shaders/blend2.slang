// libslang-vk M1 headless test shader: averages Source with an alias tap
// ("pre" = pass 0 output, identical to Source here) — exercises the alias
// binding path the M1 runner must resolve.
#pragma name slvtest_blend2

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
uniform sampler2D pre;

void main() {
	vec4 a = texture(Source, vTexCoord);
	vec4 b = texture(pre, vTexCoord);
	FragColor = (a + b) * 0.5;
}
