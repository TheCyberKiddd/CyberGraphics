# 00_window

Phase 5 manual sample. It opens a real OS window through `cyber_platform` only:
no renderer, no RHI, no graphics API. It pumps events, prints resize, DPI, and
fullscreen changes, and reads input.

Controls:

- `F11` toggles fullscreen.
- `Escape` closes the window.

Build and run (Windows):

```powershell
.\scripts\build.ps1 -Preset debug
.\build\debug\samples\00_window\cyber_sample_window.exe
```

The binary lives under `build/<preset>/samples/00_window/`. On a platform with
no window backend (Linux CI) the sample prints a message and exits zero.

Expected result: a 1280x720 window opens, resize and fullscreen changes print to
the console, and the window closes cleanly on `Escape`.
