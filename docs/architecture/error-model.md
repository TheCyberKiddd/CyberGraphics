# Error model

CyberGraphics uses three error mechanisms, each for a different kind of
failure. Mixing them is how error handling becomes unreadable.

## 1. Assertions: programmer errors

Use `CG_ASSERT` for conditions that must be true if the code is correct:
null pointers that cannot be null, invalid enum values, violated invariants.
Assertions report file and line and break into the debugger. They are compiled
out in Shipping. `CG_ASSERT_MSG(expression, message)` adds a fixed message.

`CG_VERIFY` evaluates the expression in all builds and reports in non-Shipping
builds; use it when the expression has a side effect that must still happen in
Shipping.

Assertions must not allocate on the failure path. A failed assertion is often
the only diagnostic available. `FormatAssertionMessage` writes into a
caller-provided stack buffer; the failure path uses no heap.

The macros live in `cyber/core/assert.h`.

## 2. `Result<T, E>`: recoverable errors

Use `cyber::core::Result<T, E>` for expected failure: a file does not exist, a
shader fails to compile, a GPU does not support a feature. The caller must
handle or propagate it. Runtime code is compiled without exceptions, so
`Result` is the only recoverable path.

```cpp
cyber::core::Result<TextureHandle, cyber::core::Error> LoadTexture(const char* path);
```

Rules:

- `Result` is never valueless: exactly one of the value or the error is engaged.
- `T` and `E` must be nothrow move constructible. Copy operations are available
  only when both are nothrow copy constructible, so `Result<std::string, E>` is
  move-only.
- Use `Result<void, E>` (`cyber::core::Status<E>`) when there is no value.
- `E` is a small error type with a stable code and a message.
  `cyber::core::Error` carries an `ErrorCode` and a non-owning `const char*`.
- Do not use `Result` for programmer errors.
- Do not ignore a `Result`; mark deliberate ignores with a comment. The class is
  `[[nodiscard]]`.
- Accessors (`Value`, `Error`) assert the active alternative outside Shipping.
- Error messages are for humans; codes are for tools and tests.

## 3. Error codes at the C boundary

The stable API in `include/cybergraphics/` cannot expose C++ types. Functions
return a `cg_result` code and may call a message callback with a
human-readable string. No exception, no STL type, and no C++ object crosses the
boundary.

```c
typedef enum cg_result {
    CG_OK = 0,
    CG_ERROR_INVALID_ARGUMENT = 1,
    CG_ERROR_UNSUPPORTED = 2,
    CG_ERROR_DEVICE_LOST = 3,
    CG_ERROR_OUT_OF_MEMORY = 4,
    CG_ERROR_INTERNAL = 5
} cg_result;
```

## Logging

The logger has levels (`Trace`, `Debug`, `Info`, `Warning`, `Error`, `Fatal`,
`Off`) and channels (`Core`, `Platform`, `Rhi`, `Renderer`, `Assets`, `Shaders`,
`Jobs`, `Stability`). Records carry channel, level, file, and line and a fixed
512-byte message. The logger is thread-safe, bounded to eight sinks, and does not
allocate on the hot path. Sinks are non-owning; `ConsoleSink` and `CallbackSink`
live in `cyber/core/log_sinks.h`. Use `CG_LOG(level, channel, ...)` for the
process-wide `DefaultLogger`. The host installs a sink; the default logger starts
with none.

## Validation

Debug-only validation catches invalid API use: destroyed handles, missing
barriers, mismatched formats, incomplete bindings. Validation is a teaching
tool during development and a test oracle in CI; it is not a substitute for
correct code.

## What not to do

- Do not throw exceptions in runtime code.
- Do not return a bool when the caller needs to know why it failed.
- Do not log an error and continue as if nothing happened.
- Do not put a formatted string in a hot path to report a non-error.
- Do not expose `std::string`, `std::vector`, or C++ exceptions across the C
  boundary.
