// libslang-vk drill case shader: live-samples PassOutput0 in what the
// preset declares as a 1-pass chain — the causality check must reject the
// load ("slang: PassOutput0 is not causal in pass 0").
#pragma name slvdrill_nc

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
uniform sampler2D PassOutput0;

void main() {
	FragColor = texture(PassOutput0, vTexCoord);
}
