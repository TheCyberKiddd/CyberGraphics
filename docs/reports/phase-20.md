# Phase 20 report: render graph

Date: 2026-11-06 · Working tree based on `f876288` (not committed)

Machine: Windows 10 Pro, Intel Core i5-6400 CPU @ 2.70 GHz, Intel HD Graphics
530 driver `31.0.101.2125`, MSVC 19.44.35229.0.

## What was built

- Added the public `RenderGraph`, generation-checked graph resource handles,
  setup-stage `RenderPassBuilder`, and execute-stage `RenderPassContext`.
- Added imported and graph-owned transient buffer/texture resources, captured
  imported RHI state, state/usage validation, and explicit export final states.
- Added read/write hazard inference, dependency closure, export/side-effect
  liveness roots, dead-pass culling, and transient physical-slot sharing for
  identical descriptions with disjoint live intervals.
- Added barrier recording immediately before each live pass, explicit RHI
  resource ownership, bounded all-or-nothing text and Graphviz DOT exports, and
  a null-backend multi-pass proof that emits a DOT file beneath the build tree.
- Updated the renderer contract, package-header verification, build guide,
  changelog, project documentation, and phase indexes.

## Decisions

See [ADR 0023](../adr/0023-render-graph-lifetimes-and-aliasing.md) and the
[design note](../design/phase-20-render-graph.md). ADR 0023 remains proposed
until owner review. The required GDC Vault talk page was accessible, but its
full talk and slides required a member login; the implementation follows the
repository's binding RHI, allocator, and module contracts and does not claim
to derive undocumented details from that talk.

## Evidence for done-when

The graph test records the named depth prepass, main pass, and
post-processing pass through the null RHI, verifies automatic transitions
after submission, proves the unused branch is culled, and writes
`phase20_render_graph.dot` under the CTest build directory. The test binary
reports eight passing graph tests.

| Exact command | Result |
|---|---|
| `. .\scripts\bootstrap.ps1 -Quiet; .\scripts\build.ps1 -Preset debug -Test` | Passed; 100/100 CTest targets. |
| `. .\scripts\bootstrap.ps1 -Quiet; .\scripts\build.ps1 -Preset release -Test` | Passed; 100/100 CTest targets. |
| `. .\scripts\bootstrap.ps1 -Quiet; .\scripts\build.ps1 -Preset debug-asan -Test` | Passed; 100/100 CTest targets. |
| `py -3 scripts\check_structure.py` | Passed; 16 modules and one contract. |
| `py -3 scripts\layer_check.py` | Passed; 16 modules. |
| `.\scripts\format.ps1 -Check` | Passed. |
| `git diff --check` | Passed. |
| Installed package consumer (exact commands below) | Passed; `render_graph.h` was installed and the consumer built and ran. |
| Source consumer (exact commands below) | Passed. |
| `.\build\debug\engines\cyber_renderer\renderer_render_graph.exe` | Passed; 8/8 graph tests; generated DOT contains the three live named passes. |

Installed package consumer (MSVC Release):

```powershell
. .\scripts\bootstrap.ps1 -Quiet
cmake --install build\release --prefix build\phase20-package
cmake -S tests\integration\package_consumer -B build\phase20-package-consumer -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$((Resolve-Path 'build\phase20-package').Path)"
cmake --build build\phase20-package-consumer
.\build\phase20-package-consumer\consumer.exe
```

Result: passed; package installed, consumer configured and built, and
`consumer.exe` returned success.

Source-tree consumer (MSVC Debug):

```powershell
. .\scripts\bootstrap.ps1 -Quiet
cmake -S tests\integration\source_consumer -B build\phase20-source-consumer -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build\phase20-source-consumer
.\build\phase20-source-consumer\source_consumer.exe
```

Result: passed; source consumer configured and built, and
`source_consumer.exe` returned success.

## Known gaps

- The phase proof uses the null backend; it validates the declared three-pass
  schedule and state transitions but does not draw a visible image or exercise
  native D3D12 render-pass contents.
- Scheduling remains declaration-order and single-command-list. Async compute,
  pass reordering, and cross-queue synchronization are deferred until their
  later contracts exist.
- Aliasing deliberately requires exact resource-description equality, which is
  conservative and may miss otherwise compatible allocations.
- Graph recording is not transactional. After an execution error, or when a
  successfully recorded command list is discarded instead of submitted, the
  graph must be discarded. After successful submission, call
  `AcknowledgeSubmission` before executing the graph again. Discard unsubmitted
  command lists and ensure submitted work no longer references graph-owned
  transients before destroying the graph.

## Sources used

- [CyberGraphics Phase 20 build guide](../graphics/CYBERGRAPHICS_BUILD_GUIDE.md#phase-20-render-graph).
- [Phase 20 reading notes](../notes/phase-20.md), including the GDC Vault
  access limitation.
- CyberGraphics RHI resource/state, command-list, and allocator contracts.

## What the next phase needs from this one

Phase 21 can rely on pass setup completing before command recording, graph
handles being scoped to a stable graph, and all barriers being recorded on the
render-thread-owned command list. The graph neither submits work nor changes
thread ownership; Phase 21 must define those responsibilities and the safe
frame lifetime for graph-owned transients.
