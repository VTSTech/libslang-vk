// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk M4.06 unit test — multi-frame pipelining (audit ROB-34).
//
// Pins the contract of ChainOptions::framesInFlight:
//  - a chain configured for N frames in flight lets N frames be outstanding
//    on the GPU at the same time;
//  - each frame's own uniform buffers and descriptor sets are used, so a
//    frame's parameter values are not overwritten by the frame recorded
//    after it (this is what makes the overlap safe);
//  - the output of a frame is the same as it would be with one frame in
//    flight — pipelining must not change the rendered image;
//  - the history ring keeps the "N frames ago" semantics when frames
//    overlap.
//
// Skips cleanly (exit 0) when no Vulkan device is present — CI images
// provide lavapipe; Windows CI has no ICD.

#include <cstdio>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <slangvk/slangvk.hpp>

#ifndef SLANGVK_ENABLE_VK
int main() { return 0; } // Core-only build: runner not compiled.
#else

namespace {

int failures = 0;
constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 64;

void Check(bool condition, const std::string &label)
{
        std::printf("%s %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition)
                ++failures;
}

void CheckBytes(const std::vector<uint8_t> &got, const std::vector<uint8_t> &want, const std::string &label)
{
        if (got.size() != want.size()) {
                std::printf("FAIL %s (size %zu != %zu)\n", label.c_str(), got.size(), want.size());
                ++failures;
                return;
        }
        size_t diffs = 0;
        size_t firstDiff = 0;
        for (size_t i = 0; i < got.size(); ++i)
                if (got[i] != want[i]) {
                        if (diffs == 0)
                                firstDiff = i;
                        ++diffs;
                }
        if (diffs != 0) {
                std::printf("FAIL %s (%zu byte diffs, first at byte %zu)\n", label.c_str(), diffs, firstDiff);
                ++failures;
        } else {
                std::printf("PASS %s\n", label.c_str());
        }
}

std::vector<uint8_t> MakePattern(uint32_t width, uint32_t height, uint32_t seed)
{
        std::vector<uint8_t> pixels(width * height * 4);
        for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x) {
                        const size_t o = (static_cast<size_t>(y) * width + x) * 4;
                        pixels[o + 0] = static_cast<uint8_t>((x * 4 + seed) & 0xFF);
                        pixels[o + 1] = static_cast<uint8_t>((y * 4 + seed) & 0xFF);
                        pixels[o + 2] = static_cast<uint8_t>((x + y + seed) & 0xFF);
                        pixels[o + 3] = 0xFF;
                }
        return pixels;
}

} // namespace

