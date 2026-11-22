// Implementation of the deterministic `.fe7in` GBA input script parser.

#include "runtime/input_script.h"

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace fe7 {
namespace {

// Maps a single case-insensitive GBA key token to its 10-bit mask.
absl::StatusOr<uint16_t> ParseSingleKey(std::string_view raw_token,
                                        int line_num) {
  const std::string token =
      absl::AsciiStrToUpper(absl::StripAsciiWhitespace(raw_token));
  if (token == "A") return kGbaKeyA;
  if (token == "B") return kGbaKeyB;
  if (token == "SELECT") return kGbaKeySelect;
  if (token == "START") return kGbaKeyStart;
  if (token == "RIGHT") return kGbaKeyRight;
  if (token == "LEFT") return kGbaKeyLeft;
  if (token == "UP") return kGbaKeyUp;
  if (token == "DOWN") return kGbaKeyDown;
  if (token == "R") return kGbaKeyR;
  if (token == "L") return kGbaKeyL;
  if (token == "NONE") return static_cast<uint16_t>(0u);
  return absl::InvalidArgumentError(
      absl::StrCat("Line ", line_num, ": unknown GBA key '", token, "'"));
}

}  // namespace

InputScript::InputScript(std::vector<InputSpan> spans)
    : spans_(std::move(spans)) {}

uint16_t InputScript::KeysForFrame(int frame) const {
  uint16_t mask = 0;
  for (const InputSpan& span : spans_) {
    if (frame >= span.start_frame && frame <= span.end_frame) {
      mask = static_cast<uint16_t>(mask | span.keys);
    }
  }
  return mask;
}

absl::StatusOr<InputScript> ParseInputScript(std::string_view text) {
  std::vector<InputSpan> spans;
  int line_num = 0;

  for (std::string_view raw_line : absl::StrSplit(text, '\n')) {
    ++line_num;
    // Strip optional `#` comment and leading/trailing whitespace.
    const size_t hash_pos = raw_line.find('#');
    if (hash_pos != std::string_view::npos) {
      raw_line = raw_line.substr(0, hash_pos);
    }
    const std::string_view line = absl::StripAsciiWhitespace(raw_line);
    if (line.empty()) {
      continue;
    }

    const size_t colon_pos = line.find(':');
    if (colon_pos == std::string_view::npos) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Line ", line_num, ": expected '<frame>[-<end>]: <keys>'"));
    }

    const std::string_view range_part =
        absl::StripAsciiWhitespace(line.substr(0, colon_pos));
    const std::string_view keys_part =
        absl::StripAsciiWhitespace(line.substr(colon_pos + 1));
    if (range_part.empty() || keys_part.empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Line ", line_num, ": empty frame range or key list"));
    }

    int start_frame = 0;
    int end_frame = 0;
    const size_t dash_pos = range_part.find('-');
    if (dash_pos == std::string_view::npos) {
      if (!absl::SimpleAtoi(range_part, &start_frame) || start_frame < 0) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Line ", line_num, ": invalid frame '", range_part, "'"));
      }
      end_frame = start_frame;
    } else {
      const std::string_view start_str =
          absl::StripAsciiWhitespace(range_part.substr(0, dash_pos));
      const std::string_view end_str =
          absl::StripAsciiWhitespace(range_part.substr(dash_pos + 1));
      if (!absl::SimpleAtoi(start_str, &start_frame) ||
          !absl::SimpleAtoi(end_str, &end_frame) || start_frame < 0 ||
          end_frame < start_frame) {
        return absl::InvalidArgumentError(absl::StrCat(
            "Line ", line_num, ": invalid frame range '", range_part, "'"));
      }
    }

    uint16_t keys_mask = 0;
    for (std::string_view key_tok : absl::StrSplit(keys_part, '+')) {
      auto key_or = ParseSingleKey(key_tok, line_num);
      if (!key_or.ok()) {
        return key_or.status();
      }
      keys_mask = static_cast<uint16_t>(keys_mask | *key_or);
    }

    spans.push_back(InputSpan{start_frame, end_frame, keys_mask});
  }

  return InputScript(std::move(spans));
}

absl::StatusOr<InputScript> LoadInputScript(std::string_view path) {
  std::ifstream file{std::string(path)};
  if (!file.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("Failed to open input script: ", path));
  }
  std::ostringstream ss;
  ss << file.rdbuf();
  return ParseInputScript(ss.str());
}

}  // namespace fe7
