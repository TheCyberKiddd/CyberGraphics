# Specifications

Long-lived format and interface specifications live here. A design note covers
one phase; a specification covers a contract that outlives it.

Planned documents:

| Document | Phase | Contents |
|---|---|---|
| `rhi.md` | 10 | Object model, synchronization, threading, capabilities, error handling. |
| `formats.md` | 11 | The format table: channels, bytes per pixel or block, sRGB, depth/stencil. |
| `shader-language.md` | 16 | Grammar, types, semantics, IR, code-generation rules. |
| `material-format.md` | 18 | Material definitions, parameters, instances, serialization. |
| `render-graph.md` | 20 | Pass declaration, lifetime, aliasing, barriers, culling. |
| `plugin-abi.md` | 25 | The versioned C ABI, structs, function table, capabilities. |
| `asset-formats.md` | 24 | Cooked mesh, texture, and material containers. |
| `capture-format.md` | 39 | Frame capture container and replay semantics. |

## Rules

- A specification is normative: code and tests follow it, not the reverse.
- Every format has a version field, a size field where applicable, and a
  checksum or a validation rule for truncated input.
- Every parser treats input as hostile.
- When a specification changes incompatibly, bump the version and write an
  ADR.
