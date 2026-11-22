"""Unit tests for tools/e2e_lockstep.py (zero retail ROM required)."""

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import e2e_lockstep


class E2eLockstepTest(unittest.TestCase):

  def test_parse_and_compare_matching_checkpoints(self):
    sample = (
        "Checkpoint frame=120 ewram=0x1111 iwram=0x2222 fb=0x3333\n"
        "Checkpoint frame=240 ewram=0xAAAA iwram=0xBBBB fb=0xCCCC\n"
    )
    cp_a = e2e_lockstep.parse_checkpoints(sample)
    cp_b = e2e_lockstep.parse_checkpoints(sample)
    self.assertEqual(len(cp_a), 2)
    self.assertEqual(cp_a[120].ewram, "0x1111")
    self.assertEqual(e2e_lockstep.compare_checkpoints(cp_a, cp_b), [])

  def test_compare_detects_hash_mismatch_and_missing_frame(self):
    base = e2e_lockstep.parse_checkpoints(
        "Checkpoint frame=60 ewram=0x1 iwram=0x2 fb=0x3\n"
        "Checkpoint frame=120 ewram=0x4 iwram=0x5 fb=0x6\n"
    )
    nat = e2e_lockstep.parse_checkpoints(
        "Checkpoint frame=60 ewram=0x1 iwram=0x2 fb=0x999\n"
    )
    diffs = e2e_lockstep.compare_checkpoints(base, nat)
    self.assertEqual(len(diffs), 2)
    self.assertIn("frame 60: mismatch", diffs[0])
    self.assertIn("frame 120: missing checkpoint", diffs[1])

  def test_parse_fallbacks(self):
    out = (
        "Checkpoint frame=60 ewram=0x1 iwram=0x2 fb=0x3\n"
        "Fallback addresses hit: 42\n"
        "  0x8001234: 500\n"
        "  0x8005678: 120\n"
    )
    total, entries = e2e_lockstep.parse_fallbacks(out)
    self.assertEqual(total, 42)
    self.assertEqual(entries, [(0x08001234, 500), (0x08005678, 120)])

  def test_build_headless_cmd(self):
    cmd = e2e_lockstep.build_headless_cmd(
        binary="./fe7_headless",
        rom="fe7_us.gba",
        frames=300,
        checkpoint_frames="100,200,300",
        input_script="boot.fe7in",
        register_native=True,
        fallback_top=15,
    )
    self.assertIn("--input_script=boot.fe7in", cmd)
    self.assertIn("--checkpoint_frames=100,200,300", cmd)
    self.assertIn("--report_fallbacks", cmd)
    self.assertIn("--fallback_top=15", cmd)


if __name__ == "__main__":
  unittest.main()
