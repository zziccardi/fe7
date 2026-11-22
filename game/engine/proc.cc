#include "game/engine/proc.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/proc.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

GbaPtr<Proc> ProcSlot(int index) { return GbaPtr<Proc>(kProcsAddr) + index; }

GbaPtr<Proc> AllocProc() {
  GbaPtr<GbaPtr<GbaPtr<Proc>>> head_ptr(kProcAllocHeadAddr);
  GbaPtr<GbaPtr<Proc>> head = *head_ptr;
  const GbaPtr<Proc> allocated = *head;
  *head_ptr = head + 1;
  return allocated;
}

void FreeProc(GbaPtr<Proc> proc) {
  GbaPtr<GbaPtr<GbaPtr<Proc>>> head_ptr(kProcAllocHeadAddr);
  GbaPtr<GbaPtr<Proc>> head = *head_ptr + (-1);
  *head_ptr = head;
  *head = proc;
}

void InsertRootProc(GbaPtr<Proc> proc, uint32_t tree_num) {
  GbaPtr<GbaPtr<Proc>> root_slot = GbaPtr<GbaPtr<Proc>>(kProcTreeRootsAddr) +
                                   static_cast<ptrdiff_t>(tree_num);
  const GbaPtr<Proc> existing = *root_slot;
  if (existing) {
    existing->proc_next = proc;
    proc->proc_prev = existing;
  }
  proc->proc_parent = GbaPtr<Proc>(tree_num);
  *root_slot = proc;
}

void InsertChildProc(GbaPtr<Proc> proc, GbaPtr<Proc> parent) {
  if (parent->proc_child) {
    parent->proc_child->proc_next = proc;
    proc->proc_prev = parent->proc_child;
  }
  parent->proc_child = proc;
  proc->proc_parent = parent;
}

void UnlinkProc(GbaPtr<Proc> proc) {
  if (proc->proc_next) {
    proc->proc_next->proc_prev = proc->proc_prev;
  }
  if (proc->proc_prev) {
    proc->proc_prev->proc_next = proc->proc_next;
  }

  // Root procs store their tree index (0..7) in proc_parent; child procs store
  // a valid EWRAM pointer (> 8).
  if (static_cast<int32_t>(proc->proc_parent.addr) > kProcTreeCount) {
    if (proc->proc_parent->proc_child == proc) {
      proc->proc_parent->proc_child = proc->proc_prev;
    }
  } else {
    GbaPtr<GbaPtr<Proc>> root_slot =
        GbaPtr<GbaPtr<Proc>>(kProcTreeRootsAddr) +
        static_cast<ptrdiff_t>(proc->proc_parent.addr);
    if (*root_slot == proc) {
      *root_slot = proc->proc_prev;
    }
  }

  proc->proc_next = GbaPtr<Proc>(0);
  proc->proc_prev = GbaPtr<Proc>(0);
}

void EndProcRecursive(GbaPtr<Proc> proc) {
  if (proc->proc_prev) {
    EndProcRecursive(proc->proc_prev);
  }
  if (proc->proc_child) {
    EndProcRecursive(proc->proc_child);
  }

  if ((proc->proc_flags & kProcFlagEnded) != 0u) {
    return;
  }

  if (proc->proc_end_func != 0u) {
    GbaCall(proc->proc_end_func, proc.addr);
  }
  FreeProc(proc);
  proc->proc_script = GbaPtr<const ProcScr>(0);
  proc->proc_repeat_func = 0u;
  proc->proc_flags |= kProcFlagEnded;
  if ((proc->proc_flags & kProcFlagBlocking) != 0u) {
    proc->proc_parent->proc_lock_cnt =
        static_cast<uint8_t>(proc->proc_parent->proc_lock_cnt - 1u);
  }
}

void RunProcRecursive(GbaPtr<Proc> proc) {
  if (proc->proc_prev) {
    RunProcRecursive(proc->proc_prev);
  }

  if (proc->proc_lock_cnt == 0u &&
      (proc->proc_flags & kProcFlagStarting) == 0u) {
    if (proc->proc_repeat_func == 0u) {
      ProcRunScript(proc);
    }
    if (proc->proc_repeat_func != 0u) {
      GbaCall(proc->proc_repeat_func, proc.addr);
    }
    if ((proc->proc_flags & kProcFlagEnded) != 0u) {
      return;
    }
  }

  if (proc->proc_child) {
    RunProcRecursive(proc->proc_child);
  }
}

