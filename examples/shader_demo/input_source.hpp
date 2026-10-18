// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

#pragma once

// libslang-vk shader_demo — input frame sources.
//
// Both sources feed the chain's InputFrame through one slv::Image:
//
//  - PatternSource renders a CPU-animated test pattern, deterministic in the
//    frame index so history/feedback presets show reproducible motion. The
//    buffer is refreshed and uploaded every frame via Image::update — the
//    M4 per-frame host-upload path a game host uses for its converted frame.
//
//  - ImageSource loads a static image with the vendored stb_image.h (the
//    same decode path LUT textures take inside the library) and uploads once.

#include <cstdint>
#include <string>
#include <vector>

#include "stb_image.h"

namespace demo {

/** @brief CPU-rendered animated test pattern (color bars, scrolling stripes,
 * checkerboard, bouncing block). 480x320 matches the bundled testcard.png so
 * switching sources is a content update, not an image recreation. */
class PatternSource {
public:
        static constexpr uint32_t kWidth = 480;
        static constexpr uint32_t kHeight = 320;

        PatternSource() : pixels_(size_t(kWidth) * kHeight * 4) {}

        /** @brief Rasterizes frame `frame` into the CPU buffer (frame 0 = startup look). */
        void Render(uint64_t frame)
        {
                constexpr uint32_t W = kWidth;
                constexpr uint32_t H = kHeight;
                constexpr uint32_t kBandH = H / 3;

                static constexpr uint8_t kBars[8][3] = {
                    {255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                    {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {255, 128, 0}};
                static constexpr uint8_t kStripes[6][3] = {
                    {230, 57, 70}, {241, 196, 15}, {52, 152, 219},
                    {46, 204, 113}, {155, 89, 182}, {236, 240, 241}};

                const uint32_t drift = uint32_t(frame * 3);
                for (uint32_t y = 0; y < H; ++y) {
                        for (uint32_t x = 0; x < W; ++x) {
                                uint8_t r = 0, g = 0, b = 0;
                                if (y < kBandH) {
                                        // Top third: static color bars.
                                        const uint32_t bi = (x * 8) / W;
                                        r = kBars[bi][0];
                                        g = kBars[bi][1];
                                        b = kBars[bi][2];
                                } else if (y < 2 * kBandH) {
                                        // Middle third: scrolling diagonal stripes.
                                        const uint32_t si = ((x + y + drift) / 20) % 6;
                                        r = kStripes[si][0];
                                        g = kStripes[si][1];
                                        b = kStripes[si][2];
                                } else {
                                        // Bottom third: 16px checkerboard.
                                        const bool on = (((x / 16) + (y / 16)) & 1) == 0;
                                        r = on ? 200 : 28;
                                        g = on ? 210 : 32;
                                        b = on ? 230 : 44;
                                }
                                // Grid overlay on the lower two thirds.
                                if (y >= kBandH && (x % 40 == 0 || y % 40 == 0)) {
                                        r = uint8_t(r / 2 + 100);
                                        g = uint8_t(g / 2 + 100);
                                        b = uint8_t(b / 2 + 100);
                                }
                                // Dark frame border (letterbox-friendly edge).
                                if (x < 4 || x >= W - 4 || y < 4 || y >= H - 4) {
                                        r = 16;
                                        g = 16;
                                        b = 16;
                                }
                                const size_t o = (size_t(y) * W + x) * 4;
                                pixels_[o + 0] = r;
                                pixels_[o + 1] = g;
                                pixels_[o + 2] = b;
                                pixels_[o + 3] = 0xFF;
                        }
                }

                // Bouncing block (drawn last): horizontal sweep + vertical triangle wave.
                constexpr uint32_t kBlock = 28;
                const uint32_t bx = uint32_t((frame * 5) % (W - kBlock));
                constexpr uint64_t kPeriod = 180;
                const uint64_t phase = frame % (2 * kPeriod);
                const uint32_t by = uint32_t((phase < kPeriod ? phase : 2 * kPeriod - phase) * (H - kBlock) / kPeriod);
                for (uint32_t y = by; y < by + kBlock; ++y)
                        for (uint32_t x = bx; x < bx + kBlock; ++x) {
                                const bool edge = x - bx < 3 || bx + kBlock - 1 - x < 3 || y - by < 3
                                    || by + kBlock - 1 - y < 3;
                                const size_t o = (size_t(y) * W + x) * 4;
                                pixels_[o + 0] = edge ? 230 : 250;
                                pixels_[o + 1] = edge ? 57 : 250;
                                pixels_[o + 2] = edge ? 70 : 250;
                                pixels_[o + 3] = 0xFF;
                        }
        }

        const uint8_t *Pixels() const { return pixels_.data(); }
        size_t Bytes() const { return pixels_.size(); }

private:
        std::vector<uint8_t> pixels_;
};

/** @brief Static image input decoded through the vendored stb_image.h. */
class ImageSource {
public:
        bool Load(const std::string &path, std::string &err)
        {
                int w = 0, h = 0, comp = 0;
                stbi_uc *decoded = stbi_load(path.c_str(), &w, &h, &comp, 4);
                if (decoded == nullptr) {
                        const char *reason = stbi_failure_reason();
                        err = "stb_image failed for " + path + (reason ? ": " : "") + (reason ? reason : "");
                        return false;
                }
                w_ = uint32_t(w);
                h_ = uint32_t(h);
                pixels_.assign(decoded, decoded + size_t(w) * h * 4);
                stbi_image_free(decoded);
                return true;
        }

        uint32_t Width() const { return w_; }
        uint32_t Height() const { return h_; }
        const uint8_t *Pixels() const { return pixels_.data(); }
        size_t Bytes() const { return pixels_.size(); }

private:
        std::vector<uint8_t> pixels_;
        uint32_t w_ = 0;
        uint32_t h_ = 0;
};

} // namespace demo
