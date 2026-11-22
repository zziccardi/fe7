#!/usr/bin/env python3
"""Indexes FE7 US symbols from corpus/fe7_us and cross-matches them against
corpus/fireemblem8u and corpus/fireemblem6j to generate a bottom-up
decompilation work queue (work_queue.json)."""

import argparse
from collections import defaultdict
import json
import pathlib
import re
import sys

# Matches assembly function labels like `NextRN: @ 0x08000BE0`
ASM_FUNC_RE = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_]*):\s*@\s*(0x08[0-9A-Fa-f]{6})\b"
)
# Matches Thumb/ARM branch-with-link instructions like `bl NextRN`
ASM_BL_RE = re.compile(r"^\s+bl\s+([A-Za-z_][A-Za-z0-9_]*)\b")
# Matches top-level C function definitions in FE8U / FE6J src/*.c
C_FUNC_RE = re.compile(
    r"^(?:[A-Za-z_][A-Za-z0-9_\s\*]+?)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*\)\s*\{"
)

WAVE_RULES: list[tuple[int, str, tuple[str, ...]]] = [
    (1, "engine", ("main", "proc", "rng", "hardware", "dma", "oam", "bg", "irq",
                   "armfunc", "crt0", "interrupt", "sram", "svc", "util")),
    (2, "audio", ("m4a", "sound", "audio", "song", "music")),
    (3, "core", ("bmunit", "bmitem", "bmmap", "bmbattle", "bmsave", "bmio",
                 "bm", "unit", "item", "map", "battle", "save")),
    (4, "ai", ("cp_", "ai_", "fog", "weather", "support", "tactician", "rank")),
    (5, "ui", ("talk", "face", "font", "menu", "prep", "shop", "arena",
               "worldmap", "event", "msg", "text", "icon", "ui", "statscreen")),
    (6, "banim", ("ekr", "efx", "banim", "spell", "cg", "credits", "soundroom")),
]


def normalize_name(name: str) -> str:
  """Normalizes C/asm symbol names for case-insensitive cross-game matching."""
  return re.sub(r"[^a-z0-9]", "", name.lower())


def classify_wave(name: str, matched_file: str | None) -> tuple[int, str]:
  """Assigns a function to Wave 1..6 using its matched C filename or symbol."""
  haystack = f"{matched_file or ''} {name}".lower()
  for wave_num, wave_name, keywords in WAVE_RULES:
    if any(kw in haystack for kw in keywords):
      return wave_num, wave_name
  return 3, "core"


def parse_fe7_asm_symbols(
    asm_dirs: list[pathlib.Path],
) -> tuple[dict[str, dict], dict[str, set[str]]]:
  """Extracts FE7 US functions (name -> metadata) and direct `bl` callees."""
  funcs: dict[str, dict] = {}
  callees: dict[str, set[str]] = defaultdict(set)

  for asm_dir in asm_dirs:
    if not asm_dir.exists():
      continue
    for path in sorted(asm_dir.glob("*.s")):
      current_fn: str | None = None
      for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        m_fn = ASM_FUNC_RE.match(line)
        if m_fn:
          name, addr_str = m_fn.group(1), m_fn.group(2)
          current_fn = name
          funcs[name] = {
              "name": name,
              "addr": addr_str.lower(),
              "addr_int": int(addr_str, 16),
              "asm_file": path.name,
          }
          continue
        if current_fn is not None:
          m_bl = ASM_BL_RE.match(line)
          if m_bl:
            callees[current_fn].add(m_bl.group(1))

  return funcs, callees


def index_c_functions(src_dir: pathlib.Path) -> dict[str, str]:
  """Maps normalized C function name -> relative source path in a decomp."""
  index: dict[str, str] = {}
  if not src_dir.exists():
    return index
  for path in sorted(src_dir.rglob("*.c")):
    rel = str(path.relative_to(src_dir))
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
      if line.startswith((" ", "\t", "#", "/", "*")):
        continue
      m = C_FUNC_RE.match(line)
      if m:
        fn_name = m.group(1)
        if fn_name not in ("if", "for", "while", "switch"):
          index.setdefault(normalize_name(fn_name), rel)
  return index


def compute_dependency_depths(
    funcs: dict[str, dict], callees: dict[str, set[str]]
) -> dict[str, int]:
  """Computes bottom-up call depth (0 = leaf function; cycles broken safely)."""
  memo: dict[str, int] = {}
  visiting: set[str] = set()

  def depth(fn: str) -> int:
    if fn in memo:
      return memo[fn]
    if fn in visiting:
      return 0
    visiting.add(fn)
    max_child = -1
    for callee in callees.get(fn, ()):
      if callee in funcs and callee != fn:
        max_child = max(max_child, depth(callee))
    visiting.remove(fn)
    memo[fn] = max_child + 1
    return memo[fn]

  for fn in funcs:
    depth(fn)
  return memo


def build_work_queue(corpus_dir: pathlib.Path) -> list[dict]:
  """Builds the prioritized bottom-up work queue from local corpus clones."""
  fe7_dir = corpus_dir / "fe7_us"
  funcs, callees = parse_fe7_asm_symbols([fe7_dir / "asm", fe7_dir / "src"])
  fe8_index = index_c_functions(corpus_dir / "fireemblem8u" / "src")
  fe6_index = index_c_functions(corpus_dir / "fireemblem6j" / "src")
  depths = compute_dependency_depths(funcs, callees)

  queue: list[dict] = []
  for name, info in funcs.items():
    norm = normalize_name(name)
    fe8_match = fe8_index.get(norm)
    fe6_match = fe6_index.get(norm)
    wave_num, wave_name = classify_wave(name, fe8_match or fe6_match)
    queue.append({
        "name": name,
        "addr": info["addr"],
        "wave": wave_num,
        "subsystem": wave_name,
        "depth": depths.get(name, 0),
        "callee_count": len(callees.get(name, ())),
        "fe8u_match": fe8_match,
        "fe6j_match": fe6_match,
        "asm_file": info["asm_file"],
    })

  # Order by wave, then bottom-up depth, then matched-first, then ROM address.
  queue.sort(
      key=lambda item: (
          item["wave"],
          item["depth"],
          0 if (item["fe8u_match"] or item["fe6j_match"]) else 1,
          int(item["addr"], 16),
      )
  )
  return queue


def main(argv: list[str]) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument(
      "--corpus",
      type=pathlib.Path,
      default=pathlib.Path("corpus"),
      help="Directory containing cloned fe7_us, fireemblem8u, fireemblem6j.",
  )
  parser.add_argument(
      "--out",
      type=pathlib.Path,
      default=pathlib.Path("corpus/work_queue.json"),
      help="Output JSON path for the prioritized work queue.",
  )
  args = parser.parse_args(argv[1:])

  queue = build_work_queue(args.corpus)
  args.out.parent.mkdir(parents=True, exist_ok=True)
  args.out.write_text(json.dumps(queue, indent=2) + "\n", encoding="utf-8")

  matched = sum(1 for q in queue if q["fe8u_match"] or q["fe6j_match"])
  print(
      f"Indexed {len(queue)} FE7 US functions ({matched} matched in FE8U/FE6J) "
      f"-> {args.out}"
  )
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))
