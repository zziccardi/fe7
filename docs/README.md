# FE7 Documentation

This directory documents the Game Boy Advance hardware model, ARM7TDMI Thumb-1
assembly translation patterns, *Fire Emblem: The Blazing Blade* (FE7 US,
`AE7E`) game-engine internals, and the `fe7` native C++20 port architecture.

## Guides

| Document | What It Covers |
| --- | --- |
| [GBA Hardware Reference](gba_hardware.md) | ARM7TDMI CPU states & pipeline, full 32-bit GBA memory map, bus width & alignment rules, PPU/DMA/IRQ/BIOS SWIs, and how `runtime/` (`GbaPtr<T>`, `GbaHal`, `DispatchTable`) bridges them. |
| [Thumb-1 Assembly & C++20 Translation](thumb_assembly.md) | Registers, ARM AAPCS calling convention, Thumb-1 instruction reference, two complete instruction-by-instruction walkthroughs (`RandNextB` and `RandRoll2Rn`), and common GCC-2.9/ARM7TDMI idioms & bit-exactness traps. |
| [FE7 US Engine & Business Logic](fe7_business_logic.md) | Canonical EWRAM/IWRAM/ROM memory map, RNG (1-RN / 2-RN True Hit + LCG), 6C Proc cooperative scheduler, Units/Items/Weapon Ranks, Combat formulas & simulation, EXP/Level-Up/Promotion, Map & BFS pathfinding, Turn/Phase flow, Supports, Enemy AI, and FE7 US vs. FE8 / FE7J differences. |
| [Decompilation & Verification Workflow](decompilation_workflow.md) | Step-by-step guide for translating a Thumb function into Google-style "C-with-types" C++20, writing synthetic GoogleTest unit tests, and verifying 0-mismatch equivalence with `fe7_diff` and `e2e_lockstep.py`. |
| [Implementation Plan](implementation_plan.md) | Master architectural roadmap (Phases 1–5), Ship-of-Theseus dispatch design, and build-graph isolation rules. |

## Related Files in This Repository

- [`tools/agent_rules.md`](../tools/agent_rules.md) — Strict "C-with-types" C++20 rules enforced on `game/` by `tools/check_agent_rules.py` and `clang-tidy`.
- [`runtime/gba_ptr.h`](../runtime/gba_ptr.h) — Bounds- and alignment-checked 32-bit virtual GBA pointer wrapper.
- [`runtime/gba_hal.h`](../runtime/gba_hal.h) — Owned hardware abstraction layer for MMIO, VRAM/Palette/OAM, DMA, and BIOS SWIs.
- [`runtime/dispatch.h`](../runtime/dispatch.h) — Two-way native/fallback function dispatch table.
- [`game/proc.h`](../game/proc.h), [`game/unit.h`](../game/unit.h), [`game/item.h`](../game/item.h), [`game/battle.h`](../game/battle.h) — Human-gated canonical FE7 US POD struct definitions.
