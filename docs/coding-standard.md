# Coding standard

The formatter decides layout. This document decides names, ownership, and the
rules that a formatter cannot enforce.

## Language

- C++20.
- Runtime code is compiled with exceptions and RTTI disabled.
- Only the C++ standard library, operating system headers, and driver headers.
- No third-party runtime libraries.

## Names

| Entity | Style | Example |
|---|---|---|
| Namespace root | lower case | `cyber` |
| Module namespace | lower case | `cyber::math`, `cyber::rhi` |
| Types, structs, enums | PascalCase | `Mat4`, `DeviceDesc`, `Format` |
| Functions and methods | PascalCase | `Invert`, `CreateBuffer` |
| Local variables and parameters | camelCase | `worldMatrix`, `frameIndex` |
| Member variables | `m_` + camelCase | `m_device`, `m_frameIndex` |
| Constants | `k` + PascalCase | `kMaxFramesInFlight` |
| Enum values | PascalCase | `Format::RGBA8Unorm` |
| Macros | `CG_` + SCREAMING_SNAKE | `CG_ASSERT`, `CG_DEBUG` |
| Files | snake_case | `render_graph.cpp`, `vec3.h` |
| Tests | `_test.cpp` | `mat4_test.cpp` |
| Benchmarks | `_bench.cpp` | `mat4_bench.cpp` |

## Headers

- Use `#pragma once`.
- Public headers live under `engines/<module>/include/cyber/<short>/`.
- A public header compiles on its own; it includes everything it uses.
- No `using namespace` in a header.
- No anonymous namespace in a header.
- Keep implementation details out of public headers. If a type needs private
  state, use the pimpl pattern or an opaque handle at the ABI boundary.
- Include order: own header first in a `.cpp`, then quoted project headers,
  then `<cyber/...>` headers, then standard and system headers. The formatter
  groups them.

## Ownership and lifetimes

- Raw pointers and references are non-owning unless the name says otherwise.
- GPU and scene objects use generation-counted handles, not raw pointers.
- Prefer explicit allocators and arenas over hidden allocation.
- `std::unique_ptr` is acceptable for ownership in tools and non-hot paths.
  Avoid `std::shared_ptr` in runtime hot paths.
- Never let ownership cross the C ABI. The host owns host memory; the plugin
  owns plugin memory; the contract says which is which.
- Deferred destruction is explicit: a resource is released after the fence that
  guarantees the GPU is done with it.

## Errors

- Programmer error: `CG_ASSERT`.
- Recoverable error: `Result<T, E>`.
- C ABI: `cg_result` plus a message callback.
- No exceptions in runtime code.
- Do not ignore a `Result`; annotate deliberate ignores.

## Functions

- Mark non-throwing functions `noexcept`.
- Mark pure query functions `[[nodiscard]]`.
- Prefer free functions in a namespace over classes with only static methods.
- Keep functions short enough to read; a function that needs a scroll bar is a
  design smell, not a formatting problem.

## Integers and sizes

- Use fixed-width types for data that crosses a boundary or is serialized.
- Use `std::size_t` for sizes and indices in memory, unless a narrower type is
  part of a format.
- Check for overflow before it happens; do not rely on unsigned wraparound.
- Avoid `int` in public interfaces where the width matters.

## Comments and documentation

- Explain **why**, not what. The code says what.
- Public headers use `//` comments directly above the declaration.
- TODO format: `// TODO(name): reason and exit condition`.
- Do not leave commented-out code. Delete it; version control remembers.
- Design notes and ADRs carry the long explanations.

## Tests

- Every public function has a test.
- Test names describe behavior, not implementation.
- Use a tolerance helper for floating-point comparisons; never compare floats
  with `==` unless the operation is exact by definition.
- A bug fix gets a regression test that fails before the fix.
- Golden images are deterministic: fixed resolution, fixed seed, fixed scene.

## Formatting

- `.clang-format` is authoritative.
- 4 spaces, 100 columns, Allman braces.
- Run `.\scripts\format.ps1` before committing.
- CI runs `clang-format --dry-run --Werror` when the formatter is available.

## Commits

Use a short scope prefix and the imperative mood:

```text
core: add version metadata
math: test reverse-Z projection corners
rhi: reject destroyed buffer handles
docs: record the render graph ADR
```

One logical change per commit. Every commit builds and passes the tests.
