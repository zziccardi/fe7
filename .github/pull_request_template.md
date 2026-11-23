## Summary

<!-- Describe what this PR changes and why. -->

## Verification

- [ ] `bazel test --config=asan --config=headless //...` passes
- [ ] `python3 tools/check_format.py` passes
- [ ] `python3 tools/check_agent_rules.py` passes
- [ ] `python3 -m unittest discover -s tools -p '*_test.py' -v` passes
- [ ] No copyrighted ROM bytes, extracted assets, saves, or `.fe7t` traces are committed
- [ ] *(If modifying `game/`)* `//runtime:fe7_diff` reports 0 mismatches against a local FE7 US (`AE7E`) ROM
