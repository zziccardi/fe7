# Fire Emblem 7 (US / `AE7E`) Engine & Business Logic Reference

This document summarizes the core engine structures, fixed RAM/ROM tables, and
gameplay formulas of *Fire Emblem: The Blazing Blade* (**FE7 US, `AE7E`**) as
decompiled and differentially verified in `game/`.

---

## 1. Key FE7 US Memory Map (EWRAM, IWRAM & ROM Tables)

### IWRAM (`0x03000000–0x030077FF`)

| Address | Symbol | Type / Size | Role |
| --- | --- | --- | --- |
| `0x03000000` | `gRNSeeds` | `uint16_t[3]` | Primary 48-bit shift-register RNG state |
| `0x03000008` | `gLCGRNValue` | `uint32_t` | Secondary 32-bit LCG state (visuals/misc) |
| `0x030013C0` | `gpAiScoreCoeffs` | `GbaPtr<const AiCombatScoreCoefficients>` | Active AI scoring weight table pointer |
| `0x030041E0` | `gWorkingMap` | `GbaPtr<GbaPtr<uint8_t>>` | Currently active 2D tile map for `MapFill` / BFS |
| `0x030041F0` | `gMapFloodSquareBufB` | `MapFloodSquare[]` | BFS ping-pong queue B |
| `0x030043F0` | `gWorkingMovTable` | `int8_t[65]` | Active unit's terrain movement cost table |
| `0x03004490` | `gMapFloodSquareBufA` | `MapFloodSquare[]` | BFS ping-pong queue A |
| `0x03004690` | `gActiveUnit` | `GbaPtr<Unit>` | Currently acting unit pointer |
| `0x030046A0` | `gMapFloodSt` | `MapFloodState` (`0x0C` B) | Double-buffered BFS movement flood state |

### EWRAM (`0x02000000–0x0203FFFF`)

| Address | Symbol | Type / Size | Role |
| --- | --- | --- | --- |
| `0x02024E28` | `gProcs` | `Proc[64]` (`64 × 0x6C` B) | Fixed pool of cooperative coroutine slots |
| `0x02026928` | `gProcAllocList` | `GbaPtr<Proc>[64]` | Free-list stack of available `Proc` slots |
| `0x02026A2C` | `gProcAllocHead` | `GbaPtr<GbaPtr<Proc>>` | Current top of the `Proc` free-list stack |
| `0x02026A30` | `gProcTreeRoots` | `GbaPtr<Proc>[8]` | 8 independent `Proc` root trees (`0` = VSync, `1..7` = main loop) |
| `0x0202BBB8` | `gBmStatus` | `BmSt` | Map/battle runtime state (`+0x01`: `game_lock`, `+0x04`: mode flags) |
| `0x0202BBF8` | `gPlayStatus` | `PlaySt` | Persistent chapter state (`+0x0D`: vision, `+0x0F`: faction, `+0x10`: turn, `+0x1B`: chapter mode, `+0x2B..0x2D`: Tactician affinity & stars) |
| `0x0202BD48` | `gActiveUnitId` | `uint8_t` | Unit ID of active AI/player unit |
| `0x0202E3D8` | `gMapSize` | `MapSize` (`int16_t x, y`) | Current chapter map dimensions in tiles |
| `0x0202E3DC` | `gMapUnit` | `GbaPtr<GbaPtr<uint8_t>>` | Row-pointer 2D map of unit IDs per tile |
| `0x0202E3E0` | `gMapTerrain` | `GbaPtr<GbaPtr<uint8_t>>` | Row-pointer 2D map of terrain IDs per tile |
| `0x0202E3E4` | `gMapMovement` | `GbaPtr<GbaPtr<uint8_t>>` | Row-pointer 2D map of BFS movement costs |
| `0x0202E3E8` | `gMapRange` | `GbaPtr<GbaPtr<uint8_t>>` | Row-pointer 2D map of attack/staff range |
| `0x0203A3D8` | `gBattleStats` | `BattleStats` (`0x18` B) | Global combat config, range, hit/crit/dmg scratch, Triangle Attack partners |
| `0x0203A3F0` | `gBattleActor` | `BattleUnit` (`0x80` B) | Initiating combatant snapshot + derived stats |
| `0x0203A470` | `gBattleTarget` | `BattleUnit` (`0x80` B) | Defending combatant snapshot + derived stats |
| `0x0203A4F0` | `gBattleHitArray` | `BattleHit[7]` (`28` B) | Unwound combat hit sequence |
| `0x0203A50C` | `gBattleHitIt` | `GbaPtr<BattleHit>` | Write cursor into `gBattleHitArray` |
| `0x0203A8EC` | `gAiState` | `AiState` | Enemy AI working state |
| `0x0203A97C` | `gAiDecision` | `AiDecision` (`0x0C` B) | Chosen AI action, move tile, target, and item slot |

