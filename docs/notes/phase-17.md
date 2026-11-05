# Phase 17 reading notes — reflection, permutations, and hot reload

Date: 2026-11-05

- Microsoft, "Packing rules for constant variables" (HLSL): constants pack into
  16-byte registers on 4-byte boundaries, a value never straddles a 16-byte
  boundary, and an array element or a structure starts a new register. A
  `float3` followed by a `float` shares one register; two `float`s followed by
  a `float3` take two. Our reflection reproduces exactly these rules.
- Microsoft, "packoffset": `: packoffset(cN.comp)` pins a cbuffer member to a
  register and component. The official variable syntax documents both the
  semantic and packoffset declaration positions. Emitting packoffset for every
  member makes generated HLSL agree with reflection by construction.
- Microsoft, "numthreads": `[numthreads(X, Y, Z)]`; for `cs_5_x` Z is at most
  64 and X*Y*Z at most 1024 (the D3D11/12 limits also cap X and Y at 1024).
  The compiler validates these before emitting.
- Microsoft, `ReadDirectoryChangesW`: change notifications for last-write time
  are only raised when the write reaches the file system, the kernel buffer can
  overflow and drop every queued change, and network shares cap the buffer at
  64 KiB. A timestamp poll observes the same last-write time without the
  overflow failure mode, so Phase 17 keeps the existing polling `FileWatcher`
  and polls at a fixed 50 ms cadence.
- Editors save in several ways (truncate and write, write a temporary file and
  rename). A watcher can therefore observe a half-written file; the reload path
  must treat a failed parse as "keep the last good shader" and retry on the next
  change, not as fatal.
- Khronos, Vulkan `VkPhysicalDeviceLimits::maxPushConstantsSize`: every
  implementation guarantees at least 128 bytes. D3D12 root constants count
  against the 64-DWORD root signature. Reflected push-constant blocks are
  therefore capped at 128 bytes so the same shader works on later backends.
- Microsoft, "Programming guide for HLSL" (author-time versus runtime
  compilation): offline compilation trades startup time for flexibility. The
  on-disk cache stores our compiler's output keyed by everything that can change
  it, so a warm start skips the Cyber front end. The RHI pipeline library
  caches pipeline state objects, but an explicit backend-bytecode export/cache
  remains a gap.
- xxHash64 (Collet, xxHash specification): fast, stable, non-cryptographic.
  Already implemented in `cyber_core`; used for source, keyword, and cache-key
  hashing. Cache files also store the full key so a 64-bit collision is
  detected as a miss, never served as a hit.

## Sources

- Microsoft Learn, [High-level shader language (HLSL)](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl).
- Microsoft Learn, [HLSL packing rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules).
- Microsoft Learn, [HLSL keywords](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-appendix-keywords).
- Microsoft Learn, [HLSL variable syntax and `packoffset`](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-variable-syntax).
- Microsoft Learn, [`numthreads`](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-attributes-numthreads).
- Microsoft Learn, [`ReadDirectoryChangesW`](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-readdirectorychangesw).
- Khronos, [Vulkan specification limits](https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#limits-minmax).
- xxHash, [xxHash specification](https://github.com/Cyan4973/xxHash/blob/dev/doc/xxhash_spec.md).
