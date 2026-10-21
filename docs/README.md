# CyberGraphics documentation

This directory is the project's memory. Code is only half of the design; the
other half is the written contract, the decision record, and the evidence that
a phase actually works.

## Start here

| Document | Purpose |
|---|---|
| [ROADMAP.md](ROADMAP.md) | Master checklist for all 44 phases, linked to the two binding plans. |
| [requirements.md](requirements.md) | Project-level functional and non-functional requirements. |
| [architecture/overview.md](architecture/overview.md) | The module map, layers, and dependency law. |
| [architecture/integration.md](architecture/integration.md) | How Cyber Engine consumes CyberGraphics as a submodule or package. |
| [architecture/layers.md](architecture/layers.md) | The exact layer and order rules and how CI checks them. |
| [architecture/module-contract.md](architecture/module-contract.md) | Template and checklist for every module. |
| [architecture/threading.md](architecture/threading.md) | Thread ownership and frame model. |
| [architecture/error-model.md](architecture/error-model.md) | `Result`, assertions, error codes, no exceptions. |
| [architecture/abi-policy.md](architecture/abi-policy.md) | The stable plugin boundary. |
| [math-conventions.md](math-conventions.md) | Handedness, matrices, depth, units, color. |
| [coding-standard.md](coding-standard.md) | Naming, headers, ownership, style. |
| [glossary.md](glossary.md) | Terms used in code and docs. |

## Working documents

| Directory | Contents |
|---|---|
| [graphics/](graphics/) | The binding CyberGraphics roadmap and per-phase build guide. |
| [design/](design/) | One design note per roadmap phase, written before code. |
| [adr/](adr/) | Architecture decision records. |
| [notes/](notes/) | Reading notes and sources for each phase. |
| [reports/](reports/) | Phase reports with evidence for the done-when line. |
| [specs/](specs/) | RHI, shader language, and asset format specifications. |
| [testing/](testing/) | Test layers, golden images, sanitizers, and CI gates. |
| [perf/](perf/) | Performance log and budgets. |

## Rules for documentation

- Write design notes before implementation, not after.
- List sources: specifications, papers, talks. Ideas are allowed; copied code
  is not.
- Record decisions as ADRs when they are expensive to reverse.
- Phase reports must name the machine and include the evidence, not a promise.
- Keep the glossary current; inconsistent vocabulary becomes inconsistent code.
