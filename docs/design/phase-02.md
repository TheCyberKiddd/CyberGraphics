# Phase 2 — Core primitives and test runner

Date: 2026-10-20
Status: implementation
Related: ADR 0001, ADR 0003, ADR 0004,
[error model](../architecture/error-model.md), [testing strategy](../testing/README.md)

## What this phase builds

`cyber_core` stops being a version stamp and becomes the dependency-free base
that every later module can use:

- fixed-width aliases and common scalar names;
- a compile-time build configuration view;
- `Result<T, E>` and `Result<void, E>` for recoverable errors;
- assertions and verified expressions;
- a bounded, thread-safe logger with levels, channels, and sinks;
- a stable non-cryptographic hash (`xxHash64`) and hash combining;
- the project test runner in `tools/test`.

The phase also moves every existing test onto the runner and proves the runner
in CI on Windows and Linux.

## What it deliberately leaves out

- Allocators, arenas, containers, and string types. They belong to
  `cyber_memory`.
- Platform time, thread IDs, environment variables, files, and console setup.
  They belong to `cyber_platform`.
- An asynchronous logging thread. The logger is synchronous and bounded; a
  future service can drain a lock-free queue without changing the sink API.
- Cryptographic hashing. `xxHash64` is for asset IDs, caches, and hash tables,
  never for signatures or passwords.
- The C ABI. `include/cybergraphics/` stays reserved for Phase 25.
- A property-based testing library, mocking framework, or benchmark harness.

## Interfaces it touches

Public headers added under `engines/cyber_core/include/cyber/core/`:

| Header | Responsibility |
|---|---|
| `types.h` | Fixed-width aliases, `usize`, `isize`, `Byte`, invalid index. |
| `build_config.h` | Build kind, platform, sanitizer flags, version view. |
| `error.h` | Small `ErrorCode` and `Error` value types. |
| `result.h` | `Result<T, E>`, `Result<void, E>`, `Status<E>`. |
| `assert.h` | `CG_ASSERT`, `CG_ASSERT_MSG`, `CG_VERIFY`, failure formatting. |
| `log.h` | Levels, channels, records, sinks, `Logger`, default logger. |
| `log_sinks.h` | `ConsoleSink`, `CallbackSink`, line formatting. |
| `hash.h` | `HashBytes`, `HashString`, `HashCombine`. |
| `version.h` | Existing generated version metadata. |

The test runner lives at `tools/test/`. Its header is
`tools/test/include/cyber/test/test.h`, and it is never installed or linked into
a runtime target.

## Design

### Result

`Result` uses a tagged union, not `std::variant` or C++23 `std::expected`.
Both alternatives either require a default-constructible value, introduce
valueless states, or are unavailable in C++20. The invariant is explicit:

```text
Result is either an engaged T or an engaged E. Never both. Never neither.
```

`T` and `E` must be nothrow move constructible because runtime code has no
exceptions. Copy construction and copy assignment are conditionally available
only when both alternatives are nothrow copy constructible. This keeps
`Result<int, Error>` copyable while preventing a hidden throwing copy of
`std::string` in a no-exception build.

`Result<void, E>` is a specialization with the same error access and no value
access. `Status<E>` is an alias for it. Accessors assert the active alternative
in non-shipping builds; misuse in Shipping is undefined behavior, exactly like
a bad `std::optional` dereference.

`cyber::core::Error` is a two-word, trivially copyable value: a stable
`ErrorCode` and a non-owning `const char*` message. Modules may use it or define
their own error enum; the `Result` template does not care.

### Assertions

`CG_ASSERT` is active in Debug and Release and compiled out in Shipping.
`CG_VERIFY` evaluates its expression in every build and reports in non-Shipping
builds. Failure formatting uses a caller-provided fixed buffer and
`std::snprintf`; no heap allocation is performed by our code. The failure path
writes pieces to `stderr`, breaks into the debugger when one is present, and
aborts.

### Logging

The logger is synchronous. `Log` formats into a fixed `LogRecord` on the stack,
then calls registered sinks under a small spinlock. The bounds are compile-time
constants:

- 512-byte message, truncated with an explicit `truncated` flag;
- 8 sink slots;
- no allocation in the logger or the built-in sinks;
- level and channel thresholds stored in atomics so filtering does not lock.

Sinks are non-owning pointers. The host must keep a sink alive until it is
removed or the logger is destroyed. A sink must not call back into the same
logger; the lock is held while sinks run. `DefaultLogger()` starts with no
sinks so a library never writes to the host's console without permission.

