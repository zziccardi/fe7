#include "runtime/rom_loader.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>

namespace fe7 {
namespace {

inline uint32_t Rol32(uint32_t value, uint32_t bits) {
  return (value << bits) | (value >> (32u - bits));
}

void Sha1ProcessBlock(const uint8_t* block, std::array<uint32_t, 5>& state) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4 + 0]) << 24) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (int i = 16; i < 80; ++i) {
    w[i] = Rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];

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

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}

}  // namespace

std::string ComputeSha1Hex(std::span<const uint8_t> data) {
  std::array<uint32_t, 5> state = {
      0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u,
  };

  const size_t full_blocks = data.size() / 64u;
  for (size_t i = 0; i < full_blocks; ++i) {
    Sha1ProcessBlock(data.data() + i * 64u, state);
  }

  std::array<uint8_t, 128> tail{};
  const size_t rem = data.size() % 64u;
  if (rem > 0) {
    std::copy_n(data.data() + full_blocks * 64u, rem, tail.data());
  }
  tail[rem] = 0x80u;

  const size_t pad_blocks = (rem >= 56u) ? 2u : 1u;
  const uint64_t bit_len = static_cast<uint64_t>(data.size()) * 8ull;
  const size_t len_offset = pad_blocks * 64u - 8u;
  for (int i = 0; i < 8; ++i) {
    tail[len_offset + static_cast<size_t>(i)] =
        static_cast<uint8_t>((bit_len >> ((7 - i) * 8)) & 0xFFu);
  }

  for (size_t i = 0; i < pad_blocks; ++i) {
    Sha1ProcessBlock(tail.data() + i * 64u, state);
  }

  static constexpr char kHex[] = "0123456789abcdef";
  std::string out(40, '0');
  for (size_t i = 0; i < 5; ++i) {
    for (size_t j = 0; j < 8; ++j) {
      out[i * 8 + j] = kHex[(state[i] >> ((7 - j) * 4)) & 0xFu];
    }
  }
  return out;
}

bool ValidateRetailRomBytes(std::span<const uint8_t> data, std::string* sha1_out,
                            std::string* error_out) {
  if (data.size() != kExpectedRomSize) {
    if (error_out != nullptr) {
      *error_out = "Invalid ROM size: expected 16777216 bytes (16 MiB), got " +
                   std::to_string(data.size()) + " bytes";
    }
    return false;
  }

  const std::string sha1 = ComputeSha1Hex(data);
  if (sha1_out != nullptr) {
    *sha1_out = sha1;
  }
  if (sha1 != kExpectedFe7UsSha1) {
    if (error_out != nullptr) {
      *error_out = "Invalid ROM SHA-1: expected " + std::string(kExpectedFe7UsSha1) + ", got " +
                   sha1;
    }
    return false;
  }
  return true;
}

bool LoadRomFile(const std::string& path, RomImage* out, std::string* error_out) {
  if (out == nullptr) {
    if (error_out != nullptr) {
      *error_out = "Output RomImage pointer is null";
    }
    return false;
  }

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    if (error_out != nullptr) {
      *error_out = "Failed to open ROM file: " + path;
    }
    return false;
  }

  const std::ifstream::pos_type file_size = file.tellg();
  if (file_size < 0) {
    if (error_out != nullptr) {
      *error_out = "Failed to determine ROM file size: " + path;
    }
    return false;
  }

  std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
  file.seekg(0, std::ios::beg);
  if (!buffer.empty() &&
      !file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()))) {
    if (error_out != nullptr) {
      *error_out = "Failed to read ROM file: " + path;
    }
    return false;
  }

  std::string sha1;
  if (!ValidateRetailRomBytes(buffer, &sha1, error_out)) {
    return false;
  }

  out->bytes = std::move(buffer);
  out->sha1_hex = std::move(sha1);
  out->is_synthetic = false;
  return true;
}

bool LoadSyntheticRom(std::span<const uint8_t> data, RomImage* out, std::string* error_out) {
  if (out == nullptr) {
    if (error_out != nullptr) {
      *error_out = "Output RomImage pointer is null";
    }
    return false;
  }
  if (data.empty() || data.size() > 32u * 1024u * 1024u) {
    if (error_out != nullptr) {
      *error_out = "Synthetic ROM size must be between 1 byte and 32 MiB";
    }
    return false;
  }

  out->bytes.assign(data.begin(), data.end());
  out->sha1_hex = ComputeSha1Hex(out->bytes);
  out->is_synthetic = true;
  return true;
}

}  // namespace fe7
