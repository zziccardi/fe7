#!/usr/bin/env python3
"""Prepares a local headless Ghidra project and FE7 US symbol table.

Validates the user-provided FE7 US (AE7E) ROM by size and SHA-1, exports a
Ghidra-compatible CSV symbol map from corpus/fe7_us, and (when GHIDRA_INSTALL_DIR
is set) runs analyzeHeadless into gitignored .ghidra/.
"""

import argparse
import csv
import hashlib
import os
import pathlib
import subprocess
import sys

from tools import match_fe8

EXPECTED_ROM_SIZE = 16 * 1024 * 1024
EXPECTED_ROM_SHA1 = "c735fdbb9e8abe19e0c6a44708df19acc962e204"


def validate_rom(rom_path: pathlib.Path) -> None:
  data = rom_path.read_bytes()
  if len(data) != EXPECTED_ROM_SIZE:
    raise ValueError(
        f"Expected {EXPECTED_ROM_SIZE} bytes, got {len(data)} bytes"
    )
  sha1 = hashlib.sha1(data).hexdigest()
  if sha1 != EXPECTED_ROM_SHA1:
    raise ValueError(f"Expected SHA-1 {EXPECTED_ROM_SHA1}, got {sha1}")


def export_symbol_csv(corpus_dir: pathlib.Path, out_csv: pathlib.Path) -> int:
  fe7_dir = corpus_dir / "fe7_us"
  funcs, _ = match_fe8.parse_fe7_asm_symbols(
      [fe7_dir / "asm", fe7_dir / "src"]
  )
  out_csv.parent.mkdir(parents=True, exist_ok=True)
  with out_csv.open("w", newline="", encoding="utf-8") as f:
    writer = csv.writer(f)
    writer.writerow(["name", "address"])
    for info in sorted(funcs.values(), key=lambda x: x["addr_int"]):
      writer.writerow([info["name"], info["addr"]])
  return len(funcs)


def main(argv: list[str]) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--rom", type=pathlib.Path, required=True)
  parser.add_argument("--corpus", type=pathlib.Path, default=pathlib.Path("corpus"))
  parser.add_argument(
      "--project_dir", type=pathlib.Path, default=pathlib.Path(".ghidra")
  )
  args = parser.parse_args(argv[1:])

  validate_rom(args.rom)
  sym_csv = args.project_dir / "fe7_us_symbols.csv"
  count = export_symbol_csv(args.corpus, sym_csv)
  print(f"Exported {count} FE7 US symbols -> {sym_csv}")

  ghidra_dir = os.environ.get("GHIDRA_INSTALL_DIR")
  if not ghidra_dir:
    print(
        "GHIDRA_INSTALL_DIR not set; skipping analyzeHeadless. "
        "Set GHIDRA_INSTALL_DIR to run headless Ghidra analysis."
    )
    return 0

  headless = pathlib.Path(ghidra_dir) / "support" / "analyzeHeadless"
  cmd = [
      str(headless),
      str(args.project_dir),
      "fe7_us",
      "-import",
      str(args.rom),
      "-processor",
      "ARM:LE:32:v4t",
      "-loader",
      "BinaryLoader",
      "-loader-baseAddr",
      "0x08000000",
  ]
  print("+", " ".join(cmd))
  return subprocess.run(cmd).returncode


if __name__ == "__main__":
  sys.exit(main(sys.argv))
