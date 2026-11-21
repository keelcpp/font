#include "keel/font.h"
#include "keel/keel.h"
#include "keel/math.h"

static keel::i32 g_failures = 0;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        if (!(cond)) {                                                               \
            keel::eprintln("[FAIL] {} ({}:{})", #cond, __FILE__, __LINE__);          \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        if (keel::abs((a) - (b)) > (eps)) {                                          \
            keel::eprintln("[FAIL] {} ({}) != {} ({}) [{}:{}]",                      \
                           #a, (a), #b, (b), __FILE__, __LINE__);                    \
            ++g_failures;                                                            \
        }                                                                            \
    } while (0)

namespace {

void push_u8(keel::Array<keel::u8>& buf, keel::u8 v) { buf.push_back(v); }
void push_u16(keel::Array<keel::u8>& buf, keel::u16 v) {
    buf.push_back(keel::as<keel::u8>((v >> 8u) & 0xFFu));
    buf.push_back(keel::as<keel::u8>(v & 0xFFu));
}
void push_i16(keel::Array<keel::u8>& buf, keel::i16 v) {
    push_u16(buf, keel::as<keel::u16>(v));
}
void push_u32(keel::Array<keel::u8>& buf, keel::u32 v) {
    buf.push_back(keel::as<keel::u8>((v >> 24u) & 0xFFu));
    buf.push_back(keel::as<keel::u8>((v >> 16u) & 0xFFu));
    buf.push_back(keel::as<keel::u8>((v >> 8u) & 0xFFu));
    buf.push_back(keel::as<keel::u8>(v & 0xFFu));
}

// Builds a minimal valid TrueType font in memory with:
//   gid 0: .notdef (empty)
//   gid 1: ' ' (U+0020, whitespace, adv=400)
//   gid 2: 'A' (U+0041, 600x700 filled square [100..700] x [0..700], adv=800)
//   gid 3: 'H' (U+0048, 600x700 filled square [100..700] x [0..700], adv=800)
//   gid 4: 'x' (U+0078, 500x500 filled square [100..600] x [0..500], adv=700)
//   gid 5: 'B' (U+0042, composite glyph referencing gid 2 shifted by (+50, +100), adv=850)
//   kern:  ('A', 'H') = -100 units
keel::Array<keel::u8> build_synthetic_ttf() {
    auto make_rect_glyph = [](keel::i16 x0, keel::i16 y0, keel::i16 x1, keel::i16 y1) {
        keel::Array<keel::u8> g;
        push_i16(g, 1);  // numContours
        push_i16(g, x0);
        push_i16(g, y0);
        push_i16(g, x1);
        push_i16(g, y1);
        push_u16(g, 3u); // endPtsOfContours[0] = 3 (4 points)
        push_u16(g, 0u); // instructionLength
        // 4 on-curve points, 16-bit signed deltas (flags = 0x01)
        for (keel::u32 i = 0u; i < 4u; ++i) push_u8(g, 0x01u);
        // Clockwise rectangle: (x0,y0) -> (x0,y1) -> (x1,y1) -> (x1,y0)
        push_i16(g, x0);
        push_i16(g, 0);
        push_i16(g, keel::as<keel::i16>(x1 - x0));
        push_i16(g, 0);
        push_i16(g, y0);
        push_i16(g, keel::as<keel::i16>(y1 - y0));
        push_i16(g, 0);
        push_i16(g, keel::as<keel::i16>(y0 - y1));
        while ((g.size() & 3u) != 0u) push_u8(g, 0u);
        return g;
    };

    auto make_composite_glyph = [](keel::u16 refGid, keel::i16 dx, keel::i16 dy) {
        keel::Array<keel::u8> g;
        push_i16(g, -1); // composite
        push_i16(g, 100); push_i16(g, 0); push_i16(g, 750); push_i16(g, 800);
        push_u16(g, 0x0003u); // ARG_1_AND_2_ARE_WORDS | ARGS_ARE_XY_VALUES
        push_u16(g, refGid);
        push_i16(g, dx);
        push_i16(g, dy);
        while ((g.size() & 3u) != 0u) push_u8(g, 0u);
        return g;
    };

    // 1. glyf + loca (indexToLocFormat = 1, 32-bit offsets)
    keel::Array<keel::u8> glyf;
    keel::Array<keel::u32> offsets;
    offsets.push_back(0u); // gid 0 (.notdef empty)
    offsets.push_back(0u); // gid 1 (' ' empty)
    offsets.push_back(0u); // gid 2 ('A')
    {
        auto g = make_rect_glyph(100, 0, 700, 700);
        for (keel::usize i = 0u; i < g.size(); ++i) glyf.push_back(g[i]);
    }
    offsets.push_back(keel::as<keel::u32>(glyf.size())); // gid 3 ('H')
    {
        auto g = make_rect_glyph(100, 0, 700, 700);
        for (keel::usize i = 0u; i < g.size(); ++i) glyf.push_back(g[i]);
    }
    offsets.push_back(keel::as<keel::u32>(glyf.size())); // gid 4 ('x')
    {
        auto g = make_rect_glyph(100, 0, 600, 500);
        for (keel::usize i = 0u; i < g.size(); ++i) glyf.push_back(g[i]);
    }
    offsets.push_back(keel::as<keel::u32>(glyf.size())); // gid 5 ('B' composite)
    {
        auto g = make_composite_glyph(2u, 50, 100);
        for (keel::usize i = 0u; i < g.size(); ++i) glyf.push_back(g[i]);
    }
    offsets.push_back(keel::as<keel::u32>(glyf.size())); // end sentinel

    keel::Array<keel::u8> loca;
    for (keel::usize i = 0u; i < offsets.size(); ++i) push_u32(loca, offsets[i]);

    // 2. head (54 bytes)
    keel::Array<keel::u8> head(54u, 0u);
    head[18] = 0x03u; head[19] = 0xE8u; // unitsPerEm = 1000
    head[50] = 0x00u; head[51] = 0x01u; // indexToLocFormat = 1 (long)

    // 3. maxp (6 bytes)
    keel::Array<keel::u8> maxp;
    push_u32(maxp, 0x00010000u);
    push_u16(maxp, 6u); // numGlyphs = 6

    // 4. hhea (36 bytes)
    keel::Array<keel::u8> hhea(36u, 0u);
    // ascender = 800 (0x0320), descender = -200 (0xFF38), numHMetrics = 6
    hhea[4] = 0x03u; hhea[5] = 0x20u;
    hhea[6] = 0xFFu; hhea[7] = 0x38u;
    hhea[34] = 0x00u; hhea[35] = 0x06u;

    // 5. hmtx (6 * 4 bytes)
    keel::Array<keel::u8> hmtx;
    const keel::u16 advs[6] = {500u, 400u, 800u, 800u, 700u, 850u};
    for (keel::u32 i = 0u; i < 6u; ++i) {
        push_u16(hmtx, advs[i]);
        push_i16(hmtx, 0);
    }

    // 6. cmap (Format 4 with 6 segments: ' ', 'A', 'B', 'H', 'x', sentinel 0xFFFF)
    keel::Array<keel::u8> cmap;
    push_u16(cmap, 0u);  // version
    push_u16(cmap, 1u);  // numTables
    push_u16(cmap, 3u);  // platformID = Windows
    push_u16(cmap, 1u);  // encodingID = Unicode BMP
    push_u32(cmap, 12u); // subtable offset

    const keel::u16 codes[6] = {0x0020u, 0x0041u, 0x0042u, 0x0048u, 0x0078u, 0xFFFFu};
    const keel::i16 deltas[6] = {
        keel::as<keel::i16>(1 - 0x0020),
        keel::as<keel::i16>(2 - 0x0041),
        keel::as<keel::i16>(5 - 0x0042),
        keel::as<keel::i16>(3 - 0x0048),
        keel::as<keel::i16>(4 - 0x0078),
        1
    };
    const keel::u16 segCount = 6u;
    push_u16(cmap, 4u); // format 4
    push_u16(cmap, keel::as<keel::u16>(16u + segCount * 8u));
    push_u16(cmap, 0u);
    push_u16(cmap, keel::as<keel::u16>(segCount * 2u));
    push_u16(cmap, 8u);
    push_u16(cmap, 2u);
    push_u16(cmap, 4u);
    for (keel::u32 i = 0u; i < segCount; ++i) push_u16(cmap, codes[i]); // endCode
    push_u16(cmap, 0u); // reservedPad
    for (keel::u32 i = 0u; i < segCount; ++i) push_u16(cmap, codes[i]); // startCode
    for (keel::u32 i = 0u; i < segCount; ++i) push_i16(cmap, deltas[i]); // idDelta
    for (keel::u32 i = 0u; i < segCount; ++i) push_u16(cmap, 0u); // idRangeOffset

    // 7. kern (Format 0 with 1 pair: gid 2 ('A') + gid 3 ('H') -> -100)
    keel::Array<keel::u8> kern;
    push_u16(kern, 0u);  // version
    push_u16(kern, 1u);  // nTables
    push_u16(kern, 0u);  // subtable version
    push_u16(kern, 20u); // length (6 hdr + 8 fmt0 hdr + 6 pair)
    push_u16(kern, 1u);  // coverage: format 0, horizontal
    push_u16(kern, 1u);  // nPairs
    push_u16(kern, 6u);  // searchRange
    push_u16(kern, 0u);  // entrySelector
    push_u16(kern, 0u);  // rangeShift
    push_u16(kern, 2u);  // left = 'A'
    push_u16(kern, 3u);  // right = 'H'
    push_i16(kern, -100);

    struct Tbl {
        keel::u32 tag;
        const keel::Array<keel::u8>* data;
    };
    auto tag4 = [](char a, char b, char c, char d) {
        return (keel::as<keel::u32>(keel::as<keel::u8>(a)) << 24u) |
               (keel::as<keel::u32>(keel::as<keel::u8>(b)) << 16u) |
               (keel::as<keel::u32>(keel::as<keel::u8>(c)) << 8u) |
               keel::as<keel::u32>(keel::as<keel::u8>(d));
    };
    const Tbl tables[] = {
        {tag4('c', 'm', 'a', 'p'), &cmap},
        {tag4('g', 'l', 'y', 'f'), &glyf},
        {tag4('h', 'e', 'a', 'd'), &head},
        {tag4('h', 'h', 'e', 'a'), &hhea},
        {tag4('h', 'm', 't', 'x'), &hmtx},
        {tag4('k', 'e', 'r', 'n'), &kern},
        {tag4('l', 'o', 'c', 'a'), &loca},
        {tag4('m', 'a', 'x', 'p'), &maxp},
    };
    constexpr keel::u16 numTables = 8u;

    keel::Array<keel::u8> ttf;
    push_u32(ttf, 0x00010000u);
    push_u16(ttf, numTables);
    push_u16(ttf, 128u);
    push_u16(ttf, 3u);
    push_u16(ttf, 0u);

    keel::u32 dataOffset = 12u + keel::as<keel::u32>(numTables) * 16u;
    for (keel::u16 i = 0u; i < numTables; ++i) {
        push_u32(ttf, tables[i].tag);
        push_u32(ttf, 0u); // checksum
        push_u32(ttf, dataOffset);
        const keel::u32 len = keel::as<keel::u32>(tables[i].data->size());
        push_u32(ttf, len);
        dataOffset += (len + 3u) & ~3u;
    }
    for (keel::u16 i = 0u; i < numTables; ++i) {
        const auto& d = *tables[i].data;
        for (keel::usize j = 0u; j < d.size(); ++j) ttf.push_back(d[j]);
        while ((ttf.size() & 3u) != 0u) push_u8(ttf, 0u);
    }
    return ttf;
}

} // namespace

int main() {
    keel::println("[RUN] Testing keel::font...");

    using namespace keel::font;

    // 1. Bounds safety on empty / invalid / truncated buffers
    {
        FontFace bad;
        CHECK(!bad.is_valid());
        CHECK(!bad.load(nullptr, 0u));
        const keel::u8 garbage[16] = {0xDE, 0xAD, 0xBE, 0xEF, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        CHECK(!bad.load(garbage, sizeof(garbage)));
        CHECK(!bad.is_valid());
        GlyphBitmap bm{};
        CHECK(!bad.rasterize(0u, 16.0f, {}, bm));
    }

    // 2. Load synthetic TTF and validate core metrics, cmap, and kerning
    const keel::Array<keel::u8> ttf = build_synthetic_ttf();
    FontFace face;
    CHECK(face.load(ttf));
    CHECK(face.is_valid());
    CHECK(!face.is_cff());
    CHECK(face.units_per_em() == 1000u);
    CHECK(face.num_glyphs() == 6u);
    CHECK(face.ascender() == 800);
    CHECK(face.descender() == -200);
    CHECK(face.cap_height() == 700);
    CHECK(face.x_height() == 500);
    CHECK(face.has_kerning());

    const keel::u32 gSpace = face.glyph_index(' ');
    const keel::u32 gA = face.glyph_index('A');
    const keel::u32 gB = face.glyph_index('B');
    const keel::u32 gH = face.glyph_index('H');
    const keel::u32 gx = face.glyph_index('x');
    CHECK(gSpace == 1u);
    CHECK(gA == 2u);
    CHECK(gH == 3u);
    CHECK(gx == 4u);
    CHECK(gB == 5u);
    CHECK(face.glyph_index('Z') == 0u);
    CHECK(face.advance_width_units(gA) == 800u);
    CHECK(face.kerning_units(gA, gH) == -100);
    CHECK(face.kerning_units(gH, gA) == 0);

    // 3. Whitespace glyph yields empty bitmap with exact scaled advance
    {
        GlyphBitmap sp{};
        CHECK(face.rasterize(gSpace, 20.0f, {.hinting = Hinting::None}, sp));
        CHECK(sp.empty);
        CHECK_NEAR(sp.advancePx, 8.0f, 0.01f);
    }

    // 4. Analytical coverage check on exact integer-aligned 20px square ([2..14] x [0..14] = 12x14 px)
    {
        GlyphBitmap bm{};
        CHECK(face.rasterize(gA, 20.0f, {.hinting = Hinting::None, .subpixel = false}, bm));
        CHECK(!bm.empty);
        CHECK(!bm.isSubpixel);
        CHECK(bm.width == 12u);
        CHECK(bm.height == 14u);
        CHECK(bm.strideBytes == 12u);
        CHECK(bm.bearingX == 2);
        CHECK(bm.bearingY == 14);
        CHECK_NEAR(bm.advancePx, 16.0f, 0.01f);
        keel::usize fullPx = 0u;
        for (keel::usize i = 0u; i < bm.pixels.size(); ++i) {
            if (bm.pixels[i] == 255u) ++fullPx;
        }
        CHECK(fullPx == 12u * 14u);
    }

    // 5. LCD 3x subpixel + 5-tap FIR filter
    {
        GlyphBitmap lcd{};
        CHECK(face.rasterize(gA, 20.0f, {.hinting = Hinting::Light, .subpixel = true, .subpixelPhase = 1u}, lcd));
        CHECK(!lcd.empty);
        CHECK(lcd.isSubpixel);
        CHECK(lcd.strideBytes == lcd.width * 3u);
        CHECK(lcd.width >= 12u && lcd.height == 14u);
        // Edge pixels should show distinct R/G/B weights due to subpixel phase + 5-tap filter
        keel::usize chromatic = 0u;
        for (keel::u32 y = 0u; y < lcd.height; ++y) {
            const keel::u8* row = lcd.pixels.data() + keel::as<keel::usize>(y) * keel::as<keel::usize>(lcd.strideBytes);
            for (keel::u32 x = 0u; x < lcd.width; ++x) {
                const keel::u8 r = row[x * 3u], g = row[x * 3u + 1u], b = row[x * 3u + 2u];
                if (r != g || g != b) ++chromatic;
            }
        }
        CHECK(chromatic > 10u);
    }

    // 6. Composite glyph ('B' = 'A' shifted by (+50, +100) units -> (+1, +2) px at 20px)
    {
        GlyphBitmap comp{};
        CHECK(face.rasterize(gB, 20.0f, {.hinting = Hinting::None}, comp));
        CHECK(!comp.empty);
        CHECK(comp.bearingX == 3);
        CHECK(comp.bearingY == 16);
        CHECK(comp.width == 12u && comp.height == 14u);
    }

    // 7. Synthetic bold & italic
    {
        GlyphBitmap bold{}, italic{};
        CHECK(face.rasterize(gA, 20.0f, {.hinting = Hinting::Light, .syntheticBold = true}, bold));
        CHECK(!bold.empty && bold.width > 12u);
        CHECK(bold.advancePx > 16.0f);

        CHECK(face.rasterize(gA, 20.0f, {.hinting = Hinting::Light, .syntheticItalic = true}, italic));
        CHECK(!italic.empty && italic.width > 12u);
    }

    // 8. Truncation fuzz: ensure no truncated prefix of valid TTF ever crashes
    for (keel::usize cut = 0u; cut < ttf.size(); cut += 17u) {
        FontFace f;
        if (f.load(ttf.data(), cut)) {
            GlyphBitmap bm{};
            (void)f.rasterize(f.glyph_index('A'), 16.0f, {}, bm);
        }
    }

    if (g_failures == 0) {
        keel::println("[PASS] All keel::font tests passed!");
        return 0;
    }
    keel::eprintln("[FAIL] Total failures: {}", g_failures);
    return 1;
}
