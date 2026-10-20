# tools/cooker

Offline asset cooking tool. Introduced in Phase 24.

Planned responsibilities:

- import source assets (glTF, OBJ, PNG, JPEG, DDS, KTX);
- validate hostile input and report precise errors;
- write versioned, checksummed, memory-mappable cooked files;
- track dependencies and reuse a cook cache;
- produce deterministic output for CI.

It links `cyber::assets` and uses only its public API.
