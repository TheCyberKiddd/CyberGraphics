# tools/docgen

API reference generator. Decided in Phase 42.

The project needs a reference generated from public header comments. The tool
choice is deliberately deferred: writing a small generator for our own comment
format is possible, but using a standard documentation tool is acceptable as a
build-time tool under the third-party policy. The decision belongs in an ADR
before the tool is added.

Requirements:

- reads public headers only;
- resolves module and symbol names consistently;
- fails CI on malformed or missing documentation for stable APIs;
- never requires third-party code in the runtime.
