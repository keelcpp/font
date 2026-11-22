# keel_font

Zero-dependency C++20 TrueType/OpenType font parser, variable font engine, vector outline extractor, and analytical glyph rasterizer built exclusively on `keel` (0BSD, STL-free public headers, `-fno-exceptions -fno-rtti` ready).

## Features

- **Formats**: TrueType (`.ttf`), multi-face TrueType Collections (`.ttc` via `face_count()` / `load(data, faceIndex)`), and OpenType CFF v1 (`.otf` — Type 2 Charstrings, local/global subrs, CID `FDArray`/`FDSelect`, cubic Beziers, `flex`).
- **Variable Fonts (`fvar` / `avar` / `gvar` / `HVAR`)**: Continuous axis interpolation (`wght`, `wdth`, `slnt`, `opsz`, ...), named instances, piecewise `avar` normalization, sparse-point `gvar` with contour IUP (*Interpolated Untouched Points*), composite component deltas, and `HVAR` advance-width variations.
- **Unicode `cmap` + O(1) ASCII LUT**: Format 12 (full UTF-32), Format 4 (BMP), Format 6, Format 0, plus cached O(1) ASCII lookup.
- **Kerning**: Legacy TrueType `kern` (Format 0) and modern OpenType `GPOS` PairPos (LookupType 2, Format 1 & Format 2 ClassDef, including LookupType 9 extension wrappers).
- **Font Metadata**: `name` table (`family_name`, `subfamily_name`), `OS/2` (`weight_class`, `is_italic`, `strikeout_position/size`), `post` (`is_monospace`, `underline_position/thickness`).
- **Autohinting**: Vertical blue-zone alignment (`descender`, `baseline`, `x-height`, `cap-height`) + small-size overshoot clamping + automatic horizontal stem snapping for crisp bars on 1× (96 DPI) displays.
- **Analytical Scanline AA & LCD Subpixel**: Exact signed-area trapezoidal coverage (non-zero winding rule), adaptive Bezier flattening, 3× horizontal LCD subpixel rendering with 5-tap balanced FIR filter (`[16, 64, 112, 64, 16] / 256`), and smooth synthetic bold/italic.
- **Vector Outline Extraction & Zero-Alloc Hot Path**: `extract_outline()` callback sink (`move_to`/`line_to`/`quad_to`/`cubic_to`/`close`), unrasterized `glyph_metrics()`/`advance_width_px()`, and reusable `RasterScratch`.

## Quick Example

```cpp
#include "keel/font.h"

keel::font::FontFace face;
if (face.load(fontBytes)) {
    if (face.is_variable()) {
        face.set_variation(keel::font::make_tag('w', 'g', 'h', 't'), 700.0f);
    }
    keel::font::RasterScratch scratch;
    keel::font::GlyphBitmap bm{};
    face.rasterize(face.glyph_index('A'), 16.0f, {.hinting = keel::font::Hinting::Light, .subpixel = true}, bm, &scratch);
}
```

## Build & Test

```bash
cmake -S . -B build -DKEEL_FONT_BUILD_TESTS=ON -DKEEL_FONT_NO_EXCEPTIONS_RTTI=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## License

Zero-Clause BSD ([LICENSE](LICENSE)).