### Static ROM Data Tables (`0x08000000–0x08FFFFFF`)

| Address | Table | Entry Size | Notes |
| --- | --- | --- | --- |
| `0x081C3AC0` | Tactician Affinity Table | `uint32_t[]` | Maps Tactician nibble (`gPlayStatus+0x2B >> 4`) to unit affinity ID |
| `0x081D36F4` | `gAiCombatScoreCoeffTable` | `0x14` B | Weighted AI combat scoring coefficients + 12 class rank bonuses |
| `0x08B92EB0` | `gUnitLookup` | `GbaPtr<Unit>[256]` | Direct O(1) lookup from 8-bit `Unit::id` to EWRAM `Unit*` |
| `0x08B94184` | `gSupportLevelCaps` | `uint8_t[4]` | Support EXP caps (`80`, `160`, `240`, `241`) |
| `0x08B9426C` | `gWeaponTriangleRules` | `4` B | Null-terminated (`attacker_kind < 0`) WTA hit/dmg bonus table |
| `0x08BDCE18` | `gCharacterData` (`PInfo`) | `0x34` B | Static character templates (bases, growths, affinity, supports, attrs) |
| `0x08BE015C` | `gClassData` (`JInfo`) | `0x54` B | Static class templates (bases, caps, promotion, terrain/move tables) |
| `0x08BE222C` | `gItemData` (`IInfo`) | `0x24` B | Static item/weapon templates (IDs `0x00..0x9E`) |
| `0x08C9A1C0` | `gAffinityBonusTable` | `8` B | Per-affinity combat bonuses (Atk/Def/Hit/Avo/Crit/Dodge) |

---

## 2. RNG System (`game/engine/rng.{h,cc}`)

FE7 maintains two independent PRNGs:

1. **Primary Gameplay RNG (`gRNSeeds[3]` at `0x03000000`):**
   - A 48-bit (`3 × uint16_t`) shift-register generator advanced by `NextRN()`.
   - `RandNext100()` scales the 16-bit output into `[0, 99]` via
     `(NextRN() * 100) / 0x10000`.
   - **1-RN rolls (`RandRoll`):** `threshold > RandNext100()` — used for crits,
     Silencer, Devil Axe backfire, and level-up stat growths.
   - **2-RN "True Hit" (`RandRoll2Rn`):** `threshold > (RandNext100() + RandNext100()) / 2` —
     used for combat hit rates. Averaging two uniform rolls creates a triangular
     distribution: displayed hit rates above 50% hit *more* often than shown,
     and rates below 50% miss *more* often than shown.
   - **Seeding (`RandInit`):** Picks 3 halfwords from the 8-entry ROM table at
     `0x080C5820` indexed by `(seed % 7) & 7`, then warms up by `seed % 23`
     calls to `NextRN()`.
2. **Secondary Visual RNG (`gLCGRNValue` at `0x03000008`):**
   - `RandNextB()` computes `((s * 4 + 2) * (s * 4 + 3)) >> 2` (30-bit output)
     for cosmetic effects without perturbing gameplay RNG.

---

## 3. 6C Proc Cooperative Scheduler (`game/engine/proc.{h,cc}`)

FE7 has no OS threads; all gameplay subsystems (menus, animations, map cursor,
battle flow, chapter events) run as **Procs** — lightweight cooperative
coroutines stored in a fixed 64-slot pool (`gProcs`, `0x6C` bytes per slot,
hence the community name "6C").

- **8 Root Trees:** `gProcTreeRoots[0]` runs during VSync; trees `1..7` run in
  order each main-loop frame.
- **Hierarchy:** Each `Proc` has `proc_parent`, `proc_child`, `proc_next`, and
  `proc_prev` links. `SpawnProcLocking(script, parent)` increments
  `parent->proc_lock_cnt`, suspending the parent until the child ends.
- **Subclassing:** Bytes `0x00..0x28` are the common `Proc` header; bytes
  `0x2C..0x6B` (`user_data[16]`) hold subsystem-specific state accessed via
  `ProcAs<SubProc>(proc)`.
