// FE7 US (AE7E) 6C Proc cooperative coroutine scheduler.
//
// Manages the fixed 64-slot Proc pool (`gProcs` at 0x02024E28), free-list
// stack (`gProcAllocList` / `gProcAllocHead`), 8 root trees
// (`gProcTreeRoots`), tree traversal, and the 26-opcode ProcScr bytecode
// interpreter. Callbacks into un-decompiled ROM functions dispatch seamlessly
// through GbaCall().

#ifndef FE7_GAME_ENGINE_PROC_H_
#define FE7_GAME_ENGINE_PROC_H_

#include <cstdint>

#include "game/proc.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed EWRAM addresses of FE7 US Proc engine globals.
inline constexpr uint32_t kProcsAddr = 0x02024E28u;
inline constexpr uint32_t kProcAllocListAddr = 0x02026928u;
inline constexpr uint32_t kProcAllocHeadAddr = 0x02026A2Cu;
inline constexpr uint32_t kProcTreeRootsAddr = 0x02026A30u;

// Bitmask flags stored in Proc::proc_flags (offset 0x27).
inline constexpr uint8_t kProcFlagEnded = 1u << 0;
inline constexpr uint8_t kProcFlagBlocking = 1u << 1;
inline constexpr uint8_t kProcFlagBit2 = 1u << 2;
inline constexpr uint8_t kProcFlagStarting = 1u << 3;

// FE7 US ROM addresses for the Proc scheduler functions.
inline constexpr uint32_t kAddrInitProcs = 0x08004420u;
inline constexpr uint32_t kAddrSpawnProc = 0x08004494u;
inline constexpr uint32_t kAddrSpawnProcLocking = 0x080044F8u;
inline constexpr uint32_t kAddrProcEnd = 0x08004584u;
inline constexpr uint32_t kAddrProcRun = 0x08004690u;
inline constexpr uint32_t kAddrProcBreak = 0x080046A0u;
inline constexpr uint32_t kAddrProcFind = 0x080046A8u;
inline constexpr uint32_t kAddrProcFindNonBlocked = 0x080046C8u;
inline constexpr uint32_t kAddrProcFindMarked = 0x080046F4u;
inline constexpr uint32_t kAddrProcGoto = 0x08004720u;
inline constexpr uint32_t kAddrProcGotoScript = 0x08004758u;
inline constexpr uint32_t kAddrProcMark = 0x08004760u;
inline constexpr uint32_t kAddrProcSetEndFunc = 0x08004768u;
inline constexpr uint32_t kAddrProcForAll = 0x0800476Cu;
inline constexpr uint32_t kAddrProcForEach = 0x08004794u;
inline constexpr uint32_t kAddrProcForEachMarked = 0x080047BCu;
inline constexpr uint32_t kAddrProcLockEachMarked = 0x080047E8u;
inline constexpr uint32_t kAddrProcReleaseEachMarked = 0x0800480Cu;
inline constexpr uint32_t kAddrProcEndEachMarked = 0x08004834u;
inline constexpr uint32_t kAddrProcEndEach = 0x0800486Cu;
inline constexpr uint32_t kAddrProcBreakEach = 0x08004888u;
inline constexpr uint32_t kAddrProcSleepRepeat = 0x08004A84u;
inline constexpr uint32_t kAddrProcRunScript = 0x08004B84u;

// Resets all 64 Proc slots, rebuilds the free list, and clears all 8 root
// trees. Matches FE7 US `InitProcs` (0x08004420).
void InitProcs();

// Allocates a new Proc running `script`, attaches it to root tree `parent`
// (when `parent <= 7`) or as a child of parent Proc `parent`, and executes
// initial synchronous script commands. Matches FE7 US `SpawnProc` (0x08004494).
GbaPtr<Proc> SpawnProc(GbaPtr<const ProcScr> script, uint32_t parent);

// Spawns a child Proc and increments `parent->proc_lock_cnt` until the child
// ends. Returns null if the child ended synchronously during spawn.
// Matches FE7 US `SpawnProcLocking` (0x080044F8).
GbaPtr<Proc> SpawnProcLocking(GbaPtr<const ProcScr> script,
                              GbaPtr<Proc> parent);

