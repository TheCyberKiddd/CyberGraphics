# Tests

Cross-module tests only. Unit tests live with the module that owns the code.

| Folder | Contents |
|---|---|
| `integration/` | Two or more modules together: command queues, graph, assets, plugins. |
| `golden/` | Golden-image harness and comparison logic. |
| `images/` | Stored reference images, per backend and adapter class. |
| `fuzz/` | Fuzz targets and corpora for parsers and the shader compiler. |
| `stress/` | Resize, shutdown, device-lost, and long-running loops. |
| `benchmarks/` | Cross-module benchmark suite and stored baselines. |
| `fixtures/` | Small input files used by tests. |

Rules:

- Tests are deterministic. Fix the seed and the scene.
- Golden images use raw pixel comparison with a tolerance.
- A failed image test writes expected, actual, and difference images.
- Flaky tests are quarantined and fixed, not retried until green.
