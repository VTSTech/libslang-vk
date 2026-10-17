// libslang-vk — Preset implementation (M1): orchestrates the GPU-agnostic
// core (parse -> translate -> glslang compile) into a load-ready artifact.
//
// The shader root follows the core layer's contract (SetShaderRootDir, plan
// §5): hosts configure it once at startup. For headless convenience, when
// the root still points at the default non-existent "shaders" folder the
// preset's own directory is adopted implicitly.

#include <algorithm>
#include <filesystem>
#include <unordered_set>

#include "vk_internal.hpp"
#include "impls.hpp"

namespace slv {

namespace {

auto MergeParameters(const Preset::Impl &impl) -> std::vector<ParameterInfo>
{
        // Parameters are preset-global (RetroArch semantics): union by name in
        // first-seen order; pragma defaults, then preset override values.
        std::vector<ParameterInfo> out;
        for (const SlangTranslation &translation : impl.translations) {
                for (const slv::preset::ParamInfo &param : translation.parameters) {
                        bool found = false;
                        for (ParameterInfo &existing : out)
                                if (existing.name == param.name) {
                                        found = true;
                                        break;
                                }
                        if (!found) {
                                ParameterInfo info;
                                info.name = param.name;
                                info.description = param.description;
                                info.defaultValue = param.defaultValue;
                                info.minimum = param.minimum;
                                info.maximum = param.maximum;
                                info.step = param.step;
                                out.push_back(std::move(info));
                        }
                }
        }
        // Surface preset overrides as the "effective default" for UIs; the
        // parser lowercases .slangp keys, so match names case-insensitively.
        for (ParameterInfo &info : out)
                for (const slv::preset::ParamOverride &override : impl.parsed.parameters)
                        if (vk::CaseInsensitiveEqual(override.name, info.name))
                                info.defaultValue = override.value;
        return out;
}

} // namespace

auto Preset::load(Device & /*device*/, const std::filesystem::path &slangp, const PresetLoadOptions &options)
    -> std::expected<Preset, Error>
{
        namespace fs = std::filesystem;
        namespace core = slv::preset;

        if (slangp.empty())
                return std::unexpected(SLV_ERROR(Errc::invalid_argument, "Preset::load: empty path"));

        // Headless convenience: adopt the preset's directory as shader root when
        // the default root does not exist (hosts configure it explicitly).
        if (!fs::exists(core::ShaderRootDir())) {
                std::error_code ec;
                const fs::path parent = fs::absolute(slangp, ec).parent_path();
                if (!ec && !parent.empty())
                        core::SetShaderRootDir(parent.generic_string());
        }

        Preset out;
        out.impl_ = std::make_unique<Impl>();
        Impl &impl = *out.impl_;

        // 1. Parse the .slangp (order-independent, RetroArch semantics).
        std::string parseError;
        if (!core::ParsePreset(slangp.generic_string(), impl.parsed, parseError))
                return std::unexpected(SLV_ERROR(Errc::preset_parse, parseError));

        // Sampler name universe: LUT texture names + pass aliases.
        std::unordered_set<std::string> knownTextureNames;
        for (const core::TextureDef &texture : impl.parsed.textures)
                knownTextureNames.insert(texture.name);
        for (const core::PassDef &pass : impl.parsed.passes)
                if (!pass.alias.empty())
                        knownTextureNames.insert(pass.alias);

        // LUT images are an M2 capability: declared textures are recorded and
        // size-resolvable, but pixel upload is not wired yet (plan §8/§11).
        if (!impl.parsed.textures.empty() && !options.noLutImages)
                return std::unexpected(SLV_ERROR(Errc::unsupported_feature,
                    "Preset::load: LUT texture image upload lands in M2; pass noLutImages for compile-only sweeps"));

        // 2. Per pass: translate to stage GLSL, compile to SPIR-V.
        const int passCount = static_cast<int>(impl.parsed.passes.size());
        impl.passDefs = impl.parsed.passes;
        impl.translations.resize(impl.passDefs.size());
        impl.spirv.resize(impl.passDefs.size());
        for (int index = 0; index < passCount; ++index) {
                const core::PassDef &passDef = impl.passDefs[static_cast<size_t>(index)];
                const std::string shaderPath = core::ResolvePresetFilePath(impl.parsed.dir, passDef.shaderPath);

                SlangTranslation &translation = impl.translations[static_cast<size_t>(index)];
                core::TranslateSlang(shaderPath, knownTextureNames, index, passCount, translation);
                if (!translation.ok())
                        return std::unexpected(SLV_ERROR(
                            translation.error.find("cycle") != std::string::npos ? Errc::include_cycle
                                                                                 : Errc::shader_compile,
                            "pass " + std::to_string(index) + " '" + passDef.shaderPath + "': " + translation.error));
                if (options.strict && !translation.warnings.empty()) {
                        std::string joined;
                        for (const std::string &warning : translation.warnings) {
                                if (!joined.empty())
                                        joined += "; ";
                                joined += warning;
                        }
                        return std::unexpected(
                            SLV_ERROR(Errc::shader_compile, "strict load, pass " + std::to_string(index) + ": " + joined));
                }

                PassSpirv &spirv = impl.spirv[static_cast<size_t>(index)];
                std::string compileError;
                if (!core::CompilePass(translation, spirv, compileError))
                        return std::unexpected(SLV_ERROR(Errc::shader_compile,
                            "pass " + std::to_string(index) + " '" + passDef.shaderPath + "': " + compileError));
        }

        impl.parameters = MergeParameters(impl);
        return out;
}

Preset::~Preset() = default;
Preset::Preset(Preset &&) noexcept = default;
auto Preset::operator=(Preset &&) noexcept -> Preset & = default;

auto Preset::passCount() const -> uint32_t { return static_cast<uint32_t>(impl_->passDefs.size()); }
auto Preset::parameterInfo() const -> std::span<const ParameterInfo> { return impl_->parameters; }

} // namespace slv
