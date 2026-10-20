# Module contract

Every module has a contract at `engines/<module>/docs/MODULE.md`. The contract
is written before code and updated when the public interface changes. It is
short on purpose: if it cannot be read in five minutes, it is not a contract.

## Template

```markdown
# cyber_example — module contract

**Kind:** foundation | platform | service | graphics | product
**Layer:** 0..4
**Order:** integer, unique
**Status:** planned | scaffolded | active | stable | deprecated
**Owner:** @handle

## Purpose

One paragraph. What problem does this module solve?

## Public API

`include/cyber/example/`

Only these headers are public. Everything in `src/` is private. Public headers
compile on their own.

## Dependencies

Allowed: cyber_core, cyber_math
Forbidden: cyber_renderer, cyber_graphics

## Invariants

- Facts that must remain true.
- Rules a reviewer can check.

## Non-goals

- Things this module deliberately does not do.
- Where those things belong instead.

## Tests

Unit, integration, golden, fuzz, stress, or benchmark coverage.

## Budgets

CPU, GPU, memory, latency, or build-time limits. Write "none yet" if the
module has no measured budget; do not invent numbers.

## Roadmap

Phases 1, 2.
```

## Checklist for a new module

- [ ] One responsibility, stated in one sentence.
- [ ] Layer and order are correct; dependencies point downward.
- [ ] Public API path is declared and contains only public headers.
- [ ] Forbidden dependencies are explicit.
- [ ] Invariants are testable.
- [ ] Non-goals say where the excluded work belongs.
- [ ] Test strategy is named, even if tests arrive later.
- [ ] Budgets are measured or marked as "none yet".
- [ ] The module is listed in `engines/modules.json` and `MODULES.md`.
- [ ] No CMake target exists before the module's phase starts.

## Review questions

1. Could this module be understood without reading another module's `src/`?
2. Does any public header include a private header?
3. Is there a hidden global or singleton?
4. Does the module own its data, or does it borrow another module's state?
5. If the module were deleted, which modules would fail to build? That list
   must match the declared dependents, not exceed it.
6. Is there a test that would fail if the contract were violated?
