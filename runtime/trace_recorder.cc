#include "runtime/trace_recorder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

constexpr std::array<char, 4> kTraceMagic = {'F', 'E', '7', 'T'};
constexpr uint32_t kTraceVersion = 1u;

// Wrapped in absl::NoDestructor per Google C++ Style Guide (no non-trivial
// global destructors).
absl::NoDestructor<absl::flat_hash_set<uint32_t>> gTargetAddrs;
absl::NoDestructor<std::vector<TraceRecord>> gRecordedTraces;
size_t gMaxRecords = 0;

template <typename T>
void WritePod(std::ofstream& out, const T& value) {
  out.write(reinterpret_cast<const char*>(&value),
            static_cast<std::streamsize>(sizeof(T)));
}

template <typename T>
bool ReadPod(std::ifstream& in, T* out) {
  return static_cast<bool>(in.read(reinterpret_cast<char*>(out),
                                   static_cast<std::streamsize>(sizeof(T))));
}

}  // namespace

void ConfigureTraceRecorder(std::span<const uint32_t> target_addrs,
                            size_t max_records) {
  gTargetAddrs->clear();
  std::vector<TraceRecord>().swap(*gRecordedTraces);
  gMaxRecords = max_records;
  for (uint32_t addr : target_addrs) {
    // Strip Thumb bit 0 so watched addresses match normalized CPU PCs.
    gTargetAddrs->insert(addr & ~1u);
  }
}

bool IsTraceRecordingActive() {
  return !gTargetAddrs->empty() && gRecordedTraces->size() < gMaxRecords;
}

void MaybeRecordTrace(uint32_t pc, uint32_t r0, uint32_t r1, uint32_t r2,
                      uint32_t r3, const uint32_t* absl_nullable stack_args) {
  if (!IsTraceRecordingActive()) {
    return;
  }
  const uint32_t aligned = pc & ~1u;
  if (!gTargetAddrs->contains(aligned)) {
    return;
  }
  if (gMem.ewram == nullptr || gMem.iwram == nullptr) {
    return;
  }

  TraceRecord rec;
  rec.gba_addr = aligned;
  rec.regs = {r0, r1, r2, r3};
  if (stack_args != nullptr) {
    for (size_t i = 0; i < rec.stack_args.size(); ++i) {
      rec.stack_args[i] = stack_args[i];
    }
  }
  rec.ewram.resize(kEwramSize);
  rec.iwram.resize(kIwramSize);
  std::memcpy(rec.ewram.data(), gMem.ewram, kEwramSize);
  std::memcpy(rec.iwram.data(), gMem.iwram, kIwramSize);
  gRecordedTraces->push_back(std::move(rec));
}

const std::vector<TraceRecord>& GetRecordedTraces() { return *gRecordedTraces; }

void ResetTraceRecorder() {
  gTargetAddrs->clear();
  // Swap with an empty vector to release heap capacity for LSan cleanliness.
  std::vector<TraceRecord>().swap(*gRecordedTraces);
  gMaxRecords = 0;
}

absl::Status WriteTraceFile(std::string_view path,
                            std::span<const TraceRecord> records) {
  std::ofstream out(std::string(path), std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return absl::InternalError(
        absl::StrCat("Failed to open trace file for writing: ", path));
  }

  out.write(kTraceMagic.data(),
            static_cast<std::streamsize>(kTraceMagic.size()));
  WritePod(out, kTraceVersion);
  const uint32_t count = static_cast<uint32_t>(records.size());
  WritePod(out, count);

  for (const TraceRecord& rec : records) {
    if (rec.ewram.size() != kEwramSize || rec.iwram.size() != kIwramSize) {
      return absl::InvalidArgumentError(
          "TraceRecord EWRAM/IWRAM buffers must be exact GBA sizes");
    }
    WritePod(out, rec.gba_addr);
    for (uint32_t r : rec.regs) WritePod(out, r);
    for (uint32_t s : rec.stack_args) WritePod(out, s);
    out.write(reinterpret_cast<const char*>(rec.ewram.data()),
              static_cast<std::streamsize>(kEwramSize));
    out.write(reinterpret_cast<const char*>(rec.iwram.data()),
              static_cast<std::streamsize>(kIwramSize));
  }

  if (!out.good()) {
    return absl::InternalError(
        absl::StrCat("I/O error while writing trace file: ", path));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<TraceRecord>> ReadTraceFile(std::string_view path) {
  std::ifstream in(std::string(path), std::ios::binary);
  if (!in.is_open()) {
    return absl::NotFoundError(
        absl::StrCat("Failed to open trace file: ", path));
  }

  std::array<char, 4> magic{};
  if (!in.read(magic.data(), static_cast<std::streamsize>(magic.size())) ||
      magic != kTraceMagic) {
    return absl::InvalidArgumentError("Invalid trace file magic header");
  }

  uint32_t version = 0;
  uint32_t count = 0;
  if (!ReadPod(in, &version) || version != kTraceVersion ||
      !ReadPod(in, &count)) {
    return absl::InvalidArgumentError("Unsupported or truncated trace header");
  }

  std::vector<TraceRecord> records;
  records.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    TraceRecord rec;
    if (!ReadPod(in, &rec.gba_addr)) {
      return absl::InvalidArgumentError("Truncated trace record");
    }
    for (uint32_t& r : rec.regs) {
      if (!ReadPod(in, &r)) {
        return absl::InvalidArgumentError("Truncated trace registers");
      }
    }
    for (uint32_t& s : rec.stack_args) {
      if (!ReadPod(in, &s)) {
        return absl::InvalidArgumentError("Truncated trace stack args");
      }
    }
    rec.ewram.resize(kEwramSize);
    rec.iwram.resize(kIwramSize);
    if (!in.read(reinterpret_cast<char*>(rec.ewram.data()),
                 static_cast<std::streamsize>(kEwramSize)) ||
        !in.read(reinterpret_cast<char*>(rec.iwram.data()),
                 static_cast<std::streamsize>(kIwramSize))) {
      return absl::InvalidArgumentError("Truncated trace RAM snapshot");
    }
    records.push_back(std::move(rec));
  }

  return records;
}

}  // namespace fe7
