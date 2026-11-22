#!/usr/bin/env python3
"""Per-function / per-PR verification wrapper for Phase 2+ decompilation.

Runs formatting, game/ agent rules, ASan+UBSan headless unit tests, and
(when --rom is supplied) a headless smoke/checkpoint run against the retail ROM.
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys


def resolve_bazel_cmd() -> list[str]:
  """Finds bazel/bazelisk and attaches system Java trustStore if present."""
  bazel = shutil.which("bazel") or str(
      pathlib.Path.home() / ".local" / "bin" / "bazel"
  )
  cmd = [bazel]
  cacerts = pathlib.Path("/etc/ssl/certs/java/cacerts")
  if cacerts.exists():
    cmd.extend([
        f"--host_jvm_args=-Djavax.net.ssl.trustStore={cacerts}",
        "--host_jvm_args=-Djavax.net.ssl.trustStorePassword=changeit",
    ])
  return cmd


def build_steps(
    rom_path: str | None,
    frames: int,
    trace_addrs: str | None,
    record_traces: str | None,
) -> list[list[str]]:
  """Constructs the ordered verification command list."""
  bazel = resolve_bazel_cmd()
  steps: list[list[str]] = [
      [sys.executable, "tools/check_format.py"],
      [sys.executable, "tools/check_agent_rules.py"],
      [*bazel, "test", "--config=asan", "--config=headless", "//..."],
  ]
  if rom_path:
    run_cmd = [
        *bazel,
        "run",
        "--config=release",
        "--config=headless",
        "//runtime:fe7_headless",
        "--",
        f"--rom={os.path.abspath(rom_path)}",
        f"--frames={frames}",
        "--dump_checkpoints",
    ]
    if trace_addrs and record_traces:
      run_cmd.extend([
          f"--trace_addrs={trace_addrs}",
          f"--record_traces={os.path.abspath(record_traces)}",
      ])
    steps.append(run_cmd)
  return steps


def main(argv: list[str]) -> int:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--rom", default=None, help="Path to retail FE7 US ROM.")
  parser.add_argument("--frames", type=int, default=60, help="Frames to step.")
  parser.add_argument(
      "--trace_addrs",
      default=None,
      help="Comma-separated hex ROM addresses to record traces for.",
  )
  parser.add_argument(
      "--record_traces",
      default=None,
      help="Output .fe7t path for recorded traces.",
  )
  args = parser.parse_args(argv[1:])

  for step in build_steps(
      args.rom, args.frames, args.trace_addrs, args.record_traces
  ):
    print("+", " ".join(step), flush=True)
    res = subprocess.run(step)
    if res.returncode != 0:
      return res.returncode

  print("verify_fn: ALL CHECKS PASSED")
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))
