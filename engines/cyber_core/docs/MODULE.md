# cyber_core — module contract

**Kind:** foundation
**Layer:** 0
**Order:** 0
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Fixed-width types, assertions, logging, Result, configuration switches, versioning, hashing, and other dependency-free primitives.

## Public API

`include/cyber/core/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

| Header | Contents |
|---|---|
| `types.h` | Fixed-width aliases, `usize`, `isize`, `Byte`, `kInvalidIndex`. |
| `build_config.h` | Build kind, platform, sanitizer flags, version view. |
| `error.h` | `ErrorCode`, `Error`, name helpers. |
| `result.h` | `Result<T, E>`, `Result<void, E>`, `Status<E>`. |
| `assert.h` | `CG_ASSERT`, `CG_ASSERT_MSG`, `CG_VERIFY`, failure formatting. |
| `log.h` | Levels, channels, `LogRecord`, `LogSink`, `Logger`, `CG_LOG`. |
| `log_sinks.h` | `ConsoleSink`, `CallbackSink`, `FormatLogLine`. |
| `hash.h` | `HashBytes`, `HashString`, `HashCombine`. |
| `version.h` | Generated version metadata. |

## Dependencies

Allowed: None

Forbidden: cyber_platform, cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- No dependency on any other CyberGraphics module.
- Assertions and logging must not allocate on the failure path.
- Public headers are self-contained and compile on their own.
- `Result` is never valueless; exactly one of the value or the error is engaged.
- `Result` values and errors must be nothrow move constructible.
- Logger messages are capped at 512 bytes and the logger holds at most 8 sinks.
- Sinks are non-owning and must not re-enter the same logger.

## Non-goals

- Containers and allocators (cyber_memory).
- Math types (cyber_math).
- Platform services (cyber_platform).
- Rendering (cyber_rhi, cyber_renderer).
- Asynchronous logging threads.
- Cryptographic hashing.

## Tests

Unit tests for Result, assertion formatting, log channels and sinks, hashing
vectors, build configuration, version macros, and public-header compilation.
Tests run through the project runner in `tools/test`.

## Budgets

Assert path: no allocation. Log path: thread-safe and bounded, no allocation in
the logger or built-in sinks. `HashBytes`: one shot, no allocation.

## Roadmap

Phase 2.
