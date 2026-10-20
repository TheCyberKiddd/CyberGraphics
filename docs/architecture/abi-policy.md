# ABI policy

CyberGraphics has two kinds of interface, and they have different promises.

## Internal C++ interfaces

Module headers under `engines/<module>/include/cyber/<short>/` are for other
CyberGraphics modules and for tools built from the same source revision. They
are source-compatible within a release, but they are **not** binary-stable.
Types may change between versions. Do not ship them as a public SDK.

## Stable product ABI

`include/cybergraphics/` is the contract between Cyber Engine and a renderer
plugin. It is a C ABI and follows these rules:

1. **C types only.** No `std::`, no templates, no exceptions, no RTTI, no
   references, no C++ classes with virtual functions.
2. **Explicit sizes.** Every struct starts with `size` and `version` fields so
   new fields can be appended without breaking older hosts.
3. **Opaque handles.** Objects are referenced by opaque pointer handles, never
   by layout.
4. **Function tables, not exports.** A plugin exports one versioned entry
   point that returns a table of function pointers. Adding a function appends
   to the table; it does not reorder existing entries.
5. **Caller-owned memory.** The host provides allocators or buffers; the plugin
   does not return memory the host must free with a different allocator.
6. **No callbacks on the render thread unless documented.** Callbacks have a
   stated thread and lifetime.
7. **Errors are codes.** Recoverable failures return `cg_result`; a message
   callback provides detail.
8. **Capability negotiation.** The host queries capabilities before using
   optional features; unsupported features fail cleanly.

## Versioning

- The product API follows semantic versioning once it is public.
- The plugin entry point reports an ABI version.
- Struct `size` fields allow additive changes.
- Removing or reordering a function table entry is a breaking change and
  requires a new major version.
- Module versions are internal and do not need to match product versions.

## Plugin lifecycle

```text
host loads library
  -> cgGetPluginApi(abi_version, &api)
  -> api->create_device(...)
  -> capability queries
  -> frame loop
  -> api->destroy_device(...)
host unloads library
```

The host must be able to load a plugin built against an older minor version and
reject a plugin with an incompatible major version with a clear message.

## Testing the ABI

- Compile a host against the headers and load both plugins.
- Test a struct with a smaller `size` than the plugin expects.
- Test an incompatible major version.
- Test a missing optional function.
- Run the same conformance suite against GPU and software renderers.

## What not to do

- Do not expose a module's C++ API as the SDK.
- Do not pass ownership of C++ objects across the boundary.
- Do not use `bool` in a C ABI without a fixed-width representation if layout
  matters; prefer `uint32_t`.
- Do not change the meaning of an existing field; add a new one.
