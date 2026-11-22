#!/usr/bin/env python3
"""Unit tests for tools/check_format.py."""

import pathlib
import shutil
import tempfile
import unittest

from tools import check_format

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent


class CheckFormatTest(unittest.TestCase):

  def setUp(self):
    self._tmpdir = tempfile.TemporaryDirectory()
    self.tmpdir = pathlib.Path(self._tmpdir.name)
    # Place the repo's .clang-format in tmpdir so clang-format resolves the
    # exact project style regardless of where /tmp lives.
    shutil.copy(REPO_ROOT / ".clang-format", self.tmpdir / ".clang-format")

  def tearDown(self):
    self._tmpdir.cleanup()

  def _write(self, name: str, content: str) -> pathlib.Path:
    path = self.tmpdir / name
    path.write_text(content, encoding="utf-8")
    return path

  def test_normalize_internal_spaces_preserves_leading_indent(self):
    text = "  kKeyA      = 1u << 0,\n    int   x  =  1;\n"
    self.assertEqual(
        check_format.normalize_internal_spaces(text),
        ["  kKeyA = 1u << 0,", "    int x = 1;"],
    )

  def test_allows_manual_column_alignment(self):
    path = self._write(
        "aligned.h",
        "enum Foo {\n"
        "  kA      = 1,\n"
        "  kLonger = 2,\n"
        "};\n"
        "\n"
        "uint8_t  Read8(int addr);\n"
        "uint16_t Read16(int addr);\n",
    )
    self.assertEqual(check_format.check_file(path), [])

  def test_rejects_line_exceeding_80_columns(self):
    long_comment = "// " + ("x" * 80) + "\n"
    path = self._write("too_long.h", long_comment)
    errors = check_format.check_file(path)
    self.assertTrue(any("exceeds 80" in err for err in errors), errors)

  def test_rejects_wrong_leading_indentation(self):
    path = self._write(
        "bad_indent.cc",
        "int Foo() {\n"
        "    return 1;\n"
        "}\n",
    )
    errors = check_format.check_file(path)
    self.assertTrue(any("clang-format mismatch" in err for err in errors),
                    errors)

  def test_rejects_wrong_brace_style(self):
    path = self._write(
        "bad_brace.cc",
        "int Foo()\n"
        "{\n"
        "  return 1;\n"
        "}\n",
    )
    errors = check_format.check_file(path)
    self.assertTrue(any("clang-format mismatch" in err for err in errors),
                    errors)


if __name__ == "__main__":
  unittest.main()
