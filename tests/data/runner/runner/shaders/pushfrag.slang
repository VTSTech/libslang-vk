// libslang-vk runner regression canary: the stock libretro shader shape.
//
// Real field shaders (fxaa.slang, smaa.slang, ...) declare a global MVP UBO
// AND a push-constant block and READ the push block from the fragment stage
// (params.SourceSize and friends). The translator re-homes both blocks into
// set 1 (vertex) / set 3 (fragment) at bindings 0 and 1; the runner's
// pipeline layout must cover BOTH bindings or strict drivers reject the
// pipeline at create time (AMD: VK_ERROR_INVALID_SHADER_NV; lavapipe does
// not validate the shader/pipeline-layout interface, which is why the
// bundled per-stage-UBO canaries never caught this).
//
// SourceSize = vec4(w, h, 1/w, 1/h); SourceSize.z * SourceSize.x == 1.0
// exactly for the 64-wide test target (power of two), so the golden output
// is the input pattern.
#pragma name slvtest_pushfrag

layout(push_constant) uniform Push
{
	vec4 SourceSize;
	vec4 OriginalSize;
	vec4 OutputSize;
	uint FrameCount;
} params;

layout(std140, set = 0, binding = 0) uniform UBO
{
	mat4 MVP;
} global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;

void main() {
	gl_Position = global.MVP * Position;
	vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;

void main() {
	// Read the push block so it survives SPIR-V dead-code elimination
	// (an unused block is stripped by glslang and would hide the bug).
	FragColor = texture(Source, vTexCoord) * (params.SourceSize.z * params.SourceSize.x);
}
