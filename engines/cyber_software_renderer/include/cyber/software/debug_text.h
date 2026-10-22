#pragma once

#include <cyber/core/types.h>
#include <cyber/math/color.h>
#include <cyber/memory/string_view.h>
#include <cyber/software/framebuffer.h>

namespace cyber::software
{

using cyber::core::i32;

// Glyph box in pixels at scale 1. The drawn cell is kGlyphWidth wide and
// kGlyphHeight tall; consecutive characters advance by kGlyphAdvance, which
// leaves one blank column between glyphs.
inline constexpr i32 kGlyphWidth = 5;
inline constexpr i32 kGlyphHeight = 7;
inline constexpr i32 kGlyphAdvance = 6;

struct TextMetrics
{
    i32 width = 0;
    i32 height = 0;
};

// True when the 5x7 font has a glyph for this character. Supported: space,
// digits, '.', ':', '-', and 'A'-'Z'. Everything else renders blank.
[[nodiscard]] bool IsGlyphSupported(char character) noexcept;

// Size of the drawn text for a positive integer scale. A scale below 1 is
// treated as 1. Text scales by whole pixels: width is
// max(0, count * kGlyphAdvance - 1) * scale and height is kGlyphHeight * scale.
// An empty string measures 0 x 0.
[[nodiscard]] TextMetrics MeasureText(memory::StringView text, i32 pixelScale = 1) noexcept;

// Draws text with the top-left of the first glyph cell at (x, y). Glyph pixels
// are opaque; there is no blending or anti-aliasing. Clipped to the framebuffer.
void DrawText(Framebuffer& target, i32 x, i32 y, memory::StringView text, const math::Color& color,
              i32 pixelScale = 1) noexcept;

}  // namespace cyber::software
