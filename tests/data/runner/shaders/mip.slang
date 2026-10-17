// libslang-vk M2 headless test shader: reads the box average of the input
// via mip level 6 (64x64 -> 1x1). With a mip-capable input the runner
// refills the mip chain every frame (F1); the sampled value is the whole
// image average, independent of texel position.
#pragma name slvtest_mip

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
	FragColor = textureLod(Source, vTexCoord, 6.0);
}
