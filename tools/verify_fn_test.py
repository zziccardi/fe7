#!/usr/bin/env python3
"""Unit tests for tools/verify_fn.py."""

import unittest

from tools import verify_fn


class VerifyFnTest(unittest.TestCase):

  def test_build_steps_without_rom(self):
    steps = verify_fn.build_steps(
        rom_path=None, frames=60, trace_addrs=None, record_traces=None
    )
    self.assertEqual(len(steps), 3)
    self.assertIn("tools/check_format.py", steps[0])
    self.assertIn("tools/check_agent_rules.py", steps[1])
    self.assertIn("--config=asan", steps[2])
    self.assertIn("--config=headless", steps[2])

  def test_build_steps_with_rom_and_traces(self):
    steps = verify_fn.build_steps(
        rom_path="fe7_us.gba",
        frames=120,
        trace_addrs="0x08000BE0",
        record_traces="/tmp/out.fe7t",
    )
    self.assertEqual(len(steps), 4)
    headless_step = steps[3]
    self.assertIn("//runtime:fe7_headless", headless_step)
    self.assertIn("--frames=120", headless_step)
    self.assertIn("--dump_checkpoints", headless_step)
    self.assertTrue(
        any(arg.startswith("--trace_addrs=0x08000BE0") for arg in headless_step)
    )
    self.assertTrue(
        any(arg.startswith("--record_traces=") for arg in headless_step)
    )


if __name__ == "__main__":
  unittest.main()