- **Bytecode VM (`ProcRunScript`):** Interprets 26 `ProcCmd` opcodes (`0x00..0x19`)
  from ROM `ProcScr` arrays (`kEnd`, `kCall`, `kRepeat`, `kOnEnd`,
  `kStartChild`, `kStartChildBlocking`, `kWhileExists`, `kLabel`, `kGoto`,
  `kSleep`, `kBlock`, `kWhile`, `kCallArg`, etc.).

---

## 4. Units, Factions, Items & Weapon Ranks (`game/unit/`, `game/item/`)

### Units & Factions

Every active `Unit` (`0x48` bytes in EWRAM) points to a ROM `PInfo` (character)
and `JInfo` (class). Its 8-bit `Unit::id` encodes both faction and 1-based slot:

| Faction | Base ID (`id & 0xC0`) | Pool Capacity | Meaning |
| --- | --- | --- | --- |
| Blue | `0x00` | 62 | Player units |
| Green | `0x40` | 20 | Ally NPC units |
| Red | `0x80` | 50 | Enemy units |
| Purple | `0xC0` | 5 | Link Arena 4th team |

- **Combined Attributes:** `UnitAttributes(unit)` returns
  `unit.pinfo->attributes | unit.jinfo->attributes` (32-bit bitmask covering
  `Mounted`, `Canto`, `CritBonus`, `Promoted`, `Lord`, `Boss`, `FinalBoss`,
  `Assassin`, `MagicSeal`, and character weapon locks).
