# tools/test

The project's own test runner. Introduced in Phase 2.

`cyber_test` is a static library with a `main`. A test executable links it and
contains only test registration and test bodies. It is never installed and never
linked into a shipping target.

## Writing a test

```cpp
#include <cyber/test/test.h>

CG_TEST(core_version_number)
{
    CG_EXPECT_EQ(cyber::core::VersionNumber().major, cyber::core::kVersionMajor);
}
```

Available helpers:

- `CG_TEST(name)` registers one function with file and line.
- `CG_EXPECT_TRUE`, `CG_EXPECT_FALSE`, `CG_EXPECT_EQ`, `CG_EXPECT_NE`,
  `CG_EXPECT_STREQ` record a failure and continue.
- `CG_REQUIRE(expression)` records a failure and returns from the test.
- `CG_FAIL(message)` records an unconditional failure.

## Command line

| Option | Meaning |
|---|---|
| `--list` | List registered tests. |
| `--filter <text>` | Run tests whose name contains `text`. |
| `--repeat <count>` | Run every selected test `count` times (1..1000). |
| `--seed <value>` | Seed exposed to tests and to `--shuffle`. |
| `--shuffle` | Randomize test order with `--seed`. |
| `--format <format>` | `console`, `tap`, or `junit`. |
| `--help`, `-h` | Show usage. |

Exit codes: `0` for success, `1` for a test failure, `2` for a usage or
configuration error.

## Policy

- The runner depends only on the C++ standard library.
- Test executables compile with exceptions enabled; runtime modules do not.
- The runner is a developer tool and is not part of the installed package.
