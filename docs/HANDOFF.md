# Handoff — Phase 22 locally validated

## Current state

- Last verified commit: `1e251aa` (pre-Phase 22); Phase 22 changes are uncommitted.
- `cyber_scene` now provides copied mesh/light/camera proxies, bounded command
  packets through the Phase 21 render queue, generation-checked handles,
  contiguous bounds/transform records, parallel culling, and screen-space LOD.
- Debug, Release, and Debug-ASan pass 104/104 tests each. Scene ThreadSanitizer
  passes 2/2 targets; package and source consumers and repository checks pass.
- The 100,000-proxy benchmark matches all 1,681 visible handles to the serial
  reference: 1.777 ms median, 2.881 ms p95 on an Intel Core i5-6400 (4 cores).
- Design, reading notes, ADR 0025, summary, and report are present.
- The Phase 22 master-roadmap checkbox remains unchecked until this evidence
  is committed.

## Decisions and known gaps

- Scene updates reuse the Phase 21 immutable copied frame queue; `cyber_scene`
  does not depend on `cyber_renderer` or create another queue.
- Flat parallel culling meets the selected 5 ms p95 CPU budget. No BVH/grid,
  asset loading, or native GPU performance claim is included.
- `diagnosing-low-phase-6-sample-frame-rate.json` is an unrelated pre-existing
  untracked file and remains untouched.

## Exact next step

Run `.\scripts\format.ps1 -Check` and `git diff --check`, review the Phase 22
diff, then commit it. Only after the commit should the master roadmap checkbox
be marked complete; Phase 23 is next.
