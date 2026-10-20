# tools/ci

Scripts used by local development and CI. These are not shipped.

Current checks:

- `scripts/check_structure.py` — required paths, module contracts, generated
  index consistency.
- `scripts/layer_check.py` — manifest validation, dependency direction, cycles,
  CMake declarations, and cross-module includes.
- `scripts/scaffold_modules.py` — create module folders and contract stubs from
  `engines/modules.json`.

Future checks:

- header self-containment;
- formatting;
- public API compatibility;
- golden-image artifact collection;
- benchmark regression thresholds.
