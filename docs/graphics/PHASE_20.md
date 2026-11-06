# Phase 20 — Render graph

Phase 20 adds a renderer-owned setup/execute graph that derives resource
hazards, prunes dead passes, aliases compatible non-overlapping transient
resources, records RHI transitions, and writes deterministic text and Graphviz
diagnostics.

## Public surface

Include [`render_graph.h`](../../engines/cyber_renderer/include/cyber/renderer/render_graph.h).
`RenderGraph` borrows an RHI `Device` and explicit allocator. Imported resources
remain caller-owned; transient RHI objects are graph-owned. Setup callbacks
declare reads and writes before compilation. Execute callbacks receive only
declared resource handles and the active recording command list.

Typical frame construction:

```cpp
RenderGraph graph(device, allocator);
auto depth = graph.CreateTransientTexture("depth", depthDesc);
auto output = graph.ImportTexture("backbuffer", backbuffer);

graph.AddPass(depthPrepass);
graph.AddPass(mainPass);
graph.AddPass(postProcess);
graph.Export(output.Value(), cyber::rhi::ResourceState::Present);
graph.Compile();
graph.Execute(commandList);
```

The real API is fallible: check every `Result` and `Status` before continuing.
`AddPass` invokes its setup callback immediately; `Compile` computes the
dependency closure and alias slots; `Execute` records required barriers and
executes only live callbacks. Submitting the command list remains the caller's
responsibility. After submission succeeds, call `AcknowledgeSubmission` before
reusing the graph for another frame. An execution still awaiting acknowledgement
cannot be run again. If its command list is discarded, discard that graph too.
Before destroying a graph, discard unsubmitted lists and ensure submitted work
no longer references its transient resources.
Graph dumps write into caller-owned bounded storage; file I/O is intentionally
outside the runtime module.

## Decisions

- Passes stay in deterministic declaration order; no async queues or implicit
  scheduling.
- Exports and explicit side effects root culling. Reads depend on the latest
  writer; writes depend on prior readers and writers.
- Aliasing requires identical RHI resource descriptions and disjoint live-pass
  intervals. The shared physical resource uses Phase 15's transient allocator
  lifetime.
- The graph is non-copyable and non-movable so graph handles retain a stable
  owner identity. See [ADR 0023](../adr/0023-render-graph-lifetimes-and-aliasing.md).

## Validation

Eight graph tests prove the depth/main/post-processing declaration, automatic
barriers, unused-pass culling, a diamond dependency, safe and rejected alias
cases, buffer imports/transitions, read-before-write rejection, bounded
diagnostics, and production of a DOT file under the build tree. Full
Debug, Release, and Debug-ASan runs pass 100/100 CTest targets each. See the
[Phase 20 report](../reports/phase-20.md) for exact commands.
