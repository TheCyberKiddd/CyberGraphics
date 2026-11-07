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
| [debugging.md](debugging.md) | D3D12 validation, object labels, and PIX/RenderDoc capture notes. |

## Working documents

| Directory | Contents |
|---|---|
| [graphics/](graphics/) | The binding CyberGraphics roadmap and per-phase build guide. |
| [design/](design/) | One design note per roadmap phase, written before code. |
| [adr/](adr/) | Architecture decision records. |
| [notes/](notes/) | Reading notes and sources for each phase. |
| [reports/](reports/) | Phase reports with evidence for the done-when line. |
| [graphics/PHASE_17.md](graphics/PHASE_17.md) | Phase 17 implementation summary and validation status. |
| [graphics/PHASE_18.md](graphics/PHASE_18.md) | Phase 18 material-system implementation summary and validation status. |
| [reports/phase-18.md](reports/phase-18.md) | Phase 18 report and local validation evidence. |
| [graphics/PHASE_20.md](graphics/PHASE_20.md) | Phase 20 render-graph implementation summary and validation status. |
| [reports/phase-20.md](reports/phase-20.md) | Phase 20 report and local validation evidence. |
| [graphics/PHASE_21.md](graphics/PHASE_21.md) | Phase 21 render-thread implementation summary and validation status. |
| [graphics/PHASE_24.md](graphics/PHASE_24.md) | Phase 24 asset-pipeline implementation summary and validation status. |
| [reports/phase-24.md](reports/phase-24.md) | Phase 24 report and local validation evidence. |
| [reports/phase-21.md](reports/phase-21.md) | Phase 21 report and local validation evidence. |
| [graphics/PHASE_15.md](graphics/PHASE_15.md) | Phase 15 implementation summary and capture status. |
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