// Executes a single ProcScr opcode on `proc`. Returns true if the interpreter
// should continue executing subsequent commands in the same frame.
bool ExecProcCmd(GbaPtr<Proc> proc) {
  const GbaPtr<const ProcScr> pc = proc->proc_script_pc;
  const auto cmd = static_cast<ProcCmd>(pc->cmd);
  switch (cmd) {
    case ProcCmd::kEnd:
      ProcEnd(proc);
      return false;

    case ProcCmd::kName:
      proc->proc_name = GbaPtr<const char>(pc->ptr.addr);
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kCall: {
      const uint32_t fn = pc->ptr.addr;
      proc->proc_script_pc = pc + 1;
      GbaCall(fn, proc.addr);
      return true;
    }

    case ProcCmd::kRepeat:
      proc->proc_repeat_func = pc->ptr.addr;
      proc->proc_script_pc = pc + 1;
      return false;

    case ProcCmd::kOnEnd:
      ProcSetEndFunc(proc, pc->ptr.addr);
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kStartChild:
      SpawnProc(GbaPtr<const ProcScr>(pc->ptr.addr), proc.addr);
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kStartChildBlocking:
      SpawnProcLocking(GbaPtr<const ProcScr>(pc->ptr.addr), proc);
      proc->proc_script_pc = pc + 1;
      return false;

    case ProcCmd::kStartBugged:
      // Preserves vanilla FE7's quirk (0x080049D4) of reading the target tree
      // from `proc->proc_sleep_clock` instead of `pc->imm`.
      SpawnProc(
          GbaPtr<const ProcScr>(pc->ptr.addr),
          static_cast<uint32_t>(static_cast<int32_t>(proc->proc_sleep_clock)));
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kWhileExists:
      if (ProcFind(GbaPtr<const ProcScr>(pc->ptr.addr))) {
        return false;
      }
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kEndEach:
      ProcEndEach(GbaPtr<const ProcScr>(pc->ptr.addr));
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kBreakEach:
      ProcBreakEach(GbaPtr<const ProcScr>(pc->ptr.addr));
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kLabel:
    case ProcCmd::kCmd13:
    case ProcCmd::kCmd15:
    case ProcCmd::kCmd19:
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kGoto:
      ProcGoto(proc, pc->imm);
      return true;

    case ProcCmd::kGotoScr:
      ProcGotoScript(proc, GbaPtr<const ProcScr>(pc->ptr.addr));
      return true;

    case ProcCmd::kSleep:
      if (pc->imm != 0) {
        proc->proc_sleep_clock = pc->imm;
        // Store the exact GBA Thumb callback address so un-decompiled ROM code
        // inspecting `proc_repeat_func` still sees the expected function ptr.
        proc->proc_repeat_func = kAddrProcSleepRepeat | 1u;
      }
      proc->proc_script_pc = pc + 1;
      return false;

    case ProcCmd::kMark:
      proc->proc_mark = static_cast<uint8_t>(pc->imm);
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kBlock:
      return false;

    case ProcCmd::kEndIfDup: {
      int count = 0;
      for (int i = 0; i < kProcCount; ++i) {
        if (ProcSlot(i)->proc_script == proc->proc_script) {
          ++count;
        }
      }
      if (count > 1) {
        ProcEnd(proc);
        return false;
      }
      proc->proc_script_pc = pc + 1;
      return true;
    }

    case ProcCmd::kSetFlag2:
      proc->proc_flags |= kProcFlagBit2;
      proc->proc_script_pc = pc + 1;
      return true;

    case ProcCmd::kWhile: {
      const uint32_t fn = pc->ptr.addr;
      proc->proc_script_pc = pc + 1;
      const int8_t yield = static_cast<int8_t>(GbaCall(fn, proc.addr));
      if (yield == 1) {
        proc->proc_script_pc = proc->proc_script_pc + (-1);
        return false;
      }
      return true;
    }

    case ProcCmd::kCall2: {
      const uint32_t fn = pc->ptr.addr;
      proc->proc_script_pc = pc + 1;
      return static_cast<int8_t>(GbaCall(fn, proc.addr)) != 0;
    }

    case ProcCmd::kEndDups: {
      // Matches FE7 US 0x08004B28, which stops after ending the first
      // duplicate slot rather than scanning the rest of gProcs.
      for (int i = 0; i < kProcCount; ++i) {
        const GbaPtr<Proc> other = ProcSlot(i);
        if (other != proc && other->proc_script == proc->proc_script) {
          ProcEnd(other);
          break;
        }
      }
      proc->proc_script_pc = pc + 1;
      return true;
    }

    case ProcCmd::kCallArg: {
      const int16_t arg = pc->imm;
      const uint32_t fn = pc->ptr.addr;
      proc->proc_script_pc = pc + 1;
      const uint32_t res = GbaCall(
          fn, static_cast<uint32_t>(static_cast<int32_t>(arg)), proc.addr);
      return static_cast<int8_t>(res) != 0;
    }
  }
  return false;
}

