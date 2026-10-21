// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk — SHA-256 (FIPS 180-4), internal.
//
// Used for the content keys that let a host cache the compiled SPIR-V
// (the "cache enabler"): the key must change whenever the compiled output
// would change, so a weak hash is not an option — a collision would serve
// the wrong shader.
//
// Self-contained on purpose: the library has no crypto dependency and
// should not acquire one for a cache key.

#pragma once

#include <cstdint>
#include <string>

namespace slv::core {

/// The SHA-256 digest of `data` (32 bytes).
auto Sha256(const void *data, size_t size, uint8_t out[32]) -> void;

/// The SHA-256 digest of `data`, hex-encoded (64 lowercase hex chars).
auto Sha256Hex(const void *data, size_t size) -> std::string;

/// The SHA-256 digest of `text`, hex-encoded.
auto Sha256Hex(const std::string &text) -> std::string;

} // namespace slv::core