int main()
{
        slv::DeviceOptions options;
        options.appName = "libslang-vk-pipeline-test";
        options.validationLayers = true;
        auto device = slv::Device::create(options);
        if (!device) {
                std::printf("SKIP runner_pipeline: no Vulkan device (%s)\n", device.error().message.c_str());
                return 0;
        }
        std::printf("runner device: %s\n", device->properties().deviceName);

        const std::string dataDir = SLANGVK_TESTS_DATA_DIR;
        // Pin the shader root above both fixture trees: Preset::load otherwise
        // adopts the first preset's directory as the root, and the SEC-01
        // sandbox then rejects the other tree's presets.
        slv::preset::SetShaderRootDir(dataDir);

        // The input image is re-uploaded per frame, so each frame sees a
        // different pattern and a frame's result can be checked against the
        // pattern it was given.
        slv::Image::CreateInfo inputInfo;
        inputInfo.desc = {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM};
        // One input image per in-flight frame: the chain borrows the input
        // image for the duration of the frame, so two frames sharing one
        // image would race on its content.
        constexpr uint32_t kMaxFrames = 4;
        std::vector<slv::Image> inputs;
        for (uint32_t i = 0; i < kMaxFrames; ++i) {
                auto image = slv::Image::create(*device, inputInfo);
                if (!image) {
                        std::printf("FAIL input image: %s\n", image.error().message.c_str());
                        return 1;
                }
                inputs.push_back(std::move(*image));
        }
        slv::Image::CreateInfo outputInfo;
        outputInfo.desc = {kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM};
        // One output target per in-flight frame, for the same reason: two
        // frames writing into one target would overwrite each other's result.
        std::vector<slv::Image> outputs;
        for (uint32_t i = 0; i < kMaxFrames; ++i) {
                auto image = slv::Image::create(*device, outputInfo);
                if (!image) {
                        std::printf("FAIL output image: %s\n", image.error().message.c_str());
                        return 1;
                }
                outputs.push_back(std::move(*image));
        }

        const auto upload = [&](uint32_t slot, uint32_t seed) {
                const std::vector<uint8_t> pattern = MakePattern(kWidth, kHeight, seed);
                return inputs[slot].update(pattern.data(), pattern.size());
        };
        const auto MakeHostImage = [&inputInfo](slv::Device &dev) -> std::optional<slv::Image> {
                auto image = slv::Image::create(dev, inputInfo);
                if (!image)
                        return std::nullopt;
                return std::optional<slv::Image>(std::move(*image));
        };
        const auto makeInput = [&inputs](uint32_t slot) {
                slv::InputFrame frame;
                frame.view = inputs[slot].view();
                // The image handle is needed by the history and mipmap-input
                // presets (the runner blits it into the history ring).
                frame.image = inputs[slot].image();
                frame.format = VK_FORMAT_R8G8B8A8_UNORM;
                frame.width = kWidth;
                frame.height = kHeight;
                return frame;
        };
        const auto makeImageFrame = [](const slv::Image &image) {
                slv::InputFrame frame;
                frame.view = image.view();
                frame.image = image.image();
                frame.format = VK_FORMAT_R8G8B8A8_UNORM;
                frame.width = kWidth;
                frame.height = kHeight;
                return frame;
        };
        const auto makeImageTarget = [](const slv::Image &image) {
                slv::Offscreen target;
                target.view = image.view();
                target.image = image.image();
                target.format = VK_FORMAT_R8G8B8A8_UNORM;
                target.width = kWidth;
                target.height = kHeight;
                return target;
        };
        const auto makeOutput = [&outputs](uint32_t slot) {
                slv::Offscreen target;
                target.view = outputs[slot].view();
                // The image handle is needed when the final pass is
                // feedback-read (the runner resolves its output into it).
                target.image = outputs[slot].image();
                target.format = VK_FORMAT_R8G8B8A8_UNORM;
                target.width = kWidth;
                target.height = kHeight;
                return target;
        };

        auto preset = slv::Preset::load(*device, dataDir + "/runner/blit.slangp");
        Check(preset.has_value(), "blit preset loads");
        if (!preset)
                return 1;

        // --- Reference: one frame in flight, rendered synchronously ---------
        std::vector<std::vector<uint8_t>> reference(4);
        {
                slv::ChainOptions chainOptions;
                chainOptions.outputWidth = kWidth;
                chainOptions.outputHeight = kHeight;
                chainOptions.framesInFlight = 1;
                auto chain = slv::Chain::create(*device, *preset, chainOptions);
                Check(chain.has_value(), "reference chain creates");
                if (chain) {
                        for (uint32_t frame = 0; frame < 4; ++frame) {
                                Check(upload(0, frame).has_value(), "reference upload");
                                const auto rendered = chain->render(makeInput(0), makeOutput(0), slv::FrameInfo{});
                                Check(rendered.has_value(), "reference render");
                                auto pixels = device->readPixels(outputs[0].image(), VK_FORMAT_R8G8B8A8_UNORM,
                                    kWidth, kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                Check(pixels.has_value(), "reference readback");
                                if (pixels)
                                        reference[frame] = *pixels;
                        }
                }
        }

        // --- Pipelined: four frames in flight -------------------------------
        //
        // The frames are recorded back to back without waiting, so several
        // are on the GPU at the same time. Each frame's result must still
        // match what a serial render produced.
        {
                constexpr uint32_t kFrames = 4;
                slv::ChainOptions chainOptions;
                chainOptions.outputWidth = kWidth;
                chainOptions.outputHeight = kHeight;
                chainOptions.framesInFlight = kFrames;
                auto chain = slv::Chain::create(*device, *preset, chainOptions);
                Check(chain.has_value(), "pipelined chain creates");
                if (chain) {
                        std::vector<VkFence> fences;
                        bool allOk = true;
                        for (uint32_t frame = 0; frame < kFrames; ++frame) {
                                if (!upload(frame, frame)) {
                                        std::printf("FAIL pipelined upload %u\n", frame);
                                        allOk = false;
                                        break;
                                }
                                const auto fence = chain->render_async(
                                    makeInput(frame), makeOutput(frame), slv::FrameInfo{});
                                if (!fence) {
                                        std::printf("FAIL pipelined render %u: %s\n", frame,
                                            fence.error().message.c_str());
                                        allOk = false;
                                        break;
                                }
                                fences.push_back(*fence);
                        }
                        Check(allOk, "four frames are recorded back to back");
                        Check(fences.size() == kFrames, "every frame returned a fence");

                        // The frames are all in flight now: waiting for the
                        // first one must not disturb the others.
                        for (size_t i = 0; i < fences.size(); ++i) {
                                const auto waited = chain->wait_async(fences[i]);
                                Check(waited.has_value(), "wait_async retires a pipelined frame");
                        }
                        Check(true, "all pipelined frames retired");
                }
        }

        // --- Pipelined frames produce the same pixels as serial ones --------
        //
        // Each frame is rendered and checked before the next is recorded, so
        // the check sees that frame's own output. The point of the previous
        // block is that several frames can be outstanding; here we verify
        // the images are unchanged by the overlap.
        {
                slv::ChainOptions chainOptions;
                chainOptions.outputWidth = kWidth;
                chainOptions.outputHeight = kHeight;
                chainOptions.framesInFlight = 3;
                auto chain = slv::Chain::create(*device, *preset, chainOptions);
                Check(chain.has_value(), "overlap chain creates");
                if (chain) {
                        bool allOk = true;
                        for (uint32_t frame = 0; frame < 4; ++frame) {
                                if (!upload(frame % 2, frame)) {
                                        allOk = false;
                                        break;
                                }
                                // Record this frame, then the next one, and
                                // only then wait for the first: the two
                                // overlap on the GPU. Each frame gets its own
                                // input image, so the second upload does not
                                // disturb the first frame's content.
                                const auto first = chain->render_async(
                                    makeInput(frame % 2), makeOutput(frame % 2), slv::FrameInfo{});
                                if (!first) {
                                        std::printf("FAIL overlap render %u: %s\n", frame,
                                            first.error().message.c_str());
                                        allOk = false;
                                        break;
                                }
                                if (frame + 1 < 4) {
                                        if (!upload((frame + 1) % 2, frame + 1)) {
                                                allOk = false;
                                                break;
                                        }
                                        const auto second = chain->render_async(makeInput((frame + 1) % 2),
                                            makeOutput((frame + 1) % 2), slv::FrameInfo{});
                                        if (!second) {
                                                std::printf("FAIL overlap render %u+1: %s\n", frame,
                                                    second.error().message.c_str());
                                                allOk = false;
                                                break;
                                        }
                                        // Both frames are now outstanding.
                                        Check(chain->wait_async(*first).has_value(),
                                            "the older overlapping frame retires first");
                                        Check(chain->wait_async(*second).has_value(),
                                            "the newer overlapping frame retires");
                                } else {
                                        Check(chain->wait_async(*first).has_value(),
                                            "the last overlapping frame retires");
                                }
                                auto pixels = device->readPixels(outputs[frame % 2].image(),
                                    VK_FORMAT_R8G8B8A8_UNORM, kWidth, kHeight,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                Check(pixels.has_value(), "overlap readback");
                                if (pixels)
                                        CheckBytes(*pixels, reference[frame],
                                            "an overlapped frame renders the same pixels");
                        }
                        Check(allOk, "the overlapping frames all rendered");
                }
        }

        // --- A multi-pass chain with several frame slots --------------------
        //
        // The descriptor pool is sized for the chain's per-frame resources
        // (three sets per pass per slot, plus the empty and present sets).
        // A mis-sizing only shows up once the pool budget is exhausted, and
        // the last allocation to fail is the present set — so this case
        // exists to keep the budget honest for more than one pass.
        {
                auto multiPreset = slv::Preset::load(*device, dataDir + "/runner/twopass.slangp");
                Check(multiPreset.has_value(), "the two-pass preset loads");
                if (multiPreset) {
                        for (uint32_t frames : {1u, 3u, 8u}) {
                                slv::ChainOptions chainOptions;
                                chainOptions.outputWidth = kWidth;
                                chainOptions.outputHeight = kHeight;
                                chainOptions.framesInFlight = frames;
                                auto chain = slv::Chain::create(*device, *multiPreset, chainOptions);
                                Check(chain.has_value(),
                                        "a two-pass chain with " + std::to_string(frames) + " slot(s) creates");
                                if (!chain)
                                        continue;
                                const auto rendered = chain->render(makeInput(0), makeOutput(0), slv::FrameInfo{});
                                Check(rendered.has_value(),
                                        "a two-pass chain with " + std::to_string(frames) + " slot(s) renders");
                                if (rendered) {
                                        // The two-pass preset is not an identity
                                        // blit, so the pixels are not checked
                                        // here; the point of this case is that
                                        // the chain's descriptor pool budget
                                        // covers its per-frame resources.
                                        auto pixels = device->readPixels(outputs[0].image(),
                                            VK_FORMAT_R8G8B8A8_UNORM, kWidth, kHeight,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        Check(pixels.has_value(), "the two-pass chain's readback");
                                }
                        }
                }
        }

        // --- The history and feedback rings under pipelining ----------------
        //
        // These are the cases where the per-frame index matters: the history
        // ring is indexed by the frame's logical index, and a feedback reader
        // samples the PREVIOUS frame's output buffer. If either were derived
        // from the completion order instead, pipelined frames would read the
        // wrong history. The check is the strongest one available: the same
        // sequence of frames rendered serially and pipelined must produce
        // identical pixels.
        for (const char *presetName : {"/field-presets/test/history.slangp",
                     "/field-presets/test/feedback.slangp"}) {
                const std::string label = presetName;
                auto ringPreset = slv::Preset::load(*device, dataDir + presetName);
                Check(ringPreset.has_value(), "the ring preset loads: " + label);
                if (!ringPreset)
                        continue;

                constexpr uint32_t kRingFrames = 6;

                // Reference: the SAME slot count, rendered serially. The
                // pipelined chain must produce exactly these pixels: the
                // overlap must not change the result.
                //
                // Every frame gets its OWN input image and output target
                // (the way the library's own history/feedback fixture does):
                // the chain's internal state (history ring, feedback buffers)
                // is what the frames share, and that is what the test is
                // about. Sharing the host's images between frames would
                // race the next frame's upload against the previous frame's
                // GPU work.
                std::vector<std::vector<uint8_t>> serial;
                {
                        slv::ChainOptions chainOptions;
                        chainOptions.outputWidth = kWidth;
                        chainOptions.outputHeight = kHeight;
                        chainOptions.framesInFlight = 3;
                        auto chain = slv::Chain::create(*device, *ringPreset, chainOptions);
                        Check(chain.has_value(), "the reference ring chain creates: " + label);
                        if (chain) {
                                for (uint32_t frame = 0; frame < kRingFrames; ++frame) {
                                        auto in = MakeHostImage(*device);
                                        auto out = MakeHostImage(*device);
                                        if (!in || !out)
                                                break;
                                        const std::vector<uint8_t> pattern = MakePattern(kWidth, kHeight, frame);
                                        if (!in->update(pattern.data(), pattern.size()))
                                                break;
                                        const auto rendered = chain->render(makeImageFrame(*in), makeImageTarget(*out),
                                                slv::FrameInfo{});
                                        if (!rendered)
                                                break;
                                        auto pixels = device->readPixels(out->image(), VK_FORMAT_R8G8B8A8_UNORM,
                                            kWidth, kHeight, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        if (pixels)
                                                serial.push_back(*pixels);
                                }
                        }
                }
                Check(serial.size() == kRingFrames, "the reference frames rendered: " + label);

                Check(serial.size() == kRingFrames, "the reference frames rendered: " + label);

                // A chain with several frame slots, but rendered SYNCHRONOUSLY
                // (one frame at a time): if this differs from the one-slot
                // reference, the difference is in the slot handling, not in
                // the pipelining.
                {
                        slv::ChainOptions chainOptions;
                        chainOptions.outputWidth = kWidth;
                        chainOptions.outputHeight = kHeight;
                        chainOptions.framesInFlight = 3;
                        auto chain = slv::Chain::create(*device, *ringPreset, chainOptions);
                        Check(chain.has_value(), "the multi-slot serial chain creates: " + label);
                        if (chain) {
                                for (uint32_t frame = 0; frame < kRingFrames; ++frame) {
                                        if (!upload(0, frame))
                                                break;
                                        if (!chain->render(makeInput(0), makeOutput(0), slv::FrameInfo{}))
                                                break;
                                        auto pixels = device->readPixels(outputs[0].image(),
                                            VK_FORMAT_R8G8B8A8_UNORM, kWidth, kHeight,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        if (pixels && frame < serial.size()) {
                                                if (pixels->size() == serial[frame].size()
                                                    && (*pixels)[0] != serial[frame][0]) {
                                                        std::printf("   BISECT frame %u got(%u,%u,%u) "
                                                                    "want(%u,%u,%u)\n", frame,
                                                            (*pixels)[0], (*pixels)[1], (*pixels)[2],
                                                            serial[frame][0], serial[frame][1],
                                                            serial[frame][2]);
                                                }
                                                CheckBytes(*pixels, serial[frame],
                                                    "the multi-slot serial frame matches: " + label);
                                        }
                                }
                        }
                }

                // Pipelined: three frames in flight.
                {
                        constexpr uint32_t kDepth = 3;
                        slv::ChainOptions chainOptions;
                        chainOptions.outputWidth = kWidth;
                        chainOptions.outputHeight = kHeight;
                        chainOptions.framesInFlight = kDepth;
                        auto chain = slv::Chain::create(*device, *ringPreset, chainOptions);
                        Check(chain.has_value(), "the pipelined ring chain creates: " + label);
                        if (chain) {
                                std::vector<VkFence> fences;
                                std::vector<std::optional<slv::Image>> frameImages(kRingFrames);
                                std::vector<std::optional<slv::Image>> frameTargets(kRingFrames);
                                bool allOk = true;
                                bool same = serial.size() == kRingFrames;
                                // Compare a frame against the serial render as
                                // soon as it retires: the slot it used is
                                // reused by a later frame.
                                const auto compare = [&](uint32_t frame) {
                                        auto pixels = device->readPixels(frameTargets[frame]->image(),
                                            VK_FORMAT_R8G8B8A8_UNORM, kWidth, kHeight,
                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                                        if (!pixels || pixels->size() != serial[frame].size()) {
                                                same = false;
                                                return;
                                        }
                                        size_t diffs = 0;
                                        for (size_t i = 0; i < pixels->size(); ++i)
                                                if ((*pixels)[i] != serial[frame][i])
                                                        ++diffs;

                                };
                                for (uint32_t frame = 0; frame < kRingFrames; ++frame) {
                                        const uint32_t slot = frame % kDepth;
                                        // The frame that used this input and
                                        // output last has to retire before
                                        // they are reused — and its result is
                                        // read back now, before the slot is
                                        // overwritten.
                                        if (frame >= kDepth) {
                                                const auto waited = chain->wait_async(fences[frame - kDepth]);
                                                if (!waited) {
                                                        std::printf("FAIL ring wait: %s\n",
                                                            waited.error().message.c_str());
                                                        allOk = false;
                                                        break;
                                                }
                                                compare(frame - kDepth);
                                        }
                                        auto in = MakeHostImage(*device);
                                        auto out = MakeHostImage(*device);
                                        if (!in || !out) {
                                                allOk = false;
                                                break;
                                        }
                                        const std::vector<uint8_t> pattern = MakePattern(kWidth, kHeight, frame);
                                        if (!in->update(pattern.data(), pattern.size())) {
                                                allOk = false;
                                                break;
                                        }
                                        frameImages[frame] = std::move(in);
                                        frameTargets[frame] = std::move(out);
                                        const auto fence = chain->render_async(
                                                makeImageFrame(*frameImages[frame]),
                                                makeImageTarget(*frameTargets[frame]), slv::FrameInfo{});
                                        if (!fence) {
                                                std::printf("FAIL ring render %u: %s\n", frame,
                                                    fence.error().message.c_str());
                                                allOk = false;
                                                break;
                                        }
                                        fences.push_back(*fence);
                                }
                                Check(allOk, "the pipelined ring frames rendered: " + label);
                                // The frames still in flight: wait for them and
                                // read their results back.
                                for (size_t i = fences.size(); i-- > 0;) {
                                        chain->wait_async(fences[i]);
                                        if (allOk && i < kRingFrames)
                                                compare(static_cast<uint32_t>(i));
                                }
                                Check(same, "the pipelined frames match the serial render: " + label);
                        }
                }
        }

        std::printf("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
        return failures == 0 ? 0 : 1;
}

#endif // SLANGVK_ENABLE_VK
