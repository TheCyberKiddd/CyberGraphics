# Phase 2 report: core primitives and the test runner

Date: 2026-10-20
Commits: `ea511d4..ff72526` (runner, core primitives, consumers, docs)
Machine: Windows 10 Pro 19045, Intel Core i5-6400, 16 GB RAM, Intel HD 530,
MSVC 19.44.35225, CMake 3.31.6, Ninja, Ninja preset builds

## What was built

`cyber_core` became the dependency-free base layer:

- `types.h`: fixed-width aliases, `usize`, `isize`, `Byte`, `kInvalidIndex`.
- `build_config.h`: build kind, platform, sanitizer flags, assertion
  availability, and a `string_view` of the generated version.
- `error.h`: `ErrorCode`, name helpers, and the two-word `Error` value.
- `result.h`: `Result<T, E>`, `Result<void, E>`, and `Status<E>`.
- `assert.h` + `assert.cpp`: `CG_ASSERT`, `CG_ASSERT_MSG`, `CG_VERIFY`,
  allocation-free failure formatting, debugger break, and abort.
- `log.h` + `log.cpp`: levels, channels, one bounded `LogRecord`, a non-owning
  sink interface, a spinlock-protected `Logger`, atomic thresholds, and
  `CG_LOG`.
- `log_sinks.h` + `log_sinks.cpp`: `ConsoleSink`, `CallbackSink`, and
  `FormatLogLine`.
- `hash.h` + `hash.cpp`: one-shot xxHash64, `HashString`, `HashBytes`, and
  `HashCombine`.

`tools/test` became the project test runner:

- `cyber_test` static library with `main`, registry, CLI, and output formats.
- `CG_TEST`, `CG_EXPECT_TRUE/FALSE/EQ/NE/STREQ`, `CG_REQUIRE`, `CG_FAIL`.
- `--list`, `--filter`, `--repeat`, `--seed`, `--shuffle`,
  `--format console|tap|junit`, `--help`.
- Exit codes: 0 pass, 1 test failure, 2 usage or configuration error.
- `cyber_add_test` links the runner into every test executable.

No third-party dependency was added. `third_party/` remains empty.

## Decisions

- [ADR 0004](../adr/0004-core-primitives-and-test-runner.md): core primitive
  contracts and the project test runner.
- [Phase 2 design note](../design/phase-02.md): alternatives, risks, and the
  test plan.
- No change to the math, depth, color, threading, or ABI conventions.

## Evidence for done-when

### Structure and dependency law

```text
python scripts/check_structure.py
Structure check passed: 16 modules, 1 contract(s).

python scripts/layer_check.py
Layer check passed: 16 modules.

scripts\format.ps1 -Check
(no output; exit 0)
```

### Presets

Every preset builds with `/W4 /WX` on MSVC and runs the full suite. All eight
CTest entries pass in every case.

| Preset | Result | CTest |
|---|---|---|
| `debug` | build ok | 8/8 passed |
| `release` | build ok | 8/8 passed |
| `shipping` | build ok | 8/8 passed |
| `debug-asan` | build ok | 8/8 passed |

CTest entries:

```text
test_runner, core_result, core_assert, core_log,
core_hash, core_build_config, core_version, core_public_headers
```

Representative runner evidence from `build/debug`:

```text
core_result.exe --list
result_ok_and_error_access
result_void
...

core_result.exe --filter=result_void --repeat=2 --seed=7 --shuffle
[ RUN      ] result_void [repeat 1/2]
[       OK ] result_void [repeat 1/2] (0.008 ms)
[ RUN      ] result_void [repeat 2/2]
[       OK ] result_void [repeat 2/2] (0.001 ms)
[==========] 2 test(s) ran.
[  PASSED  ] 2 test(s).

core_result.exe --filter=result_void --format=tap
TAP version 13
1..1
ok 1 - result_void

core_result.exe --filter=result_void --format=junit
<?xml version="1.0" encoding="UTF-8"?>
<testsuites tests="1" failures="0" errors="0" time="0.000008">
  <testsuite name="cyber" tests="1" failures="0" errors="0" skipped="0" time="0.000008">
    <testcase classname="cyber" name="result_void" ... />
  </testsuite>
</testsuites>

test_runner.exe --help          exit 0
test_runner.exe --list          exit 0
test_runner.exe --bogus         exit 2
test_runner.exe --filter=zzz    exit 2
```

