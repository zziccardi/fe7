// Unit tests for the deterministic `.fe7in` GBA input script parser.

#include "runtime/input_script.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace fe7 {
namespace {

TEST(InputScriptTest, ParsesCommentsRangesAndCombinedKeys) {
  constexpr char kScript[] = R"(
    # Boot past logos and press Start
    60-62: START
    100: a + right
    # Overlapping range combines with bitwise OR
    61-63: L+R
    200: NONE
  )";
  auto script_or = ParseInputScript(kScript);
  ASSERT_TRUE(script_or.ok()) << script_or.status();
  const InputScript& script = *script_or;

  EXPECT_EQ(script.KeysForFrame(59), 0u);
  EXPECT_EQ(script.KeysForFrame(60), kGbaKeyStart);
  EXPECT_EQ(script.KeysForFrame(61), kGbaKeyStart | kGbaKeyL | kGbaKeyR);
  EXPECT_EQ(script.KeysForFrame(62), kGbaKeyStart | kGbaKeyL | kGbaKeyR);
  EXPECT_EQ(script.KeysForFrame(63), kGbaKeyL | kGbaKeyR);
  EXPECT_EQ(script.KeysForFrame(64), 0u);
  EXPECT_EQ(script.KeysForFrame(100), kGbaKeyA | kGbaKeyRight);
  EXPECT_EQ(script.KeysForFrame(200), 0u);
}

TEST(InputScriptTest, RejectsMalformedLinesAndUnknownKeys) {
  EXPECT_EQ(ParseInputScript("60 START").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseInputScript("60: INVALID_KEY").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseInputScript("80-60: A").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseInputScript("-5: A").status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ParseInputScript("10: ").status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(InputScriptTest, LoadMissingFileReturnsNotFound) {
  auto res = LoadInputScript("/nonexistent/path/does_not_exist.fe7in");
  EXPECT_EQ(res.status().code(), absl::StatusCode::kNotFound);
}

}  // namespace
}  // namespace fe7
