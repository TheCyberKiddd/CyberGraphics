# Test images

This folder stores small reference images used by golden-image tests. Phase 6
produces screenshots but does not yet compare golden images; Phase 9 formalizes
the harness and stores the references here.

## How Phase 6 images are produced

Do not hand-edit an image. Every image is generated from code so it can be
reproduced exactly:

1. Build a framebuffer through `cyber::software::Framebuffer::Create` with an
   explicit width, height, and `PixelFormat`.
2. Draw with deterministic functions only: fixed animation time, fixed integer
   coordinates, no random numbers, no wall-clock input, no locale-dependent
   formatting.
3. Convert at the boundary. The framebuffer stores linear color;
   `PackPresentPixels` and `WriteImage` apply the sRGB transfer function when the
   image leaves the renderer.
4. Write with `cyber::software::WriteImage(path, framebuffer, desc)` using PPM
   (P6) or uncompressed 24-bit top-down TGA.

Writing the same framebuffer twice must produce byte-identical files. The
`software_screenshot` test proves this in memory, and
`cyber_sample_gradient --capture <path>` writes a pair of files and compares
them.

## Where generated images go

Generated images are build artifacts and are not committed:

```text
build/<preset>/...        sample output and test scratch files
tests/images/            committed golden references only (Phase 9+)
```

The repository `.gitignore` excludes capture output under `build/`. A test that
needs a committed reference must document its generator, its fixed inputs, and
the exact command that reproduces it next to the file.

## Format rules

- PPM: `P6`, ASCII decimal dimensions, maximum value `255`, then RGB bytes in
  row-major order. No comments, no trailing metadata.
- TGA: image type 2 (uncompressed true color), 24 bits per pixel, top-down
  origin (descriptor `0x20`), no image ID, no color map, no footer.
- Both formats store 8 bits per channel. Floating-point framebuffers are
  converted to sRGB 8-bit at export time; linear-unorm export is available for
  debugging only.
- Alpha is not stored in either format. Opaque overlays and test scenes must not
  depend on alpha surviving export.

## Adding a golden image (Phase 9+)

1. Add or extend a scene in code with fixed inputs.
2. Run the generator at the documented resolution (640x360 for scene tests).
3. Inspect the image.
4. Commit the image under `tests/images/<area>/`.
5. Add a comparison test with a per-channel tolerance and a difference-image
   dump on failure.

Never replace a reference to make a failing test pass without explaining the
visual change in the phase report.
