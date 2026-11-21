#ifndef FE7_RUNTIME_ROM_LOADER_H_
#define FE7_RUNTIME_ROM_LOADER_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fe7 {

inline constexpr size_t kExpectedRomSize = 16u * 1024u * 1024u;
inline constexpr std::string_view kExpectedFe7UsSha1 =
    "c735fdbb9e8abe19e0c6a44708df19acc962e204";

struct RomImage {
  std::vector<uint8_t> bytes;
  std::string sha1_hex;
  bool is_synthetic = false;
};

// Computes lowercase hex SHA-1 digest of `data`.
std::string ComputeSha1Hex(std::span<const uint8_t> data);

// Validates that `data` is a 16 MiB FE7 US (`AE7E`) ROM matching kExpectedFe7UsSha1.
bool ValidateRetailRomBytes(std::span<const uint8_t> data, std::string* sha1_out,
                            std::string* error_out);

// Loads a retail ROM from `path` (any filename) and validates size + SHA-1.
bool LoadRomFile(const std::string& path, RomImage* out, std::string* error_out);

// Loads an arbitrary synthetic ROM buffer for unit/mock tests (bypasses SHA-1 check).
bool LoadSyntheticRom(std::span<const uint8_t> data, RomImage* out, std::string* error_out);

}  // namespace fe7

#endif  // FE7_RUNTIME_ROM_LOADER_H_