Hashing tests include canonical xxHash64 vectors (`""`, `"a"`, `"abc"`,
`"message digest"`, a 32-byte key, the quick-brown-fox sentence, seed 1, and
1000-byte and 1024-byte inputs) plus unaligned input and combine determinism.

### Integration

```text
scripts\verify_package.ps1 -Preset debug
install: build/install-debug
consumer: build/package-consumer-debug/consumer.exe  exit 0
```

The installed tree now contains every public core header:

```text
include/cyber/core/assert.h
include/cyber/core/build_config.h
include/cyber/core/error.h
include/cyber/core/hash.h
include/cyber/core/log.h
include/cyber/core/log_sinks.h
include/cyber/core/result.h
include/cyber/core/types.h
include/cyber/core/version.h
include/cyber/core/version_generated.h
```

The source consumer builds the same `main.cpp` through `add_subdirectory` and
exits 0:

```text
cmake -S tests/integration/source_consumer -B build/source-consumer-local -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/source-consumer-local
build/source-consumer-local/source_consumer.exe   exit 0
```

The consumer checks version metadata, `BuildConfig`, `Result`, `Result<void,E>`,
xxHash64, and logger filtering through the installed headers and the exported
`cyber::core` target.

### CI

Remote run for `ff72526`:

```text
gh run view 39407569403
```

Status is recorded in the CI evidence commit that follows this report.

## Known gaps

- Branch protection for the CI checks is documented but not yet configured in
  GitHub.
- The logger is synchronous. Timestamps and thread identity wait for
  `cyber_platform`; an asynchronous drain waits for `cyber_jobs`.
- `Logger` sink pointers are non-owning, and a sink that re-enters the same
  logger deadlocks. Both rules are documented but not debug-validated.
- xxHash64 is non-cryptographic and must not be used for signatures or
  passwords.
- `Result` is move-only for values that can throw on copy (`std::string`). This
  is intentional in a no-exception build but stricter than `std::expected`.
- The installed package still carries only static libraries. Shared-library and
  ABI packaging stay with the C ABI phase (Phase 25) and release work.
- The test runner covers the console path in CTest; TAP and JUnit are exercised
  by hand and by the runner's own option/XML tests, not by a CI consumer.

## Sources used

- ISO C++20: `std::construct_at`, `std::destroy_at`, `std::span`,
  `std::string_view`, `std::from_chars`, `std::atomic`, `std::chrono`,
  `std::endian`, `std::rotl`.
- xxHash specification and canonical XXH64 vectors, cross-checked against an
  independent xxHash implementation.
- Test Anything Protocol version 13.
- JUnit XML conventions used by common CI systems.
- Repository documents: error model, coding standard, testing strategy,
  integration contract, ADR 0001, ADR 0003, ADR 0004.

## What the next phase needs from this one

- `cyber_math` can include `<cyber/core/types.h>`, `<cyber/core/result.h>`,
  `<cyber/core/assert.h>`, and `<cyber/core/log.h>` without adding a
  dependency edge.
- `cyber_math` keeps `T` and `E` nothrow move constructible when it returns
  `Result`.
- New module tests use `CG_TEST` and `cyber_add_test`; the runner supplies
  `main`.
- New public headers join the `PUBLIC_HEADERS` list and are installed by
  `cmake/CyberPackage.cmake`.
- The test runner stays a developer tool; it is not exported and not installed.
