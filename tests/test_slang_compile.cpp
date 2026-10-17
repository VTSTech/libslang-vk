// libslang-vk M0 unit test — glslang compile path (real library code).
//
// End-to-end check of the glslang FetchContent wiring: a trivial stage pair
// must compile to well-formed SPIR-V (magic number, word alignment), and the
// documented failure paths must return errors instead of crashing.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "slangvk/core/slang_compile.hpp"

using namespace slv::preset;

namespace {

int failures = 0;

void Check(bool condition, const std::string &label)
{
	std::printf("%s %s\n", condition ? "PASS" : "FAIL", label.c_str());
	if (!condition)
		++failures;
}

bool SpvLooksValid(const std::vector<uint8_t> &data)
{
	// SPIR-V magic number 0x07230203, little-endian.
	static constexpr uint8_t kMagic[4] = { 0x03, 0x02, 0x23, 0x07 };
	return data.size() >= 20 && data.size() % 4 == 0 && std::memcmp(data.data(), kMagic, 4) == 0;
}

SlangTranslation MakeTranslation(const char *vertex, const char *fragment)
{
	SlangTranslation translation;
	if (vertex != nullptr) {
		translation.vertex.present = true;
		translation.vertex.glsl = vertex;
	}
	if (fragment != nullptr) {
		translation.fragment.present = true;
		translation.fragment.glsl = fragment;
	}
	return translation;
}

constexpr const char *kTrivialVertex =
    "#version 450\n"
    "void main(void)\n"
    "{\n"
    "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

constexpr const char *kTrivialFragment =
    "#version 450\n"
    "layout(location = 0) out vec4 fragColor;\n"
    "void main(void)\n"
    "{\n"
    "    fragColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
    "}\n";

} // namespace

int main()
{
	// Happy path: both stages compile to valid SPIR-V.
	{
		PassSpirv spirv;
		std::string error;
		const bool ok = CompilePass(MakeTranslation(kTrivialVertex, kTrivialFragment), spirv, error);
		Check(ok, "trivial stage pair compiles");
		if (!ok)
			std::printf("  error: %s\n", error.c_str());
		Check(SpvLooksValid(spirv.vertex), "vertex SPIR-V well-formed");
		Check(SpvLooksValid(spirv.fragment), "fragment SPIR-V well-formed");
	}

	// Missing stage must be a clean error, not a crash.
	{
		PassSpirv spirv;
		std::string error;
		const bool ok = CompilePass(MakeTranslation(kTrivialVertex, nullptr), spirv, error);
		Check(!ok, "missing fragment stage fails cleanly");
		Check(error.find("missing a required stage") != std::string::npos, "missing-stage error is descriptive");
	}

	// Invalid GLSL must surface the glslang log.
	{
		PassSpirv spirv;
		std::string error;
		const bool ok = CompilePass(MakeTranslation("#version 450\nthis is not glsl\n", kTrivialFragment), spirv, error);
		Check(!ok, "invalid GLSL fails cleanly");
		Check(error.find("glslang parse failed") != std::string::npos, "compile error carries the glslang log");
	}

	std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
	return failures == 0 ? 0 : 1;
}
