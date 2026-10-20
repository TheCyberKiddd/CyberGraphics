# third_party

This directory is intentionally empty.

CyberGraphics runtime code depends only on the C++ standard library and
operating system or driver headers. Build, CI, and documentation tools are not
shipped, but any exception to the runtime rule must be:

1. approved in an ADR;
2. quarantined in this directory;
3. recorded in `THIRD_PARTY_NOTICES.md` with version, license, and purpose;
4. isolated behind a module interface so it can be replaced.

`scripts/check_structure.py` fails if this directory contains anything other
than this README.