uint32_t ThunkInitProcs(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  InitProcs();
  return 0u;
}

uint32_t ThunkSpawnProc(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  return SpawnProc(GbaPtr<const ProcScr>(r0), r1).addr;
}

uint32_t ThunkSpawnProcLocking(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack*/) {
  return SpawnProcLocking(GbaPtr<const ProcScr>(r0), GbaPtr<Proc>(r1)).addr;
}

uint32_t ThunkProcEnd(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                      uint32_t /*r3*/,
                      const uint32_t* absl_nullable /*stack*/) {
  ProcEnd(GbaPtr<Proc>(r0));
  return 0u;
}

uint32_t ThunkProcRun(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                      uint32_t /*r3*/,
                      const uint32_t* absl_nullable /*stack*/) {
  ProcRun(GbaPtr<Proc>(r0));
  return 0u;
}

uint32_t ThunkProcBreak(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  ProcBreak(GbaPtr<Proc>(r0));
  return 0u;
}

uint32_t ThunkProcFind(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                       uint32_t /*r3*/,
                       const uint32_t* absl_nullable /*stack*/) {
  return ProcFind(GbaPtr<const ProcScr>(r0)).addr;
}

uint32_t ThunkProcFindNonBlocked(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                 uint32_t /*r3*/,
                                 const uint32_t* absl_nullable /*stack*/) {
  return ProcFindNonBlocked(GbaPtr<const ProcScr>(r0)).addr;
}

uint32_t ThunkProcFindMarked(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  return ProcFindMarked(static_cast<int>(r0)).addr;
}

uint32_t ThunkProcGoto(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                       uint32_t /*r3*/,
                       const uint32_t* absl_nullable /*stack*/) {
  ProcGoto(GbaPtr<Proc>(r0), static_cast<int>(r1));
  return 0u;
}

uint32_t ThunkProcGotoScript(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  ProcGotoScript(GbaPtr<Proc>(r0), GbaPtr<const ProcScr>(r1));
  return 0u;
}

uint32_t ThunkProcMark(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                       uint32_t /*r3*/,
                       const uint32_t* absl_nullable /*stack*/) {
  ProcMark(GbaPtr<Proc>(r0), static_cast<int>(r1));
  return 0u;
}

uint32_t ThunkProcSetEndFunc(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  ProcSetEndFunc(GbaPtr<Proc>(r0), r1);
  return 0u;
}

uint32_t ThunkProcForAll(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                         uint32_t /*r3*/,
                         const uint32_t* absl_nullable /*stack*/) {
  ProcForAll(r0);
  return 0u;
}

uint32_t ThunkProcForEach(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  ProcForEach(GbaPtr<const ProcScr>(r0), r1);
  return 0u;
}

uint32_t ThunkProcForEachMarked(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  ProcForEachMarked(static_cast<int>(r0), r1);
  return 0u;
}

uint32_t ThunkProcLockEachMarked(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                 uint32_t /*r3*/,
                                 const uint32_t* absl_nullable /*stack*/) {
  ProcLockEachMarked(static_cast<int>(r0));
  return 0u;
}

uint32_t ThunkProcReleaseEachMarked(uint32_t r0, uint32_t /*r1*/,
                                    uint32_t /*r2*/, uint32_t /*r3*/,
                                    const uint32_t* absl_nullable /*stack*/) {
  ProcReleaseEachMarked(static_cast<int>(r0));
  return 0u;
}

uint32_t ThunkProcEndEachMarked(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  ProcEndEachMarked(static_cast<int>(r0));
  return 0u;
}

uint32_t ThunkProcEndEach(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  ProcEndEach(GbaPtr<const ProcScr>(r0));
  return 0u;
}

uint32_t ThunkProcBreakEach(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  ProcBreakEach(GbaPtr<const ProcScr>(r0));
  return 0u;
}

uint32_t ThunkProcSleepRepeat(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  ProcSleepRepeat(GbaPtr<Proc>(r0));
  return 0u;
}

uint32_t ThunkProcRunScript(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  ProcRunScript(GbaPtr<Proc>(r0));
  return 0u;
}

}  // namespace

