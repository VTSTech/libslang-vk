// Written by VTSTech (Nigel Todman) (veritas@vts-tech.org)
// AI Assisted by Super-Z (GLM-5.3-Flash)
// License: MIT

// libslang-vk — SHA-256 (FIPS 180-4), internal. See sha256.hpp.

#include "sha256.hpp"

#include <cstring>

namespace slv::core {
namespace {

constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t Ror(uint32_t value, uint32_t bits)
{
        return (value >> bits) | (value << (32 - bits));
}

/// The SHA-256 compression function: one 64-byte block into the state.
void Compress(uint32_t state[8], const uint8_t block[64])
{
        uint32_t w[64];
        for (size_t i = 0; i < 16; ++i) {
                w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16)
                        | (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (size_t i = 16; i < 64; ++i) {
                const uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
                const uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (size_t i = 0; i < 64; ++i) {
                const uint32_t S1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
                const uint32_t ch = (e & f) ^ (~e & g);
                const uint32_t temp1 = h + S1 + ch + kRoundConstants[i] + w[i];
                const uint32_t S0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
                const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const uint32_t temp2 = S0 + maj;

                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
}

} // namespace

auto Sha256(const void *data, size_t size, uint8_t out[32]) -> void
{
        uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

        const auto *bytes = static_cast<const uint8_t *>(data);
        size_t offset = 0;
        for (; offset + 64 <= size; offset += 64)
                Compress(state, bytes + offset);

        // The final block(s): the remainder, the 0x80 terminator, the
        // 64-bit big-endian length.
        uint8_t tail[128] = {};
        const size_t rest = size - offset;
        if (rest > 0)
                std::memcpy(tail, bytes + offset, rest);
        tail[rest] = 0x80;
        const size_t tailSize = (rest < 56) ? 64 : 128;
        const uint64_t bitLength = static_cast<uint64_t>(size) * 8;
        for (size_t i = 0; i < 8; ++i)
                tail[tailSize - 1 - i] = static_cast<uint8_t>((bitLength >> (8 * i)) & 0xFF);
        for (size_t i = 0; i < tailSize; i += 64)
                Compress(state, tail + i);

        for (size_t i = 0; i < 8; ++i) {
                out[i * 4 + 0] = static_cast<uint8_t>((state[i] >> 24) & 0xFF);
                out[i * 4 + 1] = static_cast<uint8_t>((state[i] >> 16) & 0xFF);
                out[i * 4 + 2] = static_cast<uint8_t>((state[i] >> 8) & 0xFF);
                out[i * 4 + 3] = static_cast<uint8_t>(state[i] & 0xFF);
        }
}

auto Sha256Hex(const void *data, size_t size) -> std::string
{
        uint8_t digest[32];
        Sha256(data, size, digest);
        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (const uint8_t byte : digest) {
                out.push_back(kHex[byte >> 4]);
                out.push_back(kHex[byte & 0x0F]);
        }
        return out;
}

auto Sha256Hex(const std::string &text) -> std::string
{
        return Sha256Hex(text.data(), text.size());
}

} // namespace slv::core
