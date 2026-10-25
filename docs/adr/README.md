# Architecture decision records

An ADR captures a decision that is expensive to reverse, the context that led
to it, and the consequences. Use one when a future reader would otherwise ask
"why on earth is it done this way?"

## Index

| ADR | Title | Status |
|---|---|---|
| [0000](0000-template.md) | Template | — |
| [0001](0001-module-architecture.md) | Repository and module architecture | accepted |
| [0002](0002-math-and-depth-conventions.md) | Math and depth conventions | accepted |
| [0003](0003-integration-and-packaging.md) | Integration and packaging contract | accepted |
| [0004](0004-core-primitives-and-test-runner.md) | Core primitive contracts and the project test runner | accepted |
| [0005](0005-math-representation-and-simd.md) | Math representation, SIMD, and fallibility | accepted |
| [0006](0006-memory-allocation-and-handles.md) | Memory allocation, containers, and handle lifetime | accepted |
| [0007](0007-platform-and-job-scheduling.md) | Platform boundary and job scheduling | accepted |
| [0008](0008-framebuffer-and-present.md) | Framebuffer ownership and the present boundary | accepted |
| [0009](0009-rasterization-conventions.md) | Rasterization conventions | accepted |
| [0010](0010-depth-clipping-and-perspective.md) | Depth, clipping, and perspective | accepted |
| [0011](0011-textures-shaders-and-golden-images.md) | Texture sampling, CPU shader interface, and golden images | accepted |
| [0012](0012-supersampling-before-multisample-coverage.md) | Supersampling before multisample coverage | accepted |
| [0013](0013-rhi-object-model.md) | RHI object model, handles, and the backend boundary | accepted |

Add new records as `NNNN-title.md`, starting at `0001`. Never delete an ADR;
supersede it with a new one and update the old record's status.

## When to write one

- A public interface or ABI changes.
- A dependency direction changes.
- A coordinate, depth, or color convention changes.
- A backend or platform strategy changes.
- A third-party exception is considered.
- A phase chooses between materially different approaches (for example the
  shader strategy or the GI approach).

## Status values

- `proposed`
- `accepted`
- `superseded by NNNN`
- `rejected`

## Review

An ADR is accepted when the owner has reviewed it and the consequences are
written down. "We will deal with it later" is not a consequence.