void InitProcs() {
  GbaPtr<GbaPtr<Proc>> alloc_list(kProcAllocListAddr);
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    // Zero only the 0x00..0x28 header fields, matching FE7 US 0x08004420.
    proc->proc_script = GbaPtr<const ProcScr>(0);
    proc->proc_script_pc = GbaPtr<const ProcScr>(0);
    proc->proc_end_func = 0u;
    proc->proc_repeat_func = 0u;
    proc->proc_name = GbaPtr<const char>(0);
    proc->proc_parent = GbaPtr<Proc>(0);
    proc->proc_child = GbaPtr<Proc>(0);
    proc->proc_next = GbaPtr<Proc>(0);
    proc->proc_prev = GbaPtr<Proc>(0);
    proc->proc_sleep_clock = 0;
    proc->proc_mark = 0u;
    proc->proc_flags = 0u;
    proc->proc_lock_cnt = 0u;
    alloc_list[i] = proc;
  }
  alloc_list[kProcCount] = GbaPtr<Proc>(0);
  *GbaPtr<GbaPtr<GbaPtr<Proc>>>(kProcAllocHeadAddr) = alloc_list;

  GbaPtr<GbaPtr<Proc>> roots(kProcTreeRootsAddr);
  for (int i = 0; i < kProcTreeCount; ++i) {
    roots[i] = GbaPtr<Proc>(0);
  }
}

GbaPtr<Proc> SpawnProc(GbaPtr<const ProcScr> script, uint32_t parent) {
  const GbaPtr<Proc> proc = AllocProc();
  proc->proc_script = script;
  proc->proc_script_pc = script;
  proc->proc_end_func = 0u;
  proc->proc_repeat_func = 0u;
  proc->proc_parent = GbaPtr<Proc>(0);
  proc->proc_child = GbaPtr<Proc>(0);
  proc->proc_next = GbaPtr<Proc>(0);
  proc->proc_prev = GbaPtr<Proc>(0);
  proc->proc_sleep_clock = 0;
  proc->proc_mark = 0u;
  proc->proc_lock_cnt = 0u;
  proc->proc_flags = kProcFlagStarting;

  if (static_cast<int32_t>(parent) <= 7) {
    InsertRootProc(proc, parent);
  } else {
    InsertChildProc(proc, GbaPtr<Proc>(parent));
  }

  ProcRunScript(proc);
  proc->proc_flags =
      static_cast<uint8_t>(proc->proc_flags & ~kProcFlagStarting);
  return proc;
}

GbaPtr<Proc> SpawnProcLocking(GbaPtr<const ProcScr> script,
                              GbaPtr<Proc> parent) {
  const GbaPtr<Proc> proc = SpawnProc(script, parent.addr);
  if (!proc->proc_script) {
    return GbaPtr<Proc>(0);
  }
  proc->proc_flags |= kProcFlagBlocking;
  proc->proc_parent->proc_lock_cnt =
      static_cast<uint8_t>(proc->proc_parent->proc_lock_cnt + 1u);
  return proc;
}

void ProcEnd(GbaPtr<Proc> proc) {
  if (!proc) {
    return;
  }
  UnlinkProc(proc);
  EndProcRecursive(proc);
}

void ProcRun(GbaPtr<Proc> root) {
  if (!root) {
    return;
  }
  RunProcRecursive(root);
}

void ProcBreak(GbaPtr<Proc> proc) { proc->proc_repeat_func = 0u; }

GbaPtr<Proc> ProcFind(GbaPtr<const ProcScr> script) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_script == script) {
      return proc;
    }
  }
  return GbaPtr<Proc>(0);
}

GbaPtr<Proc> ProcFindNonBlocked(GbaPtr<const ProcScr> script) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_script == script && proc->proc_lock_cnt == 0u) {
      return proc;
    }
  }
  return GbaPtr<Proc>(0);
}

GbaPtr<Proc> ProcFindMarked(int mark) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_script && proc->proc_mark == static_cast<uint8_t>(mark)) {
      return proc;
    }
  }
  return GbaPtr<Proc>(0);
}

void ProcGoto(GbaPtr<Proc> proc, int label) {
  GbaPtr<const ProcScr> pc = proc->proc_script;
  while (pc->cmd != static_cast<int16_t>(ProcCmd::kEnd)) {
    if (pc->cmd == static_cast<int16_t>(ProcCmd::kLabel) && pc->imm == label) {
      proc->proc_script_pc = pc;
      proc->proc_repeat_func = 0u;
      return;
    }
    pc = pc + 1;
  }
}

