// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk example — preset_info
//
// The M0 "hello world": load a .slangp preset, print its pass chain,
// parameters and textures, then translate and compile every pass to SPIR-V
// exactly the way the Vulkan runner consumes them (translated stage
// GLSL -> glslang -> bytecode with dense set/binding assignments).
//
// Usage:
//   preset_info [path/to/preset.slangp]
//
// With no argument it runs the bundled demo preset. The shader root is set
// to the directory containing the preset — the layout the bundled demos use
// (preset next to a shaders/ folder). Hosts normally point SetShaderRootDir()
// at their runtime shaders folder once at startup instead.

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <slangvk/slangvk.hpp>

namespace {

constexpr const char *kDefaultPreset = SLANGVK_EXAMPLES_DATA_DIR "/demo-bright.slangp";

const char *FilterName(slv::preset::FilterMode mode)
{
	return mode == slv::preset::FilterMode::Linear ? "linear" : "nearest";
}

const char *WrapName(slv::preset::WrapMode mode)
{
	switch (mode) {
		case slv::preset::WrapMode::Repeat:
			return "repeat";
		case slv::preset::WrapMode::MirroredRepeat:
			return "mirrored-repeat";
		case slv::preset::WrapMode::ClampToEdge:
		default:
			return "clamp-to-edge";
	}
}

std::string ScaleText(const slv::preset::ScaleRule &rule)
{
	const char *type = rule.type == slv::preset::ScaleType::Viewport ? "viewport"
	    : rule.type == slv::preset::ScaleType::Absolute                              ? "absolute"
	                                                                                 : "source";
	char buf[64];
	std::snprintf(buf, sizeof(buf), "x%.3g %s", static_cast<double>(rule.scale), type);
	return buf;
}

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

} // namespace

