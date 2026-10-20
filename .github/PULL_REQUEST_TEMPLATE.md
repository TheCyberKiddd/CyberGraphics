## Summary

Describe what changed and which roadmap phase it belongs to.

## Checklist

- [ ] The module contract still matches the code.
- [ ] `python scripts/check_structure.py` passes.
- [ ] `python scripts/layer_check.py` passes.
- [ ] `python scripts/check_format.py` passes.
- [ ] `cmake --build --preset debug` succeeds.
- [ ] `ctest --preset debug --output-on-failure` passes.
- [ ] A design note or ADR exists if an interface or dependency changed.
- [ ] Public target or header changes pass the installed package consumer.
- [ ] `CHANGELOG.md` is updated for user-visible changes.
- [ ] No third-party runtime code was added.