void ProcGotoScript(GbaPtr<Proc> proc, GbaPtr<const ProcScr> script) {
  proc->proc_script_pc = script;
  proc->proc_repeat_func = 0u;
}

void ProcMark(GbaPtr<Proc> proc, int mark) {
  proc->proc_mark = static_cast<uint8_t>(mark);
}

void ProcSetEndFunc(GbaPtr<Proc> proc, uint32_t func) {
  proc->proc_end_func = func;
}

void ProcForAll(uint32_t func) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_script) {
      GbaCall(func, proc.addr);
    }
  }
}

void ProcForEach(GbaPtr<const ProcScr> script, uint32_t func) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_script == script) {
      GbaCall(func, proc.addr);
    }
  }
}

void ProcForEachMarked(int mark, uint32_t func) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_mark == static_cast<uint8_t>(mark)) {
      GbaCall(func, proc.addr);
    }
  }
}

void ProcLockEachMarked(int mark) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_mark == static_cast<uint8_t>(mark)) {
      proc->proc_lock_cnt = static_cast<uint8_t>(proc->proc_lock_cnt + 1u);
    }
  }
}

void ProcReleaseEachMarked(int mark) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_mark == static_cast<uint8_t>(mark) &&
        proc->proc_lock_cnt != 0u) {
      proc->proc_lock_cnt = static_cast<uint8_t>(proc->proc_lock_cnt - 1u);
    }
  }
}

void ProcEndEachMarked(int mark) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_mark == static_cast<uint8_t>(mark)) {
      ProcEnd(proc);
    }
  }
}

void ProcEndEach(GbaPtr<const ProcScr> script) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_script == script) {
      ProcEnd(proc);
    }
  }
}

void ProcBreakEach(GbaPtr<const ProcScr> script) {
  for (int i = 0; i < kProcCount; ++i) {
    const GbaPtr<Proc> proc = ProcSlot(i);
    if (proc->proc_script == script) {
      ProcBreak(proc);
    }
  }
}

void ProcSleepRepeat(GbaPtr<Proc> proc) {
  proc->proc_sleep_clock = static_cast<int16_t>(proc->proc_sleep_clock - 1);
  if (proc->proc_sleep_clock == 0) {
    ProcBreak(proc);
  }
}

void ProcRunScript(GbaPtr<Proc> proc) {
  if (!proc->proc_script || proc->proc_lock_cnt != 0u ||
      proc->proc_repeat_func != 0u) {
    return;
  }
  while (ExecProcCmd(proc)) {
    if (!proc->proc_script) {
      break;
    }
  }
}

void RegisterProcFunctions() {
  RegisterNative(kAddrInitProcs, &ThunkInitProcs);
  RegisterNative(kAddrSpawnProc, &ThunkSpawnProc);
  RegisterNative(kAddrSpawnProcLocking, &ThunkSpawnProcLocking);
  RegisterNative(kAddrProcEnd, &ThunkProcEnd);
  RegisterNative(kAddrProcRun, &ThunkProcRun);
  RegisterNative(kAddrProcBreak, &ThunkProcBreak);
  RegisterNative(kAddrProcFind, &ThunkProcFind);
  RegisterNative(kAddrProcFindNonBlocked, &ThunkProcFindNonBlocked);
  RegisterNative(kAddrProcFindMarked, &ThunkProcFindMarked);
  RegisterNative(kAddrProcGoto, &ThunkProcGoto);
  RegisterNative(kAddrProcGotoScript, &ThunkProcGotoScript);
  RegisterNative(kAddrProcMark, &ThunkProcMark);
  RegisterNative(kAddrProcSetEndFunc, &ThunkProcSetEndFunc);
  RegisterNative(kAddrProcForAll, &ThunkProcForAll);
  RegisterNative(kAddrProcForEach, &ThunkProcForEach);
  RegisterNative(kAddrProcForEachMarked, &ThunkProcForEachMarked);
  RegisterNative(kAddrProcLockEachMarked, &ThunkProcLockEachMarked);
  RegisterNative(kAddrProcReleaseEachMarked, &ThunkProcReleaseEachMarked);
  RegisterNative(kAddrProcEndEachMarked, &ThunkProcEndEachMarked);
  RegisterNative(kAddrProcEndEach, &ThunkProcEndEach);
  RegisterNative(kAddrProcBreakEach, &ThunkProcBreakEach);
  RegisterNative(kAddrProcSleepRepeat, &ThunkProcSleepRepeat);
  RegisterNative(kAddrProcRunScript, &ThunkProcRunScript);
}

}  // namespace fe7
