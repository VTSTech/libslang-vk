// libslang-vk M3 headless test shader: reads the <LutName>Size uniform for
// the declared texture "tint4" (a 4x4 image) and encodes it as the output
// color: rgb-encoded expectation (4, 4, 64, 64) = (4/255, 4/255, 0.25, 0.25)
// through the float -> UNORM store. Pins the LUT size fill in the params
// UBO (w, h, 1/w, 1/h).
#pragma name slvtest_lut_size

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
	vec4 tint4Size;
} params;

void main() {
	FragColor = vec4(params.tint4Size.x / 255.0, params.tint4Size.y / 255.0, params.tint4Size.z,
	    params.tint4Size.w);
}
