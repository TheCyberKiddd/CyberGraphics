# Design notes

One note per roadmap phase, written **before** implementation:
`phase-NN-name.md`.

## Template

```markdown
# Phase NN — <name>

Date: YYYY-MM-DD
Status: draft | approved | implemented

## What this phase builds

## What it deliberately leaves out

## Interfaces it touches

## Design

The important decisions, data flow, and invariants.

## Alternatives considered

## Risks

## Test plan

Which tests prove the done-when line.

## Sources

Specifications, papers, and talks. Ideas only; no copied code.
```

## Rules

- Write the note before the code. A design note written afterwards is a
  rationalization, not a design.
- Keep it short enough to review in one sitting.
- Link the ADRs it creates or depends on.
- If the phase changes a public interface, the note must say why and what
  migrates.
- The phase report in `docs/reports/` is the evidence; the design note is the
  plan. Do not merge them.
