# Performance log

Performance claims are recorded here with the machine and the build. The log is
append-only; a new measurement is a new entry, not an edit of history.

## Entry template

```markdown
## YYYY-MM-DD — <change or phase>

- Machine: CPU, GPU, RAM, OS, driver
- Build: preset, compiler version, commit
- Scene: name, resolution, object count
- Metric: median, p95, p99, slowest 1%, GPU time per pass
- Result: number and units
- Notes: what changed, what was measured, what was not
```

## Budgets

Budgets are set per phase and recorded in `engines/<module>/docs/MODULE.md`.
Until measured, mark them "none yet". Do not invent a budget to look rigorous.

Planned budget categories:

- CPU frame time: update, culling, submission.
- GPU time per pass.
- Frame-time consistency: slowest 1% of frames.
- Memory: system and video memory, fragmentation.
- Asset load and cook time.
- Shader compile and cache hit rate.

## Rules

- Benchmark on fixed hardware; compare medians and percentiles, not averages.
- Record the driver version; a GPU result without a driver is incomplete.
- A benchmark that is not in CI will regress. Add it to the benchmark suite when
  the phase introduces it.
- Investigate the three largest costs before optimizing anything else.
