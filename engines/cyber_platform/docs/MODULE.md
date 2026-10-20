# cyber_platform — module contract

**Kind:** platform
**Layer:** 1
**Order:** 10
**Status:** planned
**Owner:** @TheCyberKiddd

## Purpose

Hardware and OS control: window, display, input, time, files, threads, and DLL loading.

## Public API

`include/cyber/platform/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

## Dependencies

Allowed: cyber_core

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- No renderer or RHI includes.
- Win32 code is isolated under src/win32/.
- Platform state is explicit; no hidden mutable globals.

## Non-goals

- Job scheduling (cyber_jobs).
- GPU device creation (cyber_rhi).
- Audio and gamepad policy.

## Tests

Window and event smoke test, timer accuracy, file I/O, thread wrappers, and DLL loading.

## Budgets

Input latency and timer resolution recorded on the target machine.

## Roadmap

Phases 5.
