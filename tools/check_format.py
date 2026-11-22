#!/usr/bin/env python3
"""Checks 80-column line length and clang-format compliance while allowing manual
horizontal column alignment (extra internal spaces) for readability."""

import difflib
import pathlib
import re
import subprocess
import sys

MAX_LINE_LENGTH = 80
INTERNAL_SPACES_RE = re.compile(r" {2,}")


def normalize_internal_spaces(text: str) -> list[str]:
  """Preserves leading indentation while collapsing runs of internal spaces."""
  out: list[str] = []
  for line in text.splitlines():
    stripped = line.lstrip(" ")
    indent = line[: len(line) - len(stripped)]
    out.append(indent + INTERNAL_SPACES_RE.sub(" ", stripped))
  return out


def check_file(path: pathlib.Path) -> list[str]:
  errors: list[str] = []
  original = path.read_text(encoding="utf-8")

  for lineno, line in enumerate(original.splitlines(), start=1):
    if len(line) > MAX_LINE_LENGTH:
      errors.append(
          f"{path}:{lineno}: line length {len(line)} exceeds {MAX_LINE_LENGTH}"
      )

  result = subprocess.run(
      ["clang-format", str(path)],
      check=True,
      capture_output=True,
      text=True,
  )
  orig_norm = normalize_internal_spaces(original)
  fmt_norm = normalize_internal_spaces(result.stdout)
  if orig_norm != fmt_norm:
    diff = "".join(
        difflib.unified_diff(
            [l + "\n" for l in orig_norm],
            [l + "\n" for l in fmt_norm],
            fromfile=str(path),
            tofile=f"{path} (clang-format)",
            n=2,
        )
    )
    errors.append(f"{path}: clang-format mismatch:\n{diff.rstrip()}")

  return errors


def main(argv: list[str]) -> int:
  roots = [pathlib.Path(a) for a in argv[1:]] or [
      pathlib.Path("runtime"),
      pathlib.Path("game"),
  ]
  all_errors: list[str] = []
  for root in roots:
    if not root.exists():
      continue
    if root.is_file():
      all_errors.extend(check_file(root))
      continue
    for ext in ("*.h", "*.cc", "*.c"):
      for path in sorted(root.rglob(ext)):
        all_errors.extend(check_file(path))

  for err in all_errors:
    print(err, file=sys.stderr)
  if all_errors:
    print(f"Found {len(all_errors)} formatting issue(s).", file=sys.stderr)
    return 1
  print("check_format: OK")
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))
