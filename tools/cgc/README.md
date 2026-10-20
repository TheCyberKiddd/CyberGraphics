# tools/cgc

CyberGraphics Shader Compiler command-line tool. Introduced in Phase 16.

Planned responsibilities:

- compile the project shader language to HLSL text first, then SPIR-V and MSL;
- emit reflection data used to build RHI binding layouts;
- report errors with file, line, and column;
- support permutations, an on-disk cache, and incremental compilation;
- expose a fuzzable front end.

It links `cyber::shaders` and uses only its public API.
