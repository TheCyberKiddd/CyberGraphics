# Phase reports

One report per phase: `phase-NN.md`. The report is the evidence that the
phase's "done when" line is true. A phase is not done because the code compiles;
it is done when the proof is written down.

## Template

```markdown
# Phase NN report: <name>

Date: YYYY-MM-DD
Commits: <first>..<last>
Machine: CPU, GPU, driver, OS, compiler

## What was built

## Decisions

Link the ADRs.

## Evidence for done-when

Test names, command lines, screenshots, timings, and the exact machine they ran
on. Put images in `docs/reports/images/` or link to `tests/images/`.

## Known gaps

Be explicit. A known gap is acceptable; a hidden one is not.

## Sources used

## What the next phase needs from this one

Interfaces, invariants, and files the next phase can rely on.
```

## Rules

- Evidence, not adjectives. "Fast" is not evidence; "1.8 ms on i5-6400" is.
- Name the machine and the build configuration.
- If a done-when line is only partly met, say so and do not start the next
  phase.
- Link the commit range so the report is reproducible.
- Keep screenshots and logs small enough to live in the repository.
