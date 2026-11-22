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
void push_i32(keel::Array<keel::u8>& buf, keel::i32 v) {
    push_u32(buf, keel::as<keel::u32>(v));
}

struct SynthFontSpec {
    keel::StringRef family{"KeelSans"};
    keel::StringRef subfamily{"Regular"};
    keel::u16 weightClass{400u};
    bool italic{false};
    bool monospace{false};
    bool variable{false};
};

keel::Array<keel::u8> build_synthetic_ttf(const SynthFontSpec& spec) {
    auto make_rect_glyph = [](keel::i16 x0, keel::i16 y0, keel::i16 x1, keel::i16 y1) {
        keel::Array<keel::u8> g;
        push_i16(g, 1);  // numContours
        push_i16(g, x0); push_i16(g, y0); push_i16(g, x1); push_i16(g, y1);
        push_u16(g, 3u); // endPtsOfContours[0] = 3 (4 points)
        push_u16(g, 0u); // instructionLength
        for (keel::u32 i = 0u; i < 4u; ++i) push_u8(g, 0x01u); // all on-curve, 16-bit deltas
        // Points: 0=(x0,y0), 1=(x0,y1), 2=(x1,y1), 3=(x1,y0)
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
        push_i16(g, -1);
        push_i16(g, 100); push_i16(g, 0); push_i16(g, 750); push_i16(g, 800);
        push_u16(g, 0x0003u); // ARG_1_AND_2_ARE_WORDS | ARGS_ARE_XY_VALUES
        push_u16(g, refGid);
        push_i16(g, dx);
        push_i16(g, dy);
        while ((g.size() & 3u) != 0u) push_u8(g, 0u);
        return g;
    };

    // 1. glyf + loca
    // gid 0: .notdef (empty)
    // gid 1: ' ' (whitespace, adv=400)
    // gid 2: 'A' (600x700 [100..700] x [0..700], adv=800)
    // gid 3: 'H' (600x700 [100..700] x [0..700], adv=800)
    // gid 4: 'x' (500x500 [100..600] x [0..500], adv=700)
    // gid 5: 'B' (composite referencing gid 2 shifted by (+50, +100), adv=850)
    // gid 6: '-' (thin horizontal bar [100..700] x [318..382], fractional at 14px -> tests stem snapping!)
    keel::Array<keel::u8> glyf;
    keel::Array<keel::u32> offsets;
    offsets.push_back(0u); // gid 0
    offsets.push_back(0u); // gid 1 (' ')
    offsets.push_back(0u); // gid 2 ('A')
    for (keel::u8 b : make_rect_glyph(100, 0, 700, 700)) glyf.push_back(b);
    offsets.push_back(keel::as<keel::u32>(glyf.size())); // gid 3 ('H')
    for (keel::u8 b : make_rect_glyph(100, 0, 700, 700)) glyf.push_back(b);
    offsets.push_back(keel::as<keel::u32>(glyf.size())); // gid 4 ('x')
    for (keel::u8 b : make_rect_glyph(100, 0, 600, 500)) glyf.push_back(b);
    offsets.push_back(keel::as<keel::u32>(glyf.size())); // gid 5 ('B')
    for (keel::u8 b : make_composite_glyph(2u, 50, 100)) glyf.push_back(b);
    offsets.push_back(keel::as<keel::u32>(glyf.size())); // gid 6 ('-')
    for (keel::u8 b : make_rect_glyph(100, 318, 700, 382)) glyf.push_back(b);
    offsets.push_back(keel::as<keel::u32>(glyf.size())); // end sentinel

    keel::Array<keel::u8> loca;
    for (keel::u32 off : offsets) push_u32(loca, off);

    // 2. head (54 bytes)
    keel::Array<keel::u8> head(54u, 0u);
    head[18] = 0x03u; head[19] = 0xE8u; // unitsPerEm = 1000
    head[45] = spec.italic ? 0x02u : 0x00u; // macStyle
    head[50] = 0x00u; head[51] = 0x01u; // indexToLocFormat = 1

    // 3. maxp (6 bytes)
    keel::Array<keel::u8> maxp;
    push_u32(maxp, 0x00010000u);
    push_u16(maxp, 7u); // numGlyphs = 7

    // 4. hhea (36 bytes)
    keel::Array<keel::u8> hhea(36u, 0u);
    hhea[4] = 0x03u; hhea[5] = 0x20u; // ascender = 800
    hhea[6] = 0xFFu; hhea[7] = 0x38u; // descender = -200
    hhea[8] = 0x00u; hhea[9] = 0x64u; // lineGap = 100
    hhea[34] = 0x00u; hhea[35] = 0x07u; // numHMetrics = 7

    // 5. hmtx (7 * 4 bytes)
    keel::Array<keel::u8> hmtx;
    const keel::u16 advs[7] = {500u, 400u, 800u, 800u, 700u, 850u, 800u};
    for (keel::u32 i = 0u; i < 7u; ++i) {
        push_u16(hmtx, advs[i]);
        push_i16(hmtx, 100);
    }

    // 6. OS/2 (version 4, 96 bytes)
    keel::Array<keel::u8> os2(96u, 0u);
    os2[1] = 4u; // version 4
    os2[4] = keel::as<keel::u8>((spec.weightClass >> 8u) & 0xFFu);
    os2[5] = keel::as<keel::u8>(spec.weightClass & 0xFFu);
    os2[26] = 0x00u; os2[27] = 0x3Cu; // yStrikeoutSize = 60
    os2[28] = 0x01u; os2[29] = 0x18u; // yStrikeoutPosition = 280
    os2[63] = spec.italic ? 0x01u : 0x40u; // fsSelection
    os2[86] = 0x01u; os2[87] = 0xF4u; // sxHeight = 500
    os2[88] = 0x02u; os2[89] = 0xBCu; // sCapHeight = 700

    // 7. post (32 bytes)
    keel::Array<keel::u8> post(32u, 0u);
    post[1] = 0x03u; // version 3.0
    post[8] = 0xFFu; post[9] = 0x9Cu;  // underlinePosition = -100
    post[10] = 0x00u; post[11] = 0x32u; // underlineThickness = 50
    post[15] = spec.monospace ? 1u : 0u;

    // 8. name (2 Windows UTF-16BE records: nameID=1 family, nameID=2 subfamily)
    keel::Array<keel::u8> name;
    push_u16(name, 0u);  // format
    push_u16(name, 2u);  // count
    push_u16(name, 30u); // stringOffset (6 + 2*12)
    const keel::u16 famBytes = keel::as<keel::u16>(spec.family.size() * 2u);
    const keel::u16 subBytes = keel::as<keel::u16>(spec.subfamily.size() * 2u);
    push_u16(name, 3u); push_u16(name, 1u); push_u16(name, 0x0409u); push_u16(name, 1u); push_u16(name, famBytes); push_u16(name, 0u);
    push_u16(name, 3u); push_u16(name, 1u); push_u16(name, 0x0409u); push_u16(name, 2u); push_u16(name, subBytes); push_u16(name, famBytes);
    for (keel::usize i = 0u; i < spec.family.size(); ++i) push_u16(name, keel::as<keel::u8>(spec.family[i]));
    for (keel::usize i = 0u; i < spec.subfamily.size(); ++i) push_u16(name, keel::as<keel::u8>(spec.subfamily[i]));

    // 9. cmap (Format 4: ' ', '-', 'A', 'B', 'H', 'x', 0xFFFF)
    keel::Array<keel::u8> cmap;
    push_u16(cmap, 0u); push_u16(cmap, 1u);
    push_u16(cmap, 3u); push_u16(cmap, 1u); push_u32(cmap, 12u);
    const keel::u16 codes[7] = {0x0020u, 0x002Du, 0x0041u, 0x0042u, 0x0048u, 0x0078u, 0xFFFFu};
    const keel::i16 deltas[7] = {
        keel::as<keel::i16>(1 - 0x0020),
        keel::as<keel::i16>(6 - 0x002D),
        keel::as<keel::i16>(2 - 0x0041),
        keel::as<keel::i16>(5 - 0x0042),
        keel::as<keel::i16>(3 - 0x0048),
        keel::as<keel::i16>(4 - 0x0078),
        1
    };
    const keel::u16 segCount = 7u;
    push_u16(cmap, 4u);
    push_u16(cmap, keel::as<keel::u16>(16u + segCount * 8u));
    push_u16(cmap, 0u);
    push_u16(cmap, keel::as<keel::u16>(segCount * 2u));
    push_u16(cmap, 8u); push_u16(cmap, 2u); push_u16(cmap, 6u);
    for (keel::u32 i = 0u; i < segCount; ++i) push_u16(cmap, codes[i]);
    push_u16(cmap, 0u);
    for (keel::u32 i = 0u; i < segCount; ++i) push_u16(cmap, codes[i]);
    for (keel::u32 i = 0u; i < segCount; ++i) push_i16(cmap, deltas[i]);
    for (keel::u32 i = 0u; i < segCount; ++i) push_u16(cmap, 0u);

    // 10. kern (Format 0: ('A', 'H') -> -100)
    keel::Array<keel::u8> kern;
    push_u16(kern, 0u); push_u16(kern, 1u);
    push_u16(kern, 0u); push_u16(kern, 20u); push_u16(kern, 1u);
    push_u16(kern, 1u); push_u16(kern, 6u); push_u16(kern, 0u); push_u16(kern, 0u);
    push_u16(kern, 2u); push_u16(kern, 3u); push_i16(kern, -100);

    // 11. Optional Variable Font tables (`fvar`, `avar`, `gvar`, `HVAR`)
    // Axis 0: 'wght' min=100, default=400, max=900; 1 NamedInstance: Bold (700)
    // gvar for gid 2 ('A'): sparse points {0, 2} with dx={0, +200}, dy={0, 0} ->
    //   point 0 (100,0) untouched/delta=0, point 1 (100,700) IUP -> x=100 matches p0 -> dx=0,
    //   point 2 (700,700) delta=+200, point 3 (700,0) IUP -> x=700 matches p2 -> dx=+200!
    //   So 'A' smoothly widens from 600 units to 800 units (+200) at wght=900!
    // HVAR for gid 2 ('A'): +200 units advanceWidth at wght=900!
    keel::Array<keel::u8> fvar, avar, gvar, hvar;
    if (spec.variable) {
        // fvar
        push_u16(fvar, 1u); push_u16(fvar, 0u);
        push_u16(fvar, 16u); push_u16(fvar, 2u);
        push_u16(fvar, 1u); push_u16(fvar, 20u); // 1 axis
        push_u16(fvar, 1u); push_u16(fvar, 8u);  // 1 instance (4 + 1*4)
        push_u32(fvar, keel::font::make_tag('w', 'g', 'h', 't'));
        push_i32(fvar, 100 * 65536);
        push_i32(fvar, 400 * 65536);
        push_i32(fvar, 900 * 65536);
        push_u16(fvar, 0u); push_u16(fvar, 256u);
        // Instance 0: Bold (wght=700)
        push_u16(fvar, 257u); push_u16(fvar, 0u);
        push_i32(fvar, 700 * 65536);

        // avar (identity piecewise map: -1->-1, 0->0, +1->+1)
        push_u16(avar, 1u); push_u16(avar, 0u);
        push_u16(avar, 0u); push_u16(avar, 1u);
        push_u16(avar, 3u);
        push_i16(avar, -16384); push_i16(avar, -16384);
        push_i16(avar, 0);      push_i16(avar, 0);
        push_i16(avar, 16384);  push_i16(avar, 16384);

        // gvar (flags=1 -> 32-bit offsets, 1 shared tuple [+1.0], only gid 2 has data)
        // Serialized data for gid 2:
        //   private points: count=2, run(1 byte, 2 pts -> ctrl=1): pt0=0, pt2=+2
        //   X deltas: word run (2 values -> ctrl=0x41): 0, +200
        //   Y deltas: zero run (2 values -> ctrl=0x81)
        keel::Array<keel::u8> gid2Ser;
        push_u8(gid2Ser, 2u); // 2 points
        push_u8(gid2Ser, 1u); // 8-bit delta run, count=2
        push_u8(gid2Ser, 0u); // pt 0
        push_u8(gid2Ser, 2u); // pt 2
        push_u8(gid2Ser, 0x41u); // 16-bit delta run, count=2 (X)
        push_i16(gid2Ser, 0);
        push_i16(gid2Ser, 200);
        push_u8(gid2Ser, 0x81u); // zero run, count=2 (Y)

        keel::Array<keel::u8> gid2Data;
        push_u16(gid2Data, 1u); // tupleVariationCount = 1 (no shared points)
        push_u16(gid2Data, 8u); // dataOffset = 4 (hdr) + 4 (1 tuple hdr)
        push_u16(gid2Data, keel::as<keel::u16>(gid2Ser.size()));
        push_u16(gid2Data, 0x2000u | 0u); // PRIVATE_POINT_NUMBERS | sharedTupleIndex 0
        for (keel::u8 b : gid2Ser) gid2Data.push_back(b);

        const keel::u32 offsetsBytes = 8u * 4u; // 7 glyphs + 1 sentinel
        const keel::u32 sharedTuplesOff = 20u + offsetsBytes;
        const keel::u32 dataArrayOff = sharedTuplesOff + 2u; // 1 shared tuple * 1 axis * 2 bytes

        push_u16(gvar, 1u); push_u16(gvar, 0u);
        push_u16(gvar, 1u); // axisCount
        push_u16(gvar, 1u); // sharedTupleCount
        push_u32(gvar, sharedTuplesOff);
        push_u16(gvar, 7u); // glyphCount
        push_u16(gvar, 1u); // flags = 32-bit offsets
        push_u32(gvar, dataArrayOff);
        // offsets[0..7]: only gid 2 has [0..gid2Data.size()]
        push_u32(gvar, 0u); // gid 0
        push_u32(gvar, 0u); // gid 1
        push_u32(gvar, 0u); // gid 2 start
        const keel::u32 dSz = keel::as<keel::u32>(gid2Data.size());
        push_u32(gvar, dSz); // gid 3
        push_u32(gvar, dSz); // gid 4
        push_u32(gvar, dSz); // gid 5
        push_u32(gvar, dSz); // gid 6
        push_u32(gvar, dSz); // end
        // sharedTuples[0] = [+1.0]
        push_i16(gvar, 16384);
        for (keel::u8 b : gid2Data) gvar.push_back(b);

        // HVAR: direct mapping (advanceWidthMappingOffset = 0), IVS at offset 20
        // Region 0: [0.0, 1.0, 1.0]; IVD[0]: 7 items, wordDeltaCount=1 (16-bit), 1 region
        // item[2] ('A') = +200, all others = 0
        push_u16(hvar, 1u); push_u16(hvar, 0u);
        push_u32(hvar, 20u); // itemVariationStoreOffset
        push_u32(hvar, 0u);  // advanceWidthMappingOffset = direct (outer=0, inner=gid)
        push_u32(hvar, 0u);  // lsbMappingOffset
        push_u32(hvar, 0u);  // rsbMappingOffset
        // ItemVariationStore at +20:
        push_u16(hvar, 1u);  // format = 1
        push_u32(hvar, 12u); // variationRegionListOffset (relative to IVS)
        push_u16(hvar, 1u);  // itemVariationDataCount = 1
        push_u32(hvar, 22u); // itemVariationDataOffsets[0] (12 + 4 + 6 = 22)
        // VariationRegionList at IVS+12:
        push_u16(hvar, 1u);  // axisCount = 1
        push_u16(hvar, 1u);  // regionCount = 1
        push_i16(hvar, 0); push_i16(hvar, 16384); push_i16(hvar, 16384); // [0, 1, 1]
        // ItemVariationData[0] at IVS+22:
        push_u16(hvar, 7u); // itemCount = 7
        push_u16(hvar, 1u); // wordDeltaCount = 1 (16-bit deltas)
        push_u16(hvar, 1u); // regionIndexCount = 1
        push_u16(hvar, 0u); // regionIndexes[0] = 0
        const keel::i16 advDeltas[7] = {0, 0, 200, 0, 0, 0, 0};
        for (keel::u32 i = 0u; i < 7u; ++i) push_i16(hvar, advDeltas[i]);
    }

    struct Tbl {
        keel::u32 tag;
        const keel::Array<keel::u8>* data;
    };
    keel::SmallArray<Tbl, 16> tables;
    tables.push_back({keel::font::make_tag('O', 'S', '/', '2'), &os2});
    tables.push_back({keel::font::make_tag('c', 'm', 'a', 'p'), &cmap});
    tables.push_back({keel::font::make_tag('g', 'l', 'y', 'f'), &glyf});
    tables.push_back({keel::font::make_tag('h', 'e', 'a', 'd'), &head});
    tables.push_back({keel::font::make_tag('h', 'h', 'e', 'a'), &hhea});
    tables.push_back({keel::font::make_tag('h', 'm', 't', 'x'), &hmtx});
    tables.push_back({keel::font::make_tag('k', 'e', 'r', 'n'), &kern});
    tables.push_back({keel::font::make_tag('l', 'o', 'c', 'a'), &loca});
    tables.push_back({keel::font::make_tag('m', 'a', 'x', 'p'), &maxp});
    tables.push_back({keel::font::make_tag('n', 'a', 'm', 'e'), &name});
    tables.push_back({keel::font::make_tag('p', 'o', 's', 't'), &post});
    if (spec.variable) {
        tables.push_back({keel::font::make_tag('H', 'V', 'A', 'R'), &hvar});
        tables.push_back({keel::font::make_tag('a', 'v', 'a', 'r'), &avar});
        tables.push_back({keel::font::make_tag('f', 'v', 'a', 'r'), &fvar});
        tables.push_back({keel::font::make_tag('g', 'v', 'a', 'r'), &gvar});
    }

    const keel::u16 numTables = keel::as<keel::u16>(tables.size());
    keel::Array<keel::u8> ttf;
    push_u32(ttf, 0x00010000u);
    push_u16(ttf, numTables);
    push_u16(ttf, 128u); push_u16(ttf, 3u); push_u16(ttf, 0u);

    keel::u32 dataOffset = 12u + keel::as<keel::u32>(numTables) * 16u;
    for (keel::u16 i = 0u; i < numTables; ++i) {
        push_u32(ttf, tables[i].tag);
        push_u32(ttf, 0u);
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

// Packs two TTF binaries into a valid TrueType Collection ('.ttc')
keel::Array<keel::u8> build_synthetic_ttc(const keel::Array<keel::u8>& f0, const keel::Array<keel::u8>& f1) {
    auto relocate_table_offsets = [](const keel::Array<keel::u8>& src, keel::u32 baseOff) {
        keel::Array<keel::u8> out = src.clone();
        const keel::u16 nTbl = keel::as<keel::u16>((keel::as<keel::u32>(out[4]) << 8u) | out[5]);
        for (keel::u16 i = 0u; i < nTbl; ++i) {
            const keel::usize pos = 12u + keel::as<keel::usize>(i) * 16u + 8u;
            const keel::u32 oldOff =
                (keel::as<keel::u32>(out[pos]) << 24u) |
                (keel::as<keel::u32>(out[pos + 1u]) << 16u) |
                (keel::as<keel::u32>(out[pos + 2u]) << 8u) |
                keel::as<keel::u32>(out[pos + 3u]);
            const keel::u32 newOff = oldOff + baseOff;
            out[pos] = keel::as<keel::u8>((newOff >> 24u) & 0xFFu);
            out[pos + 1u] = keel::as<keel::u8>((newOff >> 16u) & 0xFFu);
            out[pos + 2u] = keel::as<keel::u8>((newOff >> 8u) & 0xFFu);
            out[pos + 3u] = keel::as<keel::u8>(newOff & 0xFFu);
        }
        return out;
    };

    const keel::u32 off0 = 20u; // 12-byte TTC header + 2 * 4-byte face offsets
    const keel::u32 off1 = off0 + keel::as<keel::u32>(f0.size());
    const keel::Array<keel::u8> r0 = relocate_table_offsets(f0, off0);
    const keel::Array<keel::u8> r1 = relocate_table_offsets(f1, off1);

    keel::Array<keel::u8> ttc;
    push_u32(ttc, keel::font::make_tag('t', 't', 'c', 'f'));
    push_u16(ttc, 1u); push_u16(ttc, 0u);
    push_u32(ttc, 2u); // numFonts = 2
    push_u32(ttc, off0);
    push_u32(ttc, off1);
    for (keel::u8 b : r0) ttc.push_back(b);
    for (keel::u8 b : r1) ttc.push_back(b);
    return ttc;
}

} // namespace

int main() {
    keel::println("[RUN] Testing keel::font v0.2...");

    using namespace keel::font;

    // 1. Bounds safety on empty / invalid buffers
    {
        CHECK(FontFace::face_count(keel::ArrayRef<keel::u8>{}) == 0u);
        FontFace bad;
        CHECK(!bad.is_valid());
        CHECK(!bad.load(keel::ArrayRef<keel::u8>{}));
        CHECK(!bad.load(static_cast<const keel::u8*>(nullptr), 0u));
        const keel::u8 garbage[16] = {0xDE, 0xAD, 0xBE, 0xEF, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
        CHECK(FontFace::face_count(garbage, sizeof(garbage)) == 0u);
        CHECK(!bad.load(garbage, sizeof(garbage)));
        GlyphBitmap bm{};
        CHECK(!bad.rasterize(0u, 16.0f, {}, bm));
    }

    // 2. Metadata, metrics, cmap (incl. O(1) ASCII LUT), and kerning
    const keel::Array<keel::u8> ttf = build_synthetic_ttf(SynthFontSpec{});
    CHECK(FontFace::face_count(ttf) == 1u);
    FontFace face;
    CHECK(face.load(ttf));
    CHECK(!face.load(ttf, 1u)); // faceIndex out of range for single TTF
    CHECK(face.load(ttf, 0u));
    CHECK(face.is_valid());
    CHECK(!face.is_cff());
    CHECK(!face.is_variable());
    CHECK(face.family_name() == "KeelSans");
    CHECK(face.subfamily_name() == "Regular");
    CHECK(face.weight_class() == 400u);
    CHECK(!face.is_italic());
    CHECK(!face.is_monospace());
    CHECK(face.units_per_em() == 1000u);
    CHECK(face.num_glyphs() == 7u);
    CHECK(face.ascender() == 800);
    CHECK(face.descender() == -200);
    CHECK(face.line_gap() == 100);
    CHECK(face.cap_height() == 700);
    CHECK(face.x_height() == 500);
    CHECK(face.underline_position() == -100);
    CHECK(face.underline_thickness() == 50);
    CHECK(face.strikeout_position() == 280);
    CHECK(face.strikeout_size() == 60);
    CHECK(face.has_kerning());

    const keel::u32 gSpace = face.glyph_index(' ');
    const keel::u32 gDash = face.glyph_index('-');
    const keel::u32 gA = face.glyph_index('A');
    const keel::u32 gB = face.glyph_index('B');
    const keel::u32 gH = face.glyph_index('H');
    CHECK(gSpace == 1u && gA == 2u && gH == 3u && gB == 5u && gDash == 6u);
    CHECK(face.kerning_units(gA, gH) == -100);
    CHECK_NEAR(face.kerning_px(gA, gH, 20.0f, Hinting::Light), -2.0f, 0.01f);
    CHECK_NEAR(face.advance_width_px(gA, 20.0f, Hinting::Light, false), 16.0f, 0.01f);

    // 3. Fast unrasterized glyph_metrics()
    {
        GlyphMetrics gm{};
        CHECK(face.glyph_metrics(gA, 20.0f, Hinting::None, gm));
        CHECK(!gm.empty);
        CHECK_NEAR(gm.advancePx, 16.0f, 0.01f);
        CHECK_NEAR(gm.minX, 2.0f, 0.01f);
        CHECK_NEAR(gm.maxX, 14.0f, 0.01f);
        CHECK_NEAR(gm.minY, -14.0f, 0.01f);
        CHECK_NEAR(gm.maxY, 0.0f, 0.01f);
    }

    // 4. Vector outline extraction API (`extract_outline`)
    {
        struct Counter {
            keel::u32 moves{0}, lines{0}, quads{0}, cubics{0}, closes{0};
        } cnt{};
        OutlineSink sink{};
        sink.user = &cnt;
        sink.move_to = [](void* u, keel::f32, keel::f32) { ++static_cast<Counter*>(u)->moves; };
        sink.line_to = [](void* u, keel::f32, keel::f32) { ++static_cast<Counter*>(u)->lines; };
        sink.quad_to = [](void* u, keel::f32, keel::f32, keel::f32, keel::f32) { ++static_cast<Counter*>(u)->quads; };
        sink.cubic_to = [](void* u, keel::f32, keel::f32, keel::f32, keel::f32, keel::f32, keel::f32) { ++static_cast<Counter*>(u)->cubics; };
        sink.close = [](void* u) { ++static_cast<Counter*>(u)->closes; };
        CHECK(face.extract_outline(gA, 20.0f, {.hinting = Hinting::None}, sink));
        CHECK(cnt.moves == 1u);
        CHECK(cnt.lines == 3u);
        CHECK(cnt.closes == 1u);
    }

    // 5. Persistent RasterScratch + analytical coverage + LCD + composite + stem snapping
    RasterScratch scratch;
    {
        GlyphBitmap bm{};
        CHECK(face.rasterize(gA, 20.0f, {.hinting = Hinting::None, .subpixel = false}, bm, &scratch));
        CHECK(!bm.empty && bm.width == 12u && bm.height == 14u && bm.bearingX == 2 && bm.bearingY == 14);
        keel::usize fullPx = 0u;
        for (keel::u8 px : bm.pixels) if (px == 255u) ++fullPx;
        CHECK(fullPx == 12u * 14u);

        GlyphBitmap lcd{};
        CHECK(face.rasterize(gA, 20.0f, {.hinting = Hinting::Light, .subpixel = true, .subpixelPhase = 1u}, lcd, &scratch));
        CHECK(!lcd.empty && lcd.isSubpixel && lcd.strideBytes == lcd.width * 3u);

        GlyphBitmap comp{};
        CHECK(face.rasterize(gB, 20.0f, {.hinting = Hinting::None}, comp, &scratch));
        CHECK(!comp.empty && comp.bearingX == 3 && comp.bearingY == 16);

        // Horizontal stem snapping on thin bar '-' at 14px:
        // Unhinted y in [318..382]*0.014 = [4.452..5.348] (height 0.896px spread across 2 rows -> soft)
        // Hinted snaps to integer 1.0px row -> peak coverage hits 255!
        GlyphBitmap dashNone{}, dashLight{};
        CHECK(face.rasterize(gDash, 14.0f, {.hinting = Hinting::None}, dashNone, &scratch));
        CHECK(face.rasterize(gDash, 14.0f, {.hinting = Hinting::Light}, dashLight, &scratch));
        keel::u8 maxNone = 0u, maxLight = 0u;
        for (keel::u8 v : dashNone.pixels) if (v > maxNone) maxNone = v;
        for (keel::u8 v : dashLight.pixels) if (v > maxLight) maxLight = v;
        CHECK(maxNone < 200u);
        CHECK(maxLight == 255u);
    }

    // 6. Multi-face TrueType Collection (`.ttc`)
    {
        const keel::Array<keel::u8> fReg = build_synthetic_ttf(SynthFontSpec{
            .family = "KeelPack", .subfamily = "Regular", .weightClass = 400u, .italic = false, .monospace = false
        });
        const keel::Array<keel::u8> fBoldIt = build_synthetic_ttf(SynthFontSpec{
            .family = "KeelPack", .subfamily = "Bold Italic", .weightClass = 700u, .italic = true, .monospace = true
        });
        const keel::Array<keel::u8> ttc = build_synthetic_ttc(fReg, fBoldIt);
        CHECK(FontFace::face_count(ttc) == 2u);

        FontFace f0, f1;
        CHECK(f0.load(ttc, 0u));
        CHECK(f1.load(ttc, 1u));
        CHECK(!f0.load(ttc, 2u));
        CHECK(f0.load(ttc, 0u));
        CHECK(f0.family_name() == "KeelPack" && f0.subfamily_name() == "Regular" && f0.weight_class() == 400u && !f0.is_italic());
        CHECK(f1.family_name() == "KeelPack" && f1.subfamily_name() == "Bold Italic" && f1.weight_class() == 700u && f1.is_italic() && f1.is_monospace());
    }

    // 7. Variable Font (`fvar` + `avar` + `gvar` with IUP + `HVAR`)
    {
        const keel::Array<keel::u8> varTtf = build_synthetic_ttf(SynthFontSpec{
            .family = "KeelVar", .subfamily = "Regular", .variable = true
        });
        FontFace vf;
        CHECK(vf.load(varTtf));
        CHECK(vf.is_variable());
        CHECK(vf.axis_count() == 1u);
        CHECK(vf.axes()[0].tag == make_tag('w', 'g', 'h', 't'));
        CHECK_NEAR(vf.axes()[0].minValue, 100.0f, 0.01f);
        CHECK_NEAR(vf.axes()[0].defaultValue, 400.0f, 0.01f);
        CHECK_NEAR(vf.axes()[0].maxValue, 900.0f, 0.01f);
        CHECK(vf.named_instances().size() == 1u);

        const keel::u32 vgA = vf.glyph_index('A');
        GlyphBitmap bmDefault{}, bmBold{}, bmMax{};
        CHECK(vf.rasterize(vgA, 20.0f, {.hinting = Hinting::None}, bmDefault, &scratch));
        CHECK(bmDefault.width == 12u); // 600 units * 0.02 = 12 px
        CHECK_NEAR(bmDefault.advancePx, 16.0f, 0.01f); // 800 units * 0.02 = 16 px

        // Select named instance 0 (wght=700 -> norm = 300/500 = 0.6 -> +120 units -> width 720 -> 14.4px -> 15px, adv=920 -> 18.4px)
        CHECK(vf.set_named_instance(0u));
        CHECK_NEAR(vf.normalized_coords()[0], 0.6f, 0.01f);
        CHECK(vf.rasterize(vgA, 20.0f, {.hinting = Hinting::None}, bmBold, &scratch));
        CHECK(bmBold.width == 15u);
        CHECK_NEAR(bmBold.advancePx, 18.4f, 0.05f);

        // Set explicit wght=900 (norm = 1.0 -> +200 units -> width 800 -> 16px, adv=1000 -> 20px, IUP widens both top & bottom corners!)
        CHECK(vf.set_variation(make_tag('w', 'g', 'h', 't'), 900.0f));
        CHECK_NEAR(vf.normalized_coords()[0], 1.0f, 0.01f);
        CHECK(vf.rasterize(vgA, 20.0f, {.hinting = Hinting::None}, bmMax, &scratch));
        CHECK(bmMax.width == 16u && bmMax.height == 14u);
        CHECK_NEAR(bmMax.advancePx, 20.0f, 0.01f);
        // Verify IUP kept the rectangle perfectly filled across all 16x14 pixels!
        keel::usize fullMax = 0u;
        for (keel::u8 px : bmMax.pixels) if (px == 255u) ++fullMax;
        CHECK(fullMax == 16u * 14u);

        // Reset restores default master
        vf.reset_variations();
        GlyphBitmap bmReset{};
        CHECK(vf.rasterize(vgA, 20.0f, {.hinting = Hinting::None}, bmReset, &scratch));
        CHECK(bmReset.width == 12u);
        CHECK_NEAR(bmReset.advancePx, 16.0f, 0.01f);
    }

    scratch.release();

    // 8. Truncation fuzzing on variable TTF
    {
        const keel::Array<keel::u8> varTtf = build_synthetic_ttf(SynthFontSpec{.variable = true});
        for (keel::usize cut = 0u; cut < varTtf.size(); cut += 19u) {
            FontFace f;
            if (f.load(varTtf.data(), cut)) {
                (void)f.set_variation(make_tag('w', 'g', 'h', 't'), 750.0f);
                GlyphBitmap bm{};
                (void)f.rasterize(f.glyph_index('A'), 16.0f, {}, bm);
            }
        }
    }

    if (g_failures == 0) {
        keel::println("[PASS] All keel::font v0.2 tests passed!");
        return 0;
    }
    keel::eprintln("[FAIL] Total failures: {}", g_failures);
    return 1;
}
