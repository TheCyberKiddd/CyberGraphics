# cybergraphics — stable product API

This directory is the **only** public SDK surface. It contains the versioned C
ABI that Cyber Engine uses to load a renderer plugin.

Rules:

- C types only. No `std::`, templates, exceptions, RTTI, or C++ classes.
- Every struct starts with `size` and `version`.
- Objects are opaque handles.
- A plugin exports one versioned entry point returning a function table.
- Recoverable errors are `cg_result` codes.
- Capability negotiation precedes optional features.
- No implementation lives here. The GPU and software renderers implement this
  contract.

The full policy is in
[docs/architecture/abi-policy.md](../../docs/architecture/abi-policy.md).

This directory is intentionally empty until Phase 25. Creating the ABI before
the renderer design is settled would freeze the wrong interface.