- **Dynamic Flags (`Unit::flags`):** Tracks `Hidden`, `TurnEnded`, `Dead`,
  `NotDeployed`, `Rescuing`, `Rescued`, `HasMoved`, `FogHidden`, `InBallista`,
  `GrowthBoost` (Afa's Drops), etc.
- **Status (`Unit::status` + `status_duration`):** Packed into two 4-bit nibbles
  (`Poison`, `Sleep`, `Silenced`, `Berserk`, and Dance-ring buffs `Attack`,
  `Defense`, `Crit`, `Avoid`).

### Items & Weapon Ranks

Each inventory slot (`Unit::items[5]`) is a packed `uint16_t`:
- Low byte (`item & 0xFF`): Item ID (`iid`, index into `gItemData`, `0x00..0x9E`)
- High byte (`item >> 8`): Remaining uses / durability

Weapon experience (`Unit::wexp[8]`) maps to `WeaponRank`:
`None (0)`, `E (1)`, `D (31)`, `C (71)`, `B (121)`, `A (181)`, `S (251)`.
Reaching **S-rank (`wexp > 250`)** grants **+5 Hit and +5 Crit** in combat.

---

## 5. Combat Formulas, Simulation & Post-Battle Progression (`game/battle/`)

### 5.1 Derived Battle Stats (`game/battle/battle.cc`)

`ComputeBattleUnitStats` runs on both `gBattleActor` and `gBattleTarget`,
followed by `ComputeBattleUnitEffectiveStats`:

- **Weapon Triangle & Reaver:** Looks up `(attacker_kind, defender_kind)` in
  `gWeaponTriangleRules` (`±15 Hit, ±1 Dmg` standard). If *exactly one*
  combatant holds a Reaver weapon (`kItemAttrReverseTriangle`), bonuses are
  **negated and doubled** (`∓30 Hit, ∓2 Dmg`); if both hold Reavers, they cancel.
- **Attack:**
  $$\text{Atk} = \text{Pow} + (\text{Might} + \text{WTA\_Dmg}) \times \text{EffMult}$$
  where $\text{EffMult} = 2$ in **FE7 US** when `IsItemEffectiveAgainst` is true
  (1 otherwise).
- **Defense:** `terrain_res + res` if opponent's weapon has `kItemAttrMagic` or
  `kItemAttrMagicDamage`, else `terrain_def + def`.
- **Attack Speed (AS):**
  $$\text{AS} = \max\big(0,\ \text{Spd} - \max(0,\ \text{Weight} - \text{Con})\big)$$
- **Hit Rate:** `ItemHit + Skl * 2 + Lck / 2 + WTA_Hit` (+ Tactician / Support / S-rank).
- **Avoid Rate:** `max(0, AS * 2 + TerrainAvo + Lck)` (+ Tactician / Support / Status).
- **Crit Rate:** `ItemCrit + Skl / 2` (+ 15 if `kUnitAttrCritBonus` [Swordmaster/Berserker], + Support / S-rank / Status).
- **Dodge Rate:** `Lck` (+ Support).
- **Effective Hit:** `clamp(attacker.Hit - defender.Avoid, 0, 100)`.
- **Effective Crit:** `max(0, attacker.Crit - defender.Dodge - defender_TacticianStars)`,
  forced to `0` if defender carries an Iron Rune (`kItemAttrNegateCrit`).
- **Assassin Silencer:** `50%` on normal enemies, `25%` on `kUnitAttrBoss`, `0%`
  on `kUnitAttrFinalBoss` (rolled only after a Critical Hit triggers).
- **FE7 Tactician Bonuses:** Active only for Blue units when Tactician is
  enabled (`gPlayStatus+0x2B & 1`), outside Lyn Mode (`gPlayStatus+0x1B != 1`)
  and outside Link Arena (`(gBmStatus+0x04 & 0x40) == 0`).
  - `stars = min(10, raw_stars / 12)` from the 9-bit field at `gPlayStatus+0x2C`.
  - Units matching the Tactician's affinity gain `+stars` Hit and `+stars` Avoid;
    all eligible Blue units reduce incoming enemy Effective Crit by `stars`.
  - Holding item `0x8A` grants an additional `+10` Hit and `+10` Avoid.
- **Special Weapons:**
  - *Light Brand (`0x10`), Runesword (`0x11`), Wind Sword (`0x99`)* at range:
    subtract `Pow / 2` from Attack and zero Crit.
  - *Eclipse (`weapon_effect == 3`):* sets Attack to `max(1, (target.hp + 1) >> 1)`,
    zeroes defender Defense and attacker Crit, and cannot double.
  - *Luna (`kItemAttrNegateDefense`):* zeroes defender Defense/Resistance.

### 5.2 Combat Unwinding (`game/battle/battle_sim.cc`)

`BattleUnwind()` simulates the entire exchange up front into the 7-slot
`gBattleHitArray`:
1. **Initial Attack:** Actor strikes (2 hits if `kItemAttrBrave`).
2. **Retaliation:** If Target survives and `can_counter != 0`, Target strikes
   (2 hits if Brave).
3. **Speed Follow-Up:** If `|actor.AS - target.AS| >= 4` (and the faster unit
   can strike and does not wield Eclipse), the faster unit performs a follow-up
   round (2 hits if Brave).
4. **Triangle Attack:** `BattleCheckTriangleAttackOrder` scans the 4 tiles
   adjacent to the defender for 2+ same-faction allies sharing the Pegasus or
   Armor Triangle Attack attribute; when active, guarantees Hit and Crit.
5. **Preview vs. Real Combat:** `BattleRoll1RN` / `BattleRoll2RN` consult
   `kBattleConfigSimulate` so battle forecasts and AI scoring run without
   consuming gameplay RNG.

### 5.3 Post-Battle EXP, Level-Up & Promotion (`game/battle/battle_exp.cc`)

- **Level-Up (`CheckBattleUnitLevelUp`):** Triggers at `exp >= 100` (max level
  20). Rolls all 7 stats via `GetStatIncrease(growth)` (guaranteed `+1` per
  100% plus a 1-RN roll for the remainder). Units with `kUnitFlagGrowthBoost`
  (Afa's Drops) receive `+5%` to every growth. If all 7 rolls yield 0, FE7
  performs up to **2 empty-level rerolls**.
- **Autoleveling (`GetAutoleveledStatIncrease`):** Applies `±12.5%` random
  variance over `level_count` levels for enemy generation.
- **Promotion (`UnitPromote`):** Switches `unit->jinfo` to `jid_promote`, adds
  class promotion bonuses, clamps stats to promoted caps, grants base promoted
  WEXP, and resets `level = 1`, `exp = 0`.

---

## 6. Map, Terrain & BFS Pathfinding (`game/map/map.{h,cc}`)

- **Padded Row-Pointer Maps (`MapInit`):** Each 2D map (`gMapUnit`,
  `gMapTerrain`, `gMapMovement`, `gMapRange`) stores a row-pointer table inside
  a contiguous buffer with **2 padding rows/columns** around the playable grid
  (`*map_out = row_ptrs + 2`) so neighbor loops can index `y - 1` / `y + 1`
  without per-tile bounds checks.
- **BFS Movement Flood (`MapFloodCoreRam`, `0x08004408`):** Double-buffered
  breadth-first search using `gMapFloodSquareBufA` and `gMapFloodSquareBufB` in
  IWRAM (`0x030041F0`, `0x03004490`). Each tile's movement cost is read from
  `gWorkingMovTable[terrain_id]` (`0x030043F0`), and opposing-faction units on
  `gMapUnit` block traversal when `has_move_limit` is set.
- **Range Diamonds (`MapAddInRange`, `MapSetInRange`):** Marks Manhattan
  distance diamonds (`|dx| + |dy| <= range`) on `gWorkingMap`.

---

## 7. Turn Flow, Supports & Enemy AI

### Turn & Phase Rotation (`game/phase/phase.{h,cc}`)

- Phases rotate **`Blue (0x00) → Red (0x80) → Green (0x40) → Blue (0x00)`** via
  `SwitchPhases()`.
- Wrapping back to Blue increments `gPlayStatus.turn` (capped at `999`) and
  invokes `ProcessSupportGains()`.
- `BmMainChangePhase()` refreshes the ending faction's units, rotates phases,
  and runs turn-start events; `BmMainUpdateTraps()` ticks map traps on Green
  phase.
- Party gold is clamped to `[0, 999999]`.

### Supports & Affinity (`game/support/support.{h,cc}`)

- Each character has up to **7** configured partners (`PInfo::support_info`).
- Support rank thresholds are `80` (C), `160` (B), `240` (A) with cap `241`;
  a unit can hold at most **5 total support ranks** across all partners.
- At the start of each player turn, adjacent (or rescued) eligible partners gain
  `exp_growth` points, clamped to the current rank's threshold until a support
  conversation unlocks the next rank (`UnitGainSupportLevel`).
- In combat, any supported partner within **3 tiles** contributes affinity
  bonuses from `gAffinityBonusTable` (`0x08C9A1C0`) scaled by rank (C=1, B=2,
  A=3) and halved after summing both units' affinities.

### Enemy AI (`game/ai/`: `ai`, `ai_decide`, `ai_script`, `ai_opcodes`, `ai_actions`, `ai_combat`, `ai_cp`, `ai_staff_loot`)

- Top-level `CpDecide` / `CpPerform` state-machine procs (`ai_cp`) drive each
  unit's turn through recovery/suspend checks, Script A & Script B bytecode
  execution (`ai_script`, `ai_opcodes`), special actions (staves, stealing,
  chest/door looting, antitoxin, ballista mounting in `ai_staff_loot` /
  `ai_actions`), and combat target selection (`ai_combat`, `ai_decide`).
- Stores the selected move/action in `gAiDecision` (`0x0203A97C`).
- Evaluates candidate `(x_move, y_move, target_id, item_slot)` tuples via
  `AiComputeCombatScore`, summing **8 weighted components** driven by
  `AiCombatScoreCoefficients` (`0x081D36F4`):
  1. Damage dealt
  2. Opponent low HP / KO bonus
  3. Friend-zone proximity (`±3` tiles)
  4. Target class rank priority
  5. Turn number weight
  6. Damage taken penalty
  7. Danger-map exposure penalty (`AiFillDangerMap`)
  8. Self low HP penalty

---

## 8. Notable FE7 US vs. FE8 / FE7J Differences

When using `fireemblem8u` or `FE7J` as reference corpora, never assume 1:1
parity — always verify against FE7 US disassembly:

| Area | FE7 US (`AE7E`) Behavior | Common Reference Trap |
| --- | --- | --- |
| Effective Weapon Might | **2×** (`atk * 2`) | FE7J and FE8 use **3×** |
| Tactician Bonuses | Hit/Avoid/Crit-Evade from `gPlayStatus+0x2B..0x2D` + item `0x8A` | Does not exist in FE8 |
| Constitution Field | `PInfo::bonus_con` added to `JInfo::base_con` | Field naming/handling differs in some FE8 helpers |
| `InitBattleUnit` | Zeroes combat delta fields on **both** `gBattleActor` and `gBattleTarget` | Easy to miss if only clearing the passed `bu` pointer |
| `BattleGenerateRoundHits` | Captures `attributes` **before** calling `BattleCheckBrave` | Reordering changes Brave hit attribute bits |
| Magic Seal (`kUnitAttrMagicSeal`) | Blocks magic/staff usage within seal aura | FE7-specific class mechanic (Kishuna) |