`ConsoleSink` writes one line per record to a `std::FILE*` (default `stderr`).
`CallbackSink` forwards to a plain function pointer and user data; it is the
integration point for a host log bridge and for tests.

### Build configuration

`BuildConfig` exposes the CMake-selected build kind, platform, sanitizer flags,
assertion availability, and version. It reads compile definitions only. It does
not probe the OS and does not read environment variables; those are platform
services.

### Hashing

`HashBytes` is a one-shot xxHash64 implementation written from the published
algorithm. It handles unaligned input, little-endian loads, 32-bit and 64-bit
tails, and the byte tail. `HashString` is a `std::string_view` wrapper.
`HashCombine` is a stable mixer for incremental IDs. None of these are
cryptographic.

### Test runner

The runner is a small static library plus a `main` in `tools/test`. Each test
executable links it and contains only test registration and test bodies.

- `CG_TEST(name)` registers one function with file and line.
- `CG_EXPECT_*` records a failure and continues.
- `CG_REQUIRE` records a failure and returns from the test.
- CLI: `--list`, `--filter`, `--repeat`, `--seed`, `--shuffle`, `--format`.
- Output: console, TAP 13, or JUnit XML.
- Exit codes: 0 pass, 1 test failure, 2 usage or configuration error.

The runner is a developer tool, so it uses the standard library normally
(`std::vector`, `std::string`, streams) and compiles with exceptions enabled.
The runtime module it tests still compiles with exceptions and RTTI disabled.
The runner has no third-party dependency and is never installed.

## Alternatives considered

| Alternative | Why not now |
|---|---|
| `std::variant<T, E>` | No explicit error/value invariant, requires default-constructible alternatives for some operations, and wrong-type access is awkward without exceptions. |
| C++23 `std::expected` | Project language level is C++20 and MSVC/library support is not guaranteed across CI. |
| Heap-allocated pimpl for `Logger` | Allocation and an extra indirection in the failure path; the fixed sink table is bounded by design. |
| Asynchronous logger | Needs a thread, queue, and shutdown policy; belongs after `cyber_platform` and `cyber_jobs`. |
| Third-party test framework | Violates the no-third-party runtime policy and adds a dependency to every test target. |
| FNV-1a only | Simple but weak for larger asset keys; xxHash64 gives a better distribution at comparable cost. |
| `std::function` in sinks | May allocate and has no stable ABI; a function pointer plus user data is explicit. |

## Risks

- **Sink lifetime:** non-owning pointers are a deliberate performance choice.
  Debug validation rejects null and duplicate registration, but cannot detect a
  dangling pointer.
- **Deadlock:** a sink that logs through the same logger will self-deadlock.
  The contract forbids it; a future debug-only reentrancy flag can catch it.
- **Result misuse:** accessors assert only in non-Shipping builds. Callers must
  branch on `IsOk()`; `[[nodiscard]]` catches ignored results at compile time.
- **Hash collisions:** xxHash64 is not cryptographic and is not collision
  resistant against adversaries. Never use it for security.
- **Test-order dependence:** test registration is deterministic. `--shuffle`
  exists to expose hidden order assumptions, but is off by default so CI output
  is stable.

## Test plan

1. `Result`: ok/error construction, value/error access, move-only values,
   conditional copy, destructor behavior, `Result<void, E>`.
2. Assertions: message formatting, truncation, null message, `CG_VERIFY` side
   effects, successful `CG_ASSERT`.
3. Logging: level/channel names, global and per-channel filtering, sink
   add/remove and capacity, formatting/truncation, callback sink, concurrent
   writers.
4. Hashing: canonical xxHash64 vectors, long input, seed sensitivity,
   unaligned input, empty input, combine determinism.
5. Build configuration: build kind, platform, sanitizer flags, version.
6. Version: generated constants, string, `BuildConfig` agreement.
7. Public headers: every `cyber/core` header compiles together.
8. Test runner: option parsing, filter matching, XML escaping, registry.
9. All presets: Debug, Release, Shipping, Debug+ASan; Linux clang+UBSan in CI.
10. Package and source consumers still build after the new public headers.

## Sources

- ISO C++20 standard: `std::construct_at`, `std::destroy_at`, `std::string_view`,
  `std::span`, `std::from_chars`, `std::atomic`, `std::chrono`.
- xxHash specification and canonical XXH64 test vectors.
- Test Anything Protocol version 13.
- JUnit XML schema used by common CI systems.
- CyberGraphics error model, coding standard, and testing strategy.