int main(int argc, char *argv[])
{
	const std::string presetArg = argc > 1 ? argv[1] : kDefaultPreset;
	const std::filesystem::path presetPath = std::filesystem::absolute(presetArg);

	std::printf("libslang-vk preset_info\n");
	std::printf("  preset: %s\n\n", presetPath.generic_string().c_str());

	// Sandbox: preset file references must resolve inside this root (SEC-01).
	slv::preset::SetShaderRootDir(presetPath.parent_path().generic_string());

	// -- parse ----------------------------------------------------------------
	slv::preset::Preset preset;
	std::string error;
	if (!slv::preset::ParsePreset(presetPath.generic_string(), preset, error)) {
		std::printf("parse FAILED: %s\n", error.c_str());
		return 1;
	}

	std::printf("preset '%s': %u pass(es), %u texture(s), feedback %s, histories %d\n\n",
	    preset.displayName.c_str(), static_cast<unsigned>(preset.passes.size()),
	    static_cast<unsigned>(preset.textures.size()),
	    preset.feedbackPass < 0 ? "none" : std::to_string(preset.feedbackPass).c_str(), preset.histories);

	for (size_t i = 0; i < preset.passes.size(); ++i) {
		const slv::preset::PassDef &pass = preset.passes[i];
		std::printf("pass %u: %s\n", static_cast<unsigned>(i), pass.shaderPath.c_str());
		std::printf("  filter %s, wrap %s, mipmap %s\n", FilterName(pass.filter), WrapName(pass.wrap),
		    pass.mipmapInput ? "on" : "off");
		std::printf("  scale x: %s, y: %s\n", ScaleText(pass.scaleX).c_str(), ScaleText(pass.scaleY).c_str());
		std::printf("  alias '%s', frame_count_mod %u, format %s%s%s\n\n",
		    pass.alias.empty() ? "-" : pass.alias.c_str(), pass.frameCountMod,
		    pass.srgbFramebuffer ? "srgb" : (pass.floatFramebuffer ? "fp16" : "rgba8"),
		    pass.pragmaFormat.empty() ? "" : " (pragma: ", pass.pragmaFormat.empty() ? "" : ")");
	}

	if (preset.textures.empty()) {
		std::printf("textures: (none)\n");
	} else {
		std::printf("textures:\n");
		for (const slv::preset::TextureDef &tex : preset.textures)
			std::printf("  %s -> %s (%s, %s%s)\n", tex.name.c_str(), tex.path.c_str(), FilterName(tex.filter),
			    WrapName(tex.wrap), tex.mipmap ? ", mipmap" : "");
	}

	// Sampler names the shaders can reference besides the builtins: every
	// LUT texture name plus every pass alias (the M1 runner will bind these).
	std::unordered_set<std::string> knownTextureNames;
	for (const slv::preset::TextureDef &tex : preset.textures)
		knownTextureNames.insert(tex.name);
	for (const slv::preset::PassDef &pass : preset.passes)
		if (!pass.alias.empty())
			knownTextureNames.insert(pass.alias);

	// -- translate + compile --------------------------------------------------
	std::printf("\ntranslating + compiling %u pass(es) to SPIR-V (glslang):\n",
	    static_cast<unsigned>(preset.passes.size()));

	// Preset parameter overrides, keyed case-insensitively (RetroArch-style).
	std::unordered_map<std::string, float> overrides;
	for (const slv::preset::ParamOverride &param : preset.parameters)
		overrides[Lower(param.name)] = param.value;

	unsigned compiled = 0;
	for (size_t i = 0; i < preset.passes.size(); ++i) {
		const slv::preset::PassDef &pass = preset.passes[i];
		const std::string shaderPath = slv::preset::ResolvePresetFilePath(preset.dir, pass.shaderPath);

		slv::preset::SlangTranslation translation;
		slv::preset::TranslateSlang(shaderPath, knownTextureNames, static_cast<int>(i),
		    static_cast<int>(preset.passes.size()), translation);
		if (!translation.ok()) {
			std::printf("pass %u %s: translate FAILED: %s\n", static_cast<unsigned>(i), pass.shaderPath.c_str(),
			    translation.error.c_str());
			return 2;
		}

		slv::preset::PassSpirv spirv;
		if (!slv::preset::CompilePass(translation, spirv, error)) {
			std::printf("pass %u %s: compile FAILED: %s\n", static_cast<unsigned>(i), pass.shaderPath.c_str(),
			    error.c_str());
			return 2;
		}
		++compiled;

		std::printf("pass %u %s: OK\n", static_cast<unsigned>(i), pass.shaderPath.c_str());
		std::printf("  vertex SPIR-V %zu bytes (%zu words), fragment SPIR-V %zu bytes (%zu words)\n",
		    spirv.vertex.size(), spirv.vertex.size() / 4, spirv.fragment.size(), spirv.fragment.size() / 4);

		std::printf("  set 2 textures:");
		for (const slv::preset::TextureRef &tex : translation.fragment.textures)
			std::printf(" %s=%u", tex.name.c_str(), tex.binding);
		std::printf("\n");

		std::printf("  parameters:\n");
		for (const slv::preset::ParamInfo &param : translation.parameters) {
			const auto it = overrides.find(Lower(param.name));
			const float value = it != overrides.end() ? it->second : param.defaultValue;
			std::printf("    %s = %.3f (default %.3f, range %.3f..%.3f step %.3f)%s\n", param.name.c_str(),
			    static_cast<double>(value), static_cast<double>(param.defaultValue),
			    static_cast<double>(param.minimum), static_cast<double>(param.maximum),
			    static_cast<double>(param.step), it != overrides.end() ? " [preset]" : "");
		}
		for (const std::string &warning : translation.warnings)
			std::printf("  warning: %s\n", warning.c_str());
	}

	std::printf("\nresult: %u/%u pass(es) compiled to SPIR-V — ready for the Vulkan runner.\n", compiled,
	    static_cast<unsigned>(preset.passes.size()));
	return compiled == preset.passes.size() ? 0 : 2;
}
