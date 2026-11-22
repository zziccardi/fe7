// Pre-call execution trace recorder and binary serializer for differential
// verification.
//
// Captures real GBA call contexts (target ROM address, r0..r3, first 4 stack
// arguments, and full EWRAM + IWRAM snapshots) when the fallback CPU branches
// to watched addresses during headless replay. The differential harness replays
// these exact snapshots against both original Thumb code and decompiled C++20
// implementations.

#ifndef FE7_RUNTIME_TRACE_RECORDER_H_
#define FE7_RUNTIME_TRACE_RECORDER_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace fe7 {

// Size of GBA EWRAM (256 KiB) and IWRAM (32 KiB) captured in each snapshot.
inline constexpr size_t kEwramSize = 0x40000u;
inline constexpr size_t kIwramSize = 0x8000u;

// Complete pre-call CPU/RAM snapshot for a single GBA function invocation.
struct TraceRecord {
  uint32_t gba_addr = 0;
  std::array<uint32_t, 4> regs{};
  std::array<uint32_t, 4> stack_args{};
  std::vector<uint8_t> ewram;  // kEwramSize bytes
  std::vector<uint8_t> iwram;  // kIwramSize bytes
};

// Configures the set of ROM addresses to watch and the maximum number of trace
// records to capture. Clears any previously recorded traces. Passing an empty
// span disables trace recording with zero hot-path overhead.
void ConfigureTraceRecorder(std::span<const uint32_t> target_addrs,
                            size_t max_records);

// Returns true if trace recording is currently active and has remaining budget.
bool IsTraceRecordingActive();

// Records a pre-call snapshot if `pc` (normalized by clearing bit 0) is in the
// configured target set and gMem backing pointers are initialized.
void MaybeRecordTrace(uint32_t pc, uint32_t r0, uint32_t r1, uint32_t r2,
                      uint32_t r3, const uint32_t* absl_nullable stack_args);

// Returns the currently recorded traces in capture order.
const std::vector<TraceRecord>& GetRecordedTraces();

// Clears watched targets and releases recorded trace memory.
void ResetTraceRecorder();

// Serializes `records` to a compact binary `.fe7t` file at `path`.
absl::Status WriteTraceFile(std::string_view path,
                            std::span<const TraceRecord> records);

// Deserializes trace records from a `.fe7t` file at `path`.
absl::StatusOr<std::vector<TraceRecord>> ReadTraceFile(std::string_view path);

}  // namespace fe7

#endif  // FE7_RUNTIME_TRACE_RECORDER_H_
