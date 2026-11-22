// Canonical FE7 US Proc (6C cooperative coroutine) struct layouts.
//
// FE7 runs game logic as a forest of up to 64 cooperative lightweight threads
// ("Procs") organized across 8 root trees. Each Proc occupies a fixed 0x6C-byte
// slot in EWRAM containing a 0x29-byte header followed by subsystem-specific
// state. All embedded pointers use GbaPtr<T> so native C++20 code and the
// fallback ARM7TDMI CPU share identical 32-bit layouts.

#ifndef FE7_GAME_PROC_H_
#define FE7_GAME_PROC_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "runtime/gba_ptr.h"

namespace fe7 {

// Maximum number of simultaneously active Proc slots in gProcs.
inline constexpr int kProcCount = 0x40;

// Number of independent Proc root trees (0 = VSync, 1..7 = main loop).
inline constexpr int kProcTreeCount = 8;

// Root tree sentinel addresses stored in Proc::proc_parent for root procs.
inline constexpr uint32_t kProcTreeVsync = 0;
inline constexpr uint32_t kProcTree1 = 1;
inline constexpr uint32_t kProcTree2 = 2;
inline constexpr uint32_t kProcTree3 = 3;
inline constexpr uint32_t kProcTree4 = 4;
inline constexpr uint32_t kProcTree5 = 5;
inline constexpr uint32_t kProcTree6 = 6;
inline constexpr uint32_t kProcTree7 = 7;

// Bytecode opcodes for ProcScr script instructions (FE7 US).
enum class ProcCmd : int16_t {
  kEnd = 0x00,
  kName = 0x01,
  kCall = 0x02,
  kRepeat = 0x03,
  kOnEnd = 0x04,
  kStartChild = 0x05,
  kStartChildBlocking = 0x06,
  kStartBugged = 0x07,
  kWhileExists = 0x08,
  kEndEach = 0x09,
  kBreakEach = 0x0A,
  kLabel = 0x0B,
  kGoto = 0x0C,
  kGotoScr = 0x0D,
  kSleep = 0x0E,
  kMark = 0x0F,
  kBlock = 0x10,
  kEndIfDup = 0x11,
  kSetFlag2 = 0x12,
  kCmd13 = 0x13,
  kWhile = 0x14,
  kCmd15 = 0x15,
  kCall2 = 0x16,
  kEndDups = 0x17,
  kCallArg = 0x18,
  kCmd19 = 0x19,
};

// Single instruction in a ROM-resident Proc bytecode script.
struct ProcScr {
  int16_t cmd;             // 0x00: ProcCmd opcode
  int16_t imm;             // 0x02: Immediate arg (sleep frames, label, etc.)
  GbaPtr<const void> ptr;  // 0x04: Script, string, or Thumb callback address
};

static_assert(sizeof(ProcScr) == 0x08, "ProcScr must be 8 bytes");
static_assert(offsetof(ProcScr, ptr) == 0x04);
static_assert(std::is_trivially_copyable_v<ProcScr>);

// Base 0x6C-byte Proc instance allocated from EWRAM gProcs[].
// Subsystem-specific Proc structs mirror the 0x00..0x28 header and reinterpret
// bytes 0x29..0x6B via ProcAs<SubProc>().
struct Proc {
  GbaPtr<const ProcScr> proc_script;     // 0x00: Start of bytecode script
  GbaPtr<const ProcScr> proc_script_pc;  // 0x04: Current script instruction
  uint32_t proc_end_func;                // 0x08: Thumb callback run on end
  uint32_t proc_repeat_func;             // 0x0C: Per-frame Thumb callback
  GbaPtr<const char> proc_name;          // 0x10: Debug name string in ROM
  GbaPtr<Proc> proc_parent;              // 0x14: Parent Proc (or tree id 0..7)
  GbaPtr<Proc> proc_child;               // 0x18: Head (most recent) child
  GbaPtr<Proc> proc_next;                // 0x1C: Next sibling
  GbaPtr<Proc> proc_prev;                // 0x20: Previous sibling
  int16_t proc_sleep_clock;              // 0x24: Remaining sleep frames
  uint8_t proc_mark;                     // 0x26: Subsystem mark tag
  uint8_t proc_flags;                    // 0x27: Lifecycle/blocking flags
  uint8_t proc_lock_cnt;                 // 0x28: Nonzero suspends execution
  uint8_t pad_29[3];                     // 0x29: Padding before 4-byte payload
  uint32_t user_data[16];                // 0x2C: Subsystem-specific storage
};

static_assert(sizeof(Proc) == 0x6C, "Proc must be 0x6C bytes");
static_assert(offsetof(Proc, proc_script_pc) == 0x04);
static_assert(offsetof(Proc, proc_end_func) == 0x08);
static_assert(offsetof(Proc, proc_repeat_func) == 0x0C);
static_assert(offsetof(Proc, proc_name) == 0x10);
static_assert(offsetof(Proc, proc_parent) == 0x14);
static_assert(offsetof(Proc, proc_child) == 0x18);
static_assert(offsetof(Proc, proc_next) == 0x1C);
static_assert(offsetof(Proc, proc_prev) == 0x20);
static_assert(offsetof(Proc, proc_sleep_clock) == 0x24);
static_assert(offsetof(Proc, proc_mark) == 0x26);
static_assert(offsetof(Proc, proc_flags) == 0x27);
static_assert(offsetof(Proc, proc_lock_cnt) == 0x28);
static_assert(offsetof(Proc, user_data) == 0x2C);
static_assert(std::is_trivially_copyable_v<Proc>);

}  // namespace fe7

#endif  // FE7_GAME_PROC_H_
