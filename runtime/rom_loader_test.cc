#include "runtime/rom_loader.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace fe7 {
namespace {

TEST(RomLoaderTest, Sha1MatchesNistVectors) {
  EXPECT_EQ(ComputeSha1Hex({}), "da39a3ee5e6b4b0d3255bfef95601890afd80709");

  const std::array<uint8_t, 3> abc = {'a', 'b', 'c'};
  EXPECT_EQ(ComputeSha1Hex(abc), "a9993e364706816aba3e25717850c26c9cd0d89d");

  const std::string msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  std::vector<uint8_t> msg_bytes(msg.begin(), msg.end());
  EXPECT_EQ(ComputeSha1Hex(msg_bytes), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST(RomLoaderTest, RejectsWrongSizeAndWrongHash) {
  std::vector<uint8_t> too_small(1024, 0xFF);
  std::string sha1;
  absl::Status status = ValidateRetailRomBytes(too_small, &sha1);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("Invalid ROM size"), std::string_view::npos);

  std::vector<uint8_t> right_size_wrong_hash(kExpectedRomSize, 0x00);
  status = ValidateRetailRomBytes(right_size_wrong_hash, &sha1);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("Invalid ROM SHA-1"), std::string_view::npos);
  EXPECT_EQ(sha1.size(), 40u);
}

TEST(RomLoaderTest, LoadRomFileRejectsMissingOrInvalidFileRegardlessOfName) {
  absl::StatusOr<RomImage> missing = LoadRomFile("/nonexistent/arbitrary_name.bin");
  EXPECT_EQ(missing.status().code(), absl::StatusCode::kNotFound);
  EXPECT_NE(missing.status().message().find("Failed to open ROM file"), std::string_view::npos);

  const std::string tmp_path = ::testing::TempDir() + "/custom_fe7_name.dat";
  {
    std::ofstream out(tmp_path, std::ios::binary);
    std::vector<uint8_t> dummy(512, 0xAA);
    out.write(reinterpret_cast<const char*>(dummy.data()),
              static_cast<std::streamsize>(dummy.size()));
  }
  absl::StatusOr<RomImage> bad_size = LoadRomFile(tmp_path);
  EXPECT_EQ(bad_size.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(bad_size.status().message().find("Invalid ROM size"), std::string_view::npos);
  std::remove(tmp_path.c_str());
}

TEST(RomLoaderTest, LoadSyntheticRomAcceptsMockPayload) {
  const std::array<uint8_t, 8> payload = {0x00, 0x00, 0x00, 0xEA, 0x70, 0x47, 0x00, 0x00};
  absl::StatusOr<RomImage> img = LoadSyntheticRom(payload);
  ASSERT_TRUE(img.ok()) << img.status();
  EXPECT_TRUE(img->is_synthetic);
  EXPECT_EQ(img->bytes.size(), payload.size());
  EXPECT_EQ(img->sha1_hex.size(), 40u);
}

}  // namespace
}  // namespace fe7
