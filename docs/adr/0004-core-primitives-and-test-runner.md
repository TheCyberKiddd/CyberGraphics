# 0004 — Core primitive contracts and the project test runner

- **Status:** accepted
- **Date:** 2026-10-20
- **Deciders:** CyberEngine (owner)
- **Related:** Phase 2, [design note](../design/phase-02.md),
  [error model](../architecture/error-model.md)

## Context

Every later module needs the same primitives: a recoverable error type, a
failure path that does not allocate, a bounded logger, stable identifiers, and
a test runner that does not bring a third-party dependency into the tree. These
choices are public C++ interfaces inside the engine and are expensive to change
after `cyber_math`, `cyber_memory`, and `cyber_platform` depend on them.

The project is C++20, exceptions and RTTI are disabled in runtime modules, and
only the standard library and OS/driver headers are allowed. The test runner is
a developer tool, not a shipping component.

## Decision

1. **Use a hand-written `Result<T, E>` tagged union.** The result is never
   valueless, works with non-default-constructible values, and does not depend
   on C++23. `T` and `E` must be nothrow move constructible. Copy operations are
   available only when both alternatives are nothrow copy constructible.
2. **Provide `Result<void, E>` and `Status<E>`.** Error-only flows should not be
   forced through a dummy value type.
3. **Use `cyber::core::Error` for the common case.** It is a two-word
   non-owning value (`ErrorCode`, `const char*`). Modules may still define their
   own error enums.
4. **Make assertions allocation-free and Shipping-aware.** `CG_ASSERT` is
   compiled out in Shipping; `CG_VERIFY` evaluates in all builds. Failure
   formatting uses a caller-provided stack buffer.
5. **Use a synchronous, bounded logger.** Fixed 512-byte messages, eight sink
   slots, atomic thresholds, and a spinlock around sink dispatch. No heap
   allocation in the logger or built-in sinks. Sinks are non-owning and must not
   re-enter the logger.
6. **Expose build configuration, not platform probing.** `BuildConfig` reads
   compile definitions for build kind, platform, sanitizers, assertions, and
   version. Environment and file configuration belong to `cyber_platform`.
7. **Use xxHash64 for non-cryptographic hashing.** One-shot implementation with
   canonical vectors. Document that it is not collision-resistant against
   adversaries.
8. **Build the test runner in `tools/test`.** It registers tests with
   `CG_TEST`, supports filtering/listing/repeat/seed/shuffle, emits console, TAP,
   or JUnit XML, and returns 0/1/2. It links into test executables only and is
   never installed.
9. **Compile test tools with exceptions enabled.** Runtime modules remain
   `-fno-exceptions`/`/EHs-c-`; the test runner and test bodies may use the
   standard library normally.

## Consequences

### Positive

- Later modules have one error model and one test model to target.
- The failure path is bounded and does not allocate.
- Tests can move to TAP or JUnit without adding a dependency.
- The engine can bridge logs through a function pointer without exposing C++
  ownership.
- The public core API is small enough to audit.

### Negative

- `Result` is move-only for value types that can throw on copy, which is stricter
  than `std::expected`.
- Accessor misuse is only asserted outside Shipping.
- The logger is synchronous; a slow sink blocks the calling thread.
- The test runner is project-owned code that must be maintained and tested.
- xxHash64 is not suitable for security decisions.

### Neutral

- The logger's sink table and message size are compile-time limits.
- The test runner's output formats are intentionally small subsets of TAP and
  JUnit.
- `BuildConfig` will grow as new platforms and sanitizers are added.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| `std::variant<T, E>` | Implicit valueless states and no clear error/value invariant. |
| `std::expected` | C++23; not available in the C++20 baseline and not consistently supported by CI toolchains. |
| Throwing `Result` copy | Runtime code is compiled without exceptions; a throwing copy terminates. |
| Heap-allocated logger | Allocation on the failure path and an avoidable lifetime problem. |
| Third-party test framework | Adds a dependency to every test target and conflicts with the repository policy. |
| Asynchronous logger now | Requires a thread and shutdown policy that belong to later modules. |
| No project test runner | CTest alone cannot list, filter, repeat, or format project tests consistently across platforms. |

## Follow-up

- Phase 3 adds math types and extends the runner with floating-point tolerance
  helpers.
- Phase 4 adds allocators and may add an allocation-counting test helper.
- Phase 5 adds platform time and thread identity; the logger can then include a
  timestamp and thread ID without changing the sink contract.
- Phase 15/36 adds benchmark integration; the runner stays a correctness tool.
- Phase 25 maps core error codes to the stable C ABI result enum.
