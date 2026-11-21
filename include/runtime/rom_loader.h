// Retail FE7 US (AE7E) ROM loading, SHA-1 verification, and synthetic test ROM support.
//
// No copyrighted ROM assets live in this repository; users supply their own FE7 US ROM at runtime.
// This module validates retail ROMs strictly by byte size and SHA-1 digest (independent of
// filename) and provides a synthetic ROM entry point for unit tests and differential harnesses.

#ifndef FE7_RUNTIME_ROM_LOADER_H_
#define FE7_RUNTIME_ROM_LOADER_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fe7 {

// Expected byte size of a clean FE7 US (AE7E) retail ROM (16 MiB).
inline constexpr size_t kExpectedRomSize = 16u * 1024u * 1024u;

// Expected lowercase hex SHA-1 digest of the FE7 US (AE7E) retail ROM.
inline constexpr std::string_view kExpectedFe7UsSha1 =
    "c735fdbb9e8abe19e0c6a44708df19acc962e204";

// Owned in-memory ROM image produced by LoadRomFile() or LoadSyntheticRom().
struct RomImage {
  std::vector<uint8_t> bytes;  // Raw ROM contents mapped at 0x08000000
  std::string sha1_hex;        // 40-character lowercase hex SHA-1 digest of `bytes`
  bool is_synthetic = false;   // True if loaded via LoadSyntheticRom() (unverified test ROM)
};

// Computes the 40-character lowercase hex SHA-1 digest of `data` (FIPS 180-4).
std::string ComputeSha1Hex(std::span<const uint8_t> data);

// Validates that `data` matches kExpectedRomSize and kExpectedFe7UsSha1.
// Writes the computed digest to *sha1_out (if non-null) and a human-readable reason to *error_out
// (if non-null) on failure. Returns true if valid.
bool ValidateRetailRomBytes(std::span<const uint8_t> data, std::string* sha1_out,
                            std::string* error_out);

// Reads a retail ROM from `path` (regardless of filename) and validates its size and SHA-1 digest.
// Populates *out on success; writes a diagnostic message to *error_out (if non-null) on failure.
bool LoadRomFile(const std::string& path, RomImage* out, std::string* error_out);

// Wraps an arbitrary 1-byte..32-MiB buffer as a synthetic RomImage for unit/mock tests, bypassing
// the retail size and SHA-1 check while still computing its digest.
bool LoadSyntheticRom(std::span<const uint8_t> data, RomImage* out, std::string* error_out);

}  // namespace fe7

#endif  // FE7_RUNTIME_ROM_LOADER_H_