// Unlinks `proc` from its tree and recursively ends it and all descendants.
// Matches FE7 US `Proc_End` (0x08004584).
void ProcEnd(GbaPtr<Proc> proc);

// Runs one frame of `root` and its sibling/child subtree in reverse-spawn
// (oldest-sibling-first) order. Matches FE7 US `func_08004690`.
void ProcRun(GbaPtr<Proc> root);

// Clears `proc->proc_repeat_func` so script execution resumes on the next tick.
// Matches FE7 US `Proc_Break` (0x080046A0).
void ProcBreak(GbaPtr<Proc> proc);

// Finds the first active Proc whose `proc_script == script`, or null.
// Matches FE7 US `Proc_Find` (0x080046A8).
GbaPtr<Proc> ProcFind(GbaPtr<const ProcScr> script);

// Finds the first active unblocked (`proc_lock_cnt == 0`) Proc for `script`.
// Matches FE7 US `func_080046C8`.
GbaPtr<Proc> ProcFindNonBlocked(GbaPtr<const ProcScr> script);

// Finds the first active Proc with `proc_mark == mark`.
// Matches FE7 US `func_080046F4`.
GbaPtr<Proc> ProcFindMarked(int mark);

// Jumps `proc->proc_script_pc` to the first `PROC_LABEL(label)` in its script
// and clears `proc_repeat_func`. Matches FE7 US `Proc_Goto` (0x08004720).
void ProcGoto(GbaPtr<Proc> proc, int label);

// Sets `proc->proc_script_pc = script` and clears `proc_repeat_func`.
// Matches FE7 US `func_08004758`.
void ProcGotoScript(GbaPtr<Proc> proc, GbaPtr<const ProcScr> script);

// Sets `proc->proc_mark = mark`. Matches FE7 US `Proc_Mark` (0x08004760).
void ProcMark(GbaPtr<Proc> proc, int mark);

// Sets `proc->proc_end_func = func`. Matches FE7 US `func_08004768`.
void ProcSetEndFunc(GbaPtr<Proc> proc, uint32_t func);

// Invokes Thumb/native callback `func(proc)` for every active Proc.
// Matches FE7 US `func_0800476C`.
void ProcForAll(uint32_t func);

// Invokes `func(proc)` for every Proc with `proc_script == script`.
// Matches FE7 US `Proc_ForEach` (0x08004794).
void ProcForEach(GbaPtr<const ProcScr> script, uint32_t func);

// Invokes `func(proc)` for every Proc slot with `proc_mark == mark`.
// Matches FE7 US `func_080047BC`.
void ProcForEachMarked(int mark, uint32_t func);

// Increments `proc_lock_cnt` for every Proc slot with `proc_mark == mark`.
// Matches FE7 US `Proc_LockEachMarked` (0x080047E8).
void ProcLockEachMarked(int mark);

// Decrements nonzero `proc_lock_cnt` for every Proc slot with `proc_mark ==
// mark`. Matches FE7 US `func_0800480C`.
void ProcReleaseEachMarked(int mark);

// Ends every Proc slot with `proc_mark == mark`. Matches FE7 US
// `func_08004834`.
void ProcEndEachMarked(int mark);

// Ends every Proc with `proc_script == script`.
// Matches FE7 US `Proc_EndEach` (0x0800486C).
void ProcEndEach(GbaPtr<const ProcScr> script);

// Calls ProcBreak() on every Proc with `proc_script == script`.
// Matches FE7 US `func_08004888`.
void ProcBreakEach(GbaPtr<const ProcScr> script);

// Built-in repeat callback for `PROC_SLEEP`: decrements `proc_sleep_clock` and
// breaks when it reaches 0. Matches FE7 US `func_08004A84`.
void ProcSleepRepeat(GbaPtr<Proc> proc);

// Executes synchronous ProcScr bytecode instructions on `proc` until a blocking
// opcode, repeat callback, or end command stops it.
// Matches FE7 US `func_08004B84`.
void ProcRunScript(GbaPtr<Proc> proc);

// Registers all native Proc scheduler functions in the global dispatch table.
void RegisterProcFunctions();

}  // namespace fe7

#endif  // FE7_GAME_ENGINE_PROC_H_
