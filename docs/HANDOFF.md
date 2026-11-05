# Handoff — Phase 16

## Current state

- Phase 15 is complete. The Intel HD Graphics 530 sample-path RTV heap-kind bug
  was fixed, the D3D12 sample submits and presents, and the capture evidence is
  consistent with the implementation.
- Phase 16 is complete. ADR 0002 is preserved; the guide now points to ADR 0019.
  The design, ADR, allocator-backed compiler, diagnostics, compiler tests, and
  generated-HLSL D3D12 cube integration are implemented and validated.
- Last verified commit: `b9cb94318897287c3b714605b7a7acc57872f14a`. Current work
  remains uncommitted.
- Debug, Profile, Release, Shipping, and Debug-ASan test suites each pass
  87/87. Structure, layer, formatting, diff, and Profile package-consumer checks
  pass.
- Native `BeginEvent`, `EndEvent`, and `SetMarker` calls remain isolated in
  `engines/cyber_rhi/src/d3d12/gpu_markers.cpp`. Debug/Profile enable these;
  Release/Shipping compile them out. No PIX runtime or other renderer
  dependency was added.
- The Intel HD Graphics 530 failure was not a general D3D12 command-list issue.
  A native D3D12 probe succeeded, the swapchain RTV was corrected, and the
  sample now clears to blue and presents successfully.
- Phase 16 validation: Debug, Release, and Debug-ASan each pass 88/88; structure,
  layer, package consumer, `git diff --check`, and targeted formatting pass.
  The full formatter passes after formatting the two affected Phase 15 files
  with the owner's direction.

## Capture status

- RenderDoc capture `%TEMP%\CyberGraphicsPhase15\d3d12-markers-fixed_frame20135.rdc`
  was reviewed. Its event tree shows `Clear Pass` with `Clear backbuffer` beneath
  it, and the preview shows the blue frame.
- PIX capture `%TEMP%\CyberGraphicsPhase15\d3d12-markers-fixed.wpix` opens and
  shows the nested `Clear Pass` region in the Graphics Queue tree. The capture
  reaches presentation and its screenshot is blue. The UI does not expose the raw
  native `SetMarker` call as a separate visible event, which is consistent with
  Microsoft's guidance that the underlying support API is not a user-facing PIX
  event; the nested debug-region labels remain the capture-visible evidence.
- PIX event-list export still requires Windows Developer Mode. The repository did
  not enable it for this phase.
- Leave `diagnosing-low-phase-6-sample-frame-rate.json` untouched; it is
  unrelated and untracked.
- Phase 15 is accepted; the stale roadmap, status, changelog, ADR 0018, and
  Phase 15 design-note claims have been reconciled with the capture evidence.

## Exact next step

Continue with Phase 17, reflection, permutations, and hot reload. Keep the
current uncommitted work intact; `diagnosing-low-phase-6-sample-frame-rate.json`
remains unrelated and must stay untouched.
