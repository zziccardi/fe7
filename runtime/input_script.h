// Deterministic GBA button-input script parser for headless TAS and E2E
// lockstep runs (`//runtime:fe7_headless --input_script=<path>`).
//
// Script syntax (one directive per non-empty line, `#` starts a comment):
//   <frame>: <KEY>[+<KEY>...]
//   <start>-<end>: <KEY>[+<KEY>...]   (inclusive frame range)
//
// Supported GBA key names (case-insensitive):
//   A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L, NONE.
// Overlapping ranges are combined with bitwise OR.

#ifndef FE7_RUNTIME_INPUT_SCRIPT_H_
#define FE7_RUNTIME_INPUT_SCRIPT_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "absl/status/statusor.h"

namespace fe7 {

// Standard 10-bit GBA KEYINPUT active-high button masks passed to
// `hal::StepFrame(uint16_t keys)`.
inline constexpr uint16_t kGbaKeyA = 1u << 0;
inline constexpr uint16_t kGbaKeyB = 1u << 1;
inline constexpr uint16_t kGbaKeySelect = 1u << 2;
inline constexpr uint16_t kGbaKeyStart = 1u << 3;
inline constexpr uint16_t kGbaKeyRight = 1u << 4;
inline constexpr uint16_t kGbaKeyLeft = 1u << 5;
inline constexpr uint16_t kGbaKeyUp = 1u << 6;
inline constexpr uint16_t kGbaKeyDown = 1u << 7;
inline constexpr uint16_t kGbaKeyR = 1u << 8;
inline constexpr uint16_t kGbaKeyL = 1u << 9;

// Inclusive frame span `[start_frame, end_frame]` holding `keys`.
struct InputSpan {
  int start_frame = 0;
  int end_frame = 0;
  uint16_t keys = 0;
};

// Parsed input script mapping any 0-based video frame index to a 10-bit GBA
// button mask.
class InputScript {
 public:
  InputScript() = default;
  explicit InputScript(std::vector<InputSpan> spans);

  // Returns the bitwise OR of all spans covering `frame` (0 if none).
  uint16_t KeysForFrame(int frame) const;

  // Returns all parsed spans in script order.
  const std::vector<InputSpan>& spans() const { return spans_; }

 private:
  std::vector<InputSpan> spans_;
};

// Parses an in-memory `.fe7in` script string into an `InputScript`.
absl::StatusOr<InputScript> ParseInputScript(std::string_view text);

// Loads and parses a `.fe7in` input script from disk.
absl::StatusOr<InputScript> LoadInputScript(std::string_view path);

}  // namespace fe7

#endif  // FE7_RUNTIME_INPUT_SCRIPT_H_
