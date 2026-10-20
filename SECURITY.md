# Security policy

CyberGraphics is a private, proprietary project. Security reports must not be
filed as public GitHub issues.

## Reporting

Send a private report to **thecyberkiddd@gmail.com** with:

- the affected commit or version;
- the platform, compiler, and driver;
- a minimal reproduction or a description of the malformed input;
- the impact you believe is possible;
- whether you want to be credited.

Do not include credentials, personal data, or third-party confidential material
in a report. If a file is needed, describe how to generate it instead.

## Scope

In scope:

- memory-safety and undefined-behavior bugs in parsers, the shader front end,
  asset loading, and command recording;
- out-of-bounds access in the software rasterizer;
- crafted shader or asset files that crash or execute unintended code;
- plugin ABI confusion that lets a host or plugin escape its contract;
- dependency or build-pipeline compromise.

Out of scope:

- rendering differences that are not safety issues (use the normal bug
  process);
- performance regressions without a correctness or safety impact;
- issues in drivers, operating systems, or third-party tools.

## Supported versions

Only the current `main` branch is supported during development. There are no
public releases until the release phase. Security fixes are applied to `main`
and recorded in `CHANGELOG.md` without disclosing the exploit before a fix is
available.

## Handling

1. Acknowledge the report privately.
2. Reproduce with a sanitizer or the software renderer.
3. Fix on a private branch, add a regression test, and run the full CI suite.
4. Publish the fix and credit the reporter if requested.
