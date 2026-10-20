# Testing strategy

The project uses a test pyramid. A phase is done only when its tests pass and
the evidence is recorded in a phase report.

## Layers

| Layer | Location | Purpose |
|---|---|---|
| Unit | `engines/<module>/tests/` | One behavior per test; fast; no GPU required. |
| Integration | `tests/integration/` | Two or more modules together; command queues, assets, render graph. |
| Golden image | `tests/golden/` + `tests/images/` | Pixel comparison against stored references with tolerance. |
| Fuzz | `tests/fuzz/` | Parsers and the shader compiler must not crash on hostile input. |
| Stress | `tests/stress/` | Resize, shutdown, device-lost, long-running loops. |
| Benchmark | `tests/benchmarks/` + module `benchmarks/` | Budgets and regression detection. |

## Unit tests

- Live with the module that owns the code.
- Use the project test runner from `tools/test/`.
- Register tests with `CG_TEST(name)` and assert with `CG_EXPECT_*`,
  `CG_REQUIRE`, and `CG_FAIL`.
- Run a test executable directly for `--list`, `--filter`, `--repeat`, `--seed`,
  `--shuffle`, and `--format console|tap|junit`. CTest runs the default console
  path and treats a non-zero exit code as a failure.
- Every public function has a test. A bug fix gets a regression test.

## Golden images

- Render fixed scenes at a small resolution (640x360) for speed.
- Fix the seed, camera, and scene definition in code.
- Store references per backend and per adapter class when output legitimately
  differs.
- Compare with a tolerance: per-channel delta and percentage of pixels allowed
  to differ. The tolerance is data, not a special case in code.
- On failure, write expected, actual, and difference images as CI artifacts.
- Compare raw pixels, not encoded files; container metadata can differ.

## Sanitizers

- Windows MSVC: AddressSanitizer.
- Linux clang: UndefinedBehaviorSanitizer and ThreadSanitizer.
- Sanitizer runs are separate presets, not the default developer build.
- A sanitizer failure is a bug, not a warning.

## CI gates

Every merge must pass:

1. Structure check and dependency-law check.
2. Windows MSVC build and tests.
3. Linux clang build and tests.
4. Formatting check when `clang-format` is available.
5. Phase-specific golden or benchmark checks once they exist.

Later phases add WARP D3D12 runs, self-hosted GPU runners, fuzzing schedules,
and long-running stress jobs.

## Flaky tests

A flaky test is a bug. Quarantine it with a tracking note, fix it, or delete it.
Do not normalize flakiness by retrying until it passes.

## Evidence

Record the command line, test names, machine, compiler, driver, and result in
the phase report. "It worked on my machine" is not evidence; the report names
the machine.
