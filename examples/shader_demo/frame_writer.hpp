// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

#pragma once

// libslang-vk shader_demo — PNG output (headless frame dumps, screenshots).
//
// stb_image_write.h is vendored alongside stb_image.h (public domain; see
// PROVENANCE.md). The implementation TU defines STB_IMAGE_WRITE_IMPLEMENTATION
// before including this header.

#include <cstdint>
#include <string>
#include <vector>

#include "stb_image_write.h"

namespace demo {

/**
 * @brief Writes tightly packed RGBA8 pixels as a PNG.
 *
 * BGRA targets (B8G8R8A8_UNORM swapchains) are swizzled first. The input
 * image is left untouched; overwrites the file when it exists.
 */
inline bool WritePng(const std::string &path, const uint8_t *pixels, uint32_t width, uint32_t height, bool bgra,
    std::string &err)
{
        std::vector<uint8_t> rgba;
        if (bgra) {
                const size_t count = size_t(width) * height;
                rgba.resize(count * 4);
                for (size_t i = 0; i < count; ++i) {
                        rgba[i * 4 + 0] = pixels[i * 4 + 2];
                        rgba[i * 4 + 1] = pixels[i * 4 + 1];
                        rgba[i * 4 + 2] = pixels[i * 4 + 0];
                        rgba[i * 4 + 3] = pixels[i * 4 + 3];
                }
                pixels = rgba.data();
        }
        if (stbi_write_png(path.c_str(), int(width), int(height), 4, pixels, int(width) * 4) == 0) {
                err = "stb_image_write failed for " + path;
                return false;
        }
        return true;
}

} // namespace demo
