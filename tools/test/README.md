# tools/test

The project's own test runner. Introduced in Phase 2.

Planned responsibilities:

- test registration and discovery;
- assertion helpers with file and line;
- name filtering, listing, seed, and repeat count;
- pass/fail counts and a non-zero exit code on failure;
- a CI-friendly output format (JUnit XML or TAP);
- no dependency outside `cyber_core` and the standard library.

The runner is a developer tool. It is never linked into a shipping target.
