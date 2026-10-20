# host_app

A minimal stub of Cyber Engine used to test the stable renderer interface
without merging the two repositories.

- Phase: 25.
- Loads `cyber_graphics` and `cyber_software_renderer` as plugins.
- Switches renderers with a startup setting.
- Runs the same scene through both and compares images.
- Exercises capability negotiation and an incompatible-version plugin.

This is a test host, not the game engine. Cyber Engine remains in its own
repository.

Not implemented yet.
