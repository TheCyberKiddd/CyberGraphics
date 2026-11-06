# Phase 20 reading notes

Date: 2026-11-06

## Sources

- Yuriy O'Donnell, “FrameGraph: Extensible Rendering Architecture in
  Frostbite,” GDC 2017,
  [GDC Vault](https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in),
  accessed 2026-11-06. The page required a member login for the talk and slides.

## What I learned

- The graph is built in a setup stage so pass dependencies, liveness, and resource lifetimes are known before command recording begins.
- Read/write declarations form hazards: reads depend on the latest writer; writes depend on prior readers and writers of the same resource.
- Dead outputs can be removed by tracing dependencies backward from exported resources and explicitly side-effecting passes.
- Transient resources with compatible descriptions can share one physical allocation only when their pass lifetimes do not overlap.
- RHI resource states are sets of read states or one write state; a transition must use the tracked prior state or `Undefined`.
- Graph-generated transitions must be recorded on the command list before each pass executes; pass callbacks receive resolved resources, not ownership of the device.
- The full talk and slides were not accessible; implementation relies on the repository's binding pass, RHI-state, allocator, and module contracts rather than claiming unseen talk details.

## What applies to CyberGraphics

Use the setup/execute split, inferred hazard graph, dead-pass culling, lifetime
analysis, and graph-generated transitions while keeping all resource and
command ownership explicit.

## What does not apply, and why

No Frostbite code or implementation details are copied. The project keeps its
own RHI vocabulary, allocator contract, backend boundary, and deterministic
declaration-order scheduling.

## Open questions

Async queues, pass reordering, and backend-specific placed-resource allocation
remain for later decisions after the render-thread and multi-queue contracts
exist.

## Links to update

No dead link found. GDC Vault currently requires an account for full access.
