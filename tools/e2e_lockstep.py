#!/usr/bin/env python3
"""Retail-ROM headless E2E lockstep verifier and fallback profiler.

Runs `//runtime:fe7_headless` twice on the same `--rom` and optional
`--input_script`:
  1. Baseline (`--register_native=false`): pure ARM7TDMI fallback execution.
  2. Native (`--register_native=true`): all decompiled C++20 functions active.

Compares EWRAM, static IWRAM, and framebuffer FNV-1a checkpoints and reports
the hottest remaining un-decompiled fallback addresses.
"""

import argparse
from dataclasses import dataclass
import pathlib
import re
import subprocess
import sys

CHECKPOINT_RE = re.compile(
    r"^Checkpoint frame=(\d+) ewram=(0x[0-9a-fA-F]+) "
    r"iwram=(0x[0-9a-fA-F]+) fb=(0x[0-9a-fA-F]+)$"
)
FALLBACK_LINE_RE = re.compile(r"^\s+(0x[0-9a-fA-F]+):\s+(\d+)$")
FALLBACK_TOTAL_RE = re.compile(r"^Fallback addresses hit:\s+(\d+)$")
SYM_FUNC_RE = re.compile(r"^([A-Za-z0-9_]+):\s+@\s+0x([0-9a-fA-F]+)")


@dataclass(frozen=True)
class Checkpoint:
  frame: int
  ewram: str
  iwram: str
  fb: str


def parse_checkpoints(output: str) -> dict[int, Checkpoint]:
  """Extracts frame -> Checkpoint entries from `fe7_headless` stdout."""
  out: dict[int, Checkpoint] = {}
  for line in output.splitlines():
    m = CHECKPOINT_RE.match(line.strip())
    if m:
      frame = int(m.group(1))
      out[frame] = Checkpoint(
          frame=frame,
          ewram=m.group(2).lower(),
          iwram=m.group(3).lower(),
          fb=m.group(4).lower(),
      )
  return out


def parse_fallbacks(output: str) -> tuple[int, list[tuple[int, int]]]:
  """Extracts `(total_unique_addrs, [(addr, count), ...])` from stdout."""
  total = 0
  entries: list[tuple[int, int]] = []
  for line in output.splitlines():
    m_tot = FALLBACK_TOTAL_RE.match(line.strip())
    if m_tot:
      total = int(m_tot.group(1))
      continue
    m_line = FALLBACK_LINE_RE.match(line)
    if m_line:
      entries.append((int(m_line.group(1), 16), int(m_line.group(2))))
  return total, entries


def compare_checkpoints(
    baseline: dict[int, Checkpoint], native: dict[int, Checkpoint]
) -> list[str]:
  """Returns human-readable mismatch descriptions (empty list = exact match)."""
  diffs: list[str] = []
  all_frames = sorted(set(baseline) | set(native))
  for frame in all_frames:
    b = baseline.get(frame)
    n = native.get(frame)
    if b is None or n is None:
      diffs.append(
          f"frame {frame}: missing checkpoint (baseline={b is not None}, "
          f"native={n is not None})"
      )
      continue
    if b != n:
      diffs.append(
          f"frame {frame}: mismatch\n"
          f"  baseline: ewram={b.ewram} iwram={b.iwram} fb={b.fb}\n"
          f"  native:   ewram={n.ewram} iwram={n.iwram} fb={n.fb}"
      )
  return diffs


def load_local_symbols(code_s_path: pathlib.Path) -> dict[int, str]:
  """Loads FE7 US ROM address -> symbol name from local gitignored code.s."""
  symbols: dict[int, str] = {}
  if not code_s_path.is_file():
    return symbols
  with code_s_path.open("r", encoding="utf-8", errors="ignore") as f:
    for line in f:
      m = SYM_FUNC_RE.match(line)
      if m:
        symbols[int(m.group(2), 16)] = m.group(1)
  return symbols


def build_headless_cmd(
    binary: str,
    rom: str,
    frames: int,
    checkpoint_frames: str,
    input_script: str,
    register_native: bool,
    fallback_top: int,
) -> list[str]:
  """Builds the `fe7_headless` command line for one lockstep run."""
  cmd = [
      binary,
      f"--rom={rom}",
      f"--frames={frames}",
      f"--register_native={'true' if register_native else 'false'}",
      "--dump_checkpoints",
  ]
  if checkpoint_frames:
    cmd.append(f"--checkpoint_frames={checkpoint_frames}")
  if input_script:
    cmd.append(f"--input_script={input_script}")
  if register_native and fallback_top >= 0:
    cmd.append("--report_fallbacks")
    if fallback_top > 0:
      cmd.append(f"--fallback_top={fallback_top}")
  return cmd


def main(argv: list[str]) -> int:
  parser = argparse.ArgumentParser(
      description="Run FE7 headless E2E lockstep & fallback profiler."
  )
  parser.add_argument(
      "--binary",
      default="./bazel-bin/runtime/fe7_headless",
      help="Path to fe7_headless binary.",
  )
  parser.add_argument("--rom", required=True, help="Path to FE7 US ROM.")
  parser.add_argument("--frames", type=int, default=360, help="Frames to run.")
  parser.add_argument(
      "--checkpoint_frames",
      default="",
      help="Comma-separated 1-based checkpoint frames.",
  )
  parser.add_argument(
      "--input_script",
      default="",
      help="Optional .fe7in scripted button input file.",
  )
  parser.add_argument(
      "--fallback_top",
      type=int,
      default=25,
      help="Number of hottest fallback functions to display (0 = all).",
  )
  parser.add_argument(
      "--symbols",
      default="corpus/fe7_us/asm/code.s",
      help="Optional local disassembly path to annotate fallback addresses.",
  )
  args = parser.parse_args(argv)

  base_cmd = build_headless_cmd(
      args.binary,
      args.rom,
      args.frames,
      args.checkpoint_frames,
      args.input_script,
      register_native=False,
      fallback_top=0,
  )
  nat_cmd = build_headless_cmd(
      args.binary,
      args.rom,
      args.frames,
      args.checkpoint_frames,
      args.input_script,
      register_native=True,
      fallback_top=args.fallback_top,
  )

  base_res = subprocess.run(base_cmd, check=True, capture_output=True, text=True)
  nat_res = subprocess.run(nat_cmd, check=True, capture_output=True, text=True)

  base_cp = parse_checkpoints(base_res.stdout)
  nat_cp = parse_checkpoints(nat_res.stdout)
  diffs = compare_checkpoints(base_cp, nat_cp)

  print(f"Checkpoints compared: {len(base_cp)}")
  if diffs:
    print("LOCKSTEP DIFFS:")
    for d in diffs:
      print(f"  {d}")
  else:
    print("LOCKSTEP: EXACT MATCH across all checkpoints")

  total_fb, top_fb = parse_fallbacks(nat_res.stdout)
  symbols = load_local_symbols(pathlib.Path(args.symbols))
  print(f"\nUnique fallback addresses hit: {total_fb}")
  if top_fb:
    print(f"Top {len(top_fb)} fallback targets:")
    for addr, count in top_fb:
      sym = symbols.get(addr, "")
      label = f" ({sym})" if sym else ""
      print(f"  0x{addr:08x}{label}: {count}")

  return 1 if diffs else 0


if __name__ == "__main__":
  sys.exit(main(sys.argv[1:]))
