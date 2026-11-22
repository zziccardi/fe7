#include "runtime/rom_loader.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace fe7 {
namespace {

// 32-bit left rotation used throughout SHA-1 message schedule expansion and compression rounds.
inline uint32_t Rol32(uint32_t value, uint32_t bits) {
  return (value << bits) | (value >> (32u - bits));
}

// Compresses one 64-byte (512-bit) SHA-1 message block into `state` per FIPS 180-4.
void Sha1ProcessBlock(const uint8_t* block, std::array<uint32_t, 5>& state) {
  uint32_t w[80];
  // Decode the 16 input words in big-endian byte order as mandated by SHA-1.
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }
  // Expand the 16 words to 80 schedule words using the FIPS 180-4 XOR + 1-bit rotate recurrence.
  for (int i = 16; i < 80; ++i) {
    w[i] = Rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];

  // Execute the four 20-round stages, each with its standard boolean mixing function and constant.
  for (int i = 0; i < 80; ++i) {
    uint32_t f = 0;
    uint32_t k = 0;
    if (i < 20) {
      f = (b & c) | ((~b) & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }
    const uint32_t temp = Rol32(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = Rol32(b, 30);
    b = a;
    a = temp;
  }

  // Feed-forward (Davies-Meyer) accumulation into the running digest state.
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}

}  // namespace

std::string ComputeSha1Hex(std::span<const uint8_t> data) {
  // Standard FIPS 180-4 SHA-1 initial hash values (H0..H4).
  std::array<uint32_t, 5> state = {
      0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u,
  };

  // Process all complete 64-byte blocks directly from the input span without copying.
  const size_t full_blocks = data.size() / 64u;
  for (size_t i = 0; i < full_blocks; ++i) {
    Sha1ProcessBlock(data.data() + i * 64u, state);
  }

  // Copy the trailing partial block into a zeroed 128-byte scratch buffer and append the 0x80
  // end-of-message bit marker required by SHA-1 padding.
  std::array<uint8_t, 128> tail{};
  const size_t rem = data.size() % 64u;
  if (rem > 0) {
    std::copy_n(data.data() + full_blocks * 64u, rem, tail.data());
  }
  tail[rem] = 0x80u;

  // If fewer than 8 bytes remain after the 0x80 byte (rem >= 56), the 64-bit bit-length footer
  // spills into a second 64-byte block.
  const size_t pad_blocks = (rem >= 56u) ? 2u : 1u;
  const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8ull;
  const size_t len_offset = pad_blocks * 64u - 8u;
  // Write the original message bit length in big-endian order in the final 8 bytes.
  for (int i = 0; i < 8; ++i) {
    tail[len_offset + static_cast<size_t>(i)] =
        static_cast<uint8_t>((bit_len >> ((7 - i) * 8)) & 0xFFu);
  }

  for (size_t i = 0; i < pad_blocks; ++i) {
    Sha1ProcessBlock(tail.data() + i * 64u, state);
  }

  // Serialize the 5 big-endian 32-bit digest words into a 40-character lowercase hex string.
  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(40, '0');
  for (size_t i = 0; i < 5; ++i) {
    for (size_t j = 0; j < 8; ++j) {
      out[i * 8 + j] = kHex[(state[i] >> ((7 - j) * 4)) & 0xFu];
    }
  }
  return out;
}

absl::Status ValidateRetailRomBytes(std::span<const uint8_t> data,
                                    std::string* absl_nullable sha1_out) {
  // Reject wrong-sized files before hashing so oversized or truncated inputs fail immediately.
  if (data.size() != kExpectedRomSize) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Invalid ROM size: expected 16777216 bytes (16 MiB), got ", data.size(), " bytes"));
  }

  const std::string sha1 = ComputeSha1Hex(data);
  if (sha1_out != nullptr) {
    *sha1_out = sha1;
  }
  if (sha1 != kExpectedFe7UsSha1) {
    return absl::InvalidArgumentError(
        absl::StrCat("Invalid ROM SHA-1: expected ", kExpectedFe7UsSha1, ", got ", sha1));
  }
  return absl::OkStatus();
}

absl::StatusOr<RomImage> LoadRomFile(std::string_view path) {
  // Open positioned at EOF (ios::ate) so tellg() yields the file size for a single-pass read.
  std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return absl::NotFoundError(absl::StrCat("Failed to open ROM file: ", path));
  }

  const std::ifstream::pos_type file_size = file.tellg();
  if (file_size < 0) {
    return absl::InternalError(absl::StrCat("Failed to determine ROM file size: ", path));
  }

  std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
  file.seekg(0, std::ios::beg);
  if (!buffer.empty() &&
      !file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()))) {
    return absl::InternalError(absl::StrCat("Failed to read ROM file: ", path));
  }

  std::string sha1;
  if (absl::Status status = ValidateRetailRomBytes(buffer, &sha1); !status.ok()) {
    return status;
  }

  RomImage out;
  out.bytes = std::move(buffer);
  out.sha1_hex = std::move(sha1);
  out.is_synthetic = false;
  return out;
}

absl::StatusOr<RomImage> LoadSyntheticRom(std::span<const uint8_t> data) {
  // Enforce the GBA's 32 MiB Game Pak ROM ceiling so GbaPtr/mGBA bounds invariants remain valid.
  if (data.empty() || data.size() > 32u * 1024u * 1024u) {
    return absl::InvalidArgumentError("Synthetic ROM size must be between 1 byte and 32 MiB");
  }

  RomImage out;
  out.bytes.assign(data.begin(), data.end());
  out.sha1_hex = ComputeSha1Hex(out.bytes);
  out.is_synthetic = true;
  return out;
}

}  // namespace fe7
