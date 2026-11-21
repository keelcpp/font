# keel_font

Zero-dependency C++20 TrueType/OpenType font parser and analytical glyph rasterizer built exclusively on `keel` (0BSD, STL-free public headers).

## Features

- **Formats**: TrueType (`.ttf`, `.ttc` — `glyf`/`loca`, simple & recursive composite glyphs) and OpenType CFF v1 (`.otf` — `CFF ` Type 2 Charstrings, local/global subrs, CID `FDArray`/`FDSelect`, cubic Beziers, `flex`).
- **Unicode `cmap`**: Format 12 (full UTF-32), Format 4 (BMP), Format 6, and Format 0.
- **Kerning**: Legacy TrueType `kern` (Format 0) and modern OpenType `GPOS` PairPos (LookupType 2, Format 1 & Format 2 ClassDef, including LookupType 9 extension wrappers).
- **Analytical Scanline AA**: Exact signed-area trapezoidal coverage integration (non-zero winding rule) with adaptive quadratic/cubic Bezier flattening.
- **Lightweight Vertical Blue-Zone Autohinting**: Aligns baseline, x-height, and cap-height to integer pixel rows with small-size overshoot clamping.
- **LCD Subpixel Rendering**: 3× horizontal subpixel rasterization with 5-tap balanced FIR filter (`[16, 64, 112, 64, 16] / 256`).
- **Synthetic Styles**: Slant shear (`syntheticItalic`) and smooth morphological embolden (`syntheticBold`).
- **Memory Safety**: Fully bounds-checked big-endian reader; safe on truncated or malformed inputs.

## Quick Example

```cpp
#include "keel/font.h"

keel::font::FontFace face;
if (face.load(fontBytes)) {
    const keel::u32 gid = face.glyph_index('A');
    keel::font::GlyphBitmap bm{};
    face.rasterize(gid, 16.0f, {.hinting = keel::font::Hinting::Light, .subpixel = true}, bm);
}
```

## Build & Test

```bash
cmake -S . -B build -DKEEL_FONT_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## License

Zero-Clause BSD ([LICENSE](LICENSE)).
