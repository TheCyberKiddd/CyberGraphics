# Contributing to CyberGraphics

## Before you change code

1. Read the module contract in `engines/<module>/docs/MODULE.md`.
2. Check the architecture rules in `docs/architecture/overview.md`.
3. If the change affects a public interface, write an ADR in `docs/adr/` first.
4. If the change starts a roadmap phase, write the design note in
   `docs/design/` before the implementation.

## Rules

- Only the C++ standard library and operating system headers. No third-party
  rendering libraries, no copied engine code. Ideas come from specifications,
  papers, and talks, and sources are listed in the design note.
- Dependencies point downward in `engines/modules.json`. Do not add an upward
  link, and do not include another module's `src/`.
- Public headers must compile on their own and live under
  `engines/<module>/include/cyber/<short>/`.
- Runtime modules compile with exceptions and RTTI disabled. Use
  `cyber::core::Result` for recoverable errors and assertions for programmer
  errors.
- No global mutable state. Pass contexts explicitly.
- Every commit builds and passes the tests. Every phase ends with evidence.

## Commit and review checklist

- [ ] `python scripts/check_structure.py` passes.
- [ ] `python scripts/layer_check.py` passes.
- [ ] `cmake --build --preset debug` succeeds.
- [ ] `ctest --preset debug --output-on-failure` passes.
- [ ] New public headers have a module contract update.
- [ ] A design note or ADR exists for architectural decisions.
- [ ] `CHANGELOG.md` is updated for user-visible changes.

## Formatting

Run `.\scripts\format.ps1` before committing. The canonical style is in
`.clang-format`; the naming and ownership rules are in
`docs/coding-standard.md`.

## Documentation

- `docs/design/` — one note per phase, written before code.
- `docs/adr/` — lasting decisions.
- `docs/notes/` — reading notes with sources.
- `docs/reports/` — phase reports with evidence.
- `docs/perf/` — measured performance results.

## License

By contributing, you agree that your contribution is owned by CyberEngine
under the terms in `LICENSE`.
