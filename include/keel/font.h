#pragma once

#include "keel/array.h"
#include "keel/core.h"
#include "keel/ref.h"
#include "keel/small_array.h"
#include "keel/static_array.h"
#include "keel/string.h"

namespace keel::font {

/// Packs a 4-character OpenType table or variation axis tag (e.g. `make_tag('w','g','h','t')`).
[[nodiscard]] constexpr keel::u32 make_tag(char a, char b, char c, char d) noexcept {
    return (keel::as<keel::u32>(keel::as<keel::u8>(a)) << 24u) |
           (keel::as<keel::u32>(keel::as<keel::u8>(b)) << 16u) |
           (keel::as<keel::u32>(keel::as<keel::u8>(c)) << 8u) |
           keel::as<keel::u32>(keel::as<keel::u8>(d));
}

/// Vertical autohinting & advance snapping policy.
enum class Hinting : keel::u8 {
    None = 0,   ///< Unhinted outlines, linear subpixel advances
    Light = 1,  ///< Vertical blue-zone + horizontal stem snapping, subpixel horizontal advances & phases
    Normal = 2, ///< Vertical blue-zone + horizontal stem snapping + integer-snapped horizontal advances
};

/// Variable font axis descriptor parsed from OpenType `fvar`.
struct VariationAxis {
    keel::u32 tag{0};         ///< OpenType 4-byte axis tag (`wght`, `wdth`, `slnt`, `ital`, `opsz`, ...)
    keel::f32 minValue{0.0f}; ///< Minimum user-space coordinate
    keel::f32 defaultValue{0.0f}; ///< Default user-space coordinate
    keel::f32 maxValue{0.0f}; ///< Maximum user-space coordinate
    keel::u16 flags{0};       ///< OpenType axis flags (bit 0 = HIDDEN_AXIS)
    keel::u16 nameId{0};      ///< `name` table ID for localized axis label
};

/// User-space variation axis assignment (e.g. `{make_tag('w','g','h','t'), 700.0f}`).
struct VariationCoord {
    keel::u32 tag{0};
    keel::f32 value{0.0f};
};

/// Named variable font instance descriptor parsed from OpenType `fvar`.
struct NamedInstance {
    keel::u16 subfamilyNameId{0};
    keel::SmallArray<keel::f32, 8> coords; ///< User-space coordinates ordered by `FontFace::axes()`
};

/// Scaled per-glyph geometric & advance metrics (computed without rasterizing a bitmap).
struct GlyphMetrics {
    keel::f32 advancePx{0.0f};
    keel::f32 lsbPx{0.0f};
    keel::f32 minX{0.0f};
    keel::f32 minY{0.0f};
    keel::f32 maxX{0.0f};
    keel::f32 maxY{0.0f};
    bool empty{true};
};

/// Callback sink for vector outline extraction (coordinates in pixels, Y-down relative to baseline).
struct OutlineSink {
    void* user{nullptr};
    void (*move_to)(void* user, keel::f32 x, keel::f32 y){nullptr};
    void (*line_to)(void* user, keel::f32 x, keel::f32 y){nullptr};
    void (*quad_to)(void* user, keel::f32 cx, keel::f32 cy, keel::f32 x, keel::f32 y){nullptr};
    void (*cubic_to)(void* user, keel::f32 c1x, keel::f32 c1y, keel::f32 c2x, keel::f32 c2y, keel::f32 x, keel::f32 y){nullptr};
    void (*close)(void* user){nullptr};
};

/// Parameters controlling single-glyph rasterization and outline extraction.
struct RasterOptions {
    Hinting hinting{Hinting::Light};
    bool subpixel{false};        ///< 3x horizontal LCD subpixel rasterization + 5-tap balanced FIR filter (RGB)
    keel::u8 subpixelPhase{0};   ///< Horizontal quarter-pixel phase [0..3] (honored when hinting != Normal)
    bool syntheticBold{false};   ///< Smooth morphological horizontal + subtle vertical embolden
    bool syntheticItalic{false}; ///< Horizontal slant shear (0.18)
};

/// Rasterized glyph bitmap and placement metrics.
struct GlyphBitmap {
    keel::Array<keel::u8> pixels; ///< 1 byte/px (Grayscale) or 3 bytes/px RGB (LCD subpixel)
    keel::u16 width{0};           ///< Width in logical pixels (LCD stores width * 3 bytes per row)
    keel::u16 height{0};          ///< Height in pixels
    keel::u16 strideBytes{0};     ///< Row stride in bytes
    keel::i16 bearingX{0};        ///< Horizontal offset from pen origin to left bitmap edge (pixels)
    keel::i16 bearingY{0};        ///< Vertical offset from baseline up to top bitmap edge (pixels)
    keel::f32 advancePx{0.0f};    ///< Horizontal pen advance in pixels
    bool isSubpixel{false};       ///< True if `pixels` contains 3-channel RGB LCD coverage
    bool empty{true};             ///< True for whitespace or empty glyphs (only `advancePx` applies)
};

/// Reusable scratch memory to avoid per-glyph heap allocations during rasterization.
struct RasterScratch {
    struct Segment {
        keel::f32 x0{0.0f};
        keel::f32 y0{0.0f};
        keel::f32 x1{0.0f};
        keel::f32 y1{0.0f};
    };

    keel::Array<Segment> segments;
    keel::Array<keel::f32> deltaBuf;
    keel::Array<keel::f32> coverageBuf;
    keel::Array<keel::f32> boldRowScratch;
    keel::Array<keel::u16> endPts;
    keel::Array<keel::u8> flags;
    keel::Array<keel::i32> rawX;
    keel::Array<keel::i32> rawY;
    keel::Array<keel::f32> ptsX;
    keel::Array<keel::f32> ptsY;
    keel::Array<keel::f32> varDeltaX;
    keel::Array<keel::f32> varDeltaY;
    keel::Array<keel::f32> tupleDeltaX;
    keel::Array<keel::f32> tupleDeltaY;
    keel::Array<keel::u8> varTouched;
    keel::Array<keel::u32> varPointIndices;

    /// Releases retained heap capacity across all scratch buffers.
    void release() noexcept;
};

/// Decoded SFNT (`.ttf` / `.ttc` / `.otf`) font face with zero third-party dependencies.
///
/// Supports TrueType (`glyf`/`loca`, simple & composite), OpenType CFF v1 (`CFF `),
/// TrueType Variable Fonts (`fvar`/`avar`/`gvar` with IUP/`HVAR`), Unicode `cmap` (12/4/6/0)
/// with O(1) ASCII LUT, `kern` + `GPOS` PairPos kerning, blue-zone + horizontal stem hinting,
/// vector outline extraction, analytical scanline AA, synthetic bold/italic, and 5-tap LCD subpixel.
///
/// Non-owning view over font bytes: the caller must keep the memory buffer passed to `load(...)`
/// alive for the lifetime of this `FontFace`.
class FontFace {
public:
    FontFace() noexcept = default;

    /// Returns the number of font faces stored in `data` (`1` for TTF/OTF, `N` for TTC, `0` if invalid).
    [[nodiscard]] static keel::u32 face_count(keel::ArrayRef<keel::u8> data) noexcept;
    [[nodiscard]] static keel::u32 face_count(const keel::u8* data, keel::usize size) noexcept;

    /// Parses SFNT font tables from non-owning memory buffer (`faceIndex` selects a face inside `.ttc`).
    bool load(keel::ArrayRef<keel::u8> data, keel::u32 faceIndex = 0u) noexcept;
    bool load(const keel::u8* data, keel::usize size, keel::u32 faceIndex = 0u) noexcept;

    // --- Core Font Metadata & Vertical Metrics ---
    [[nodiscard]] bool is_valid() const noexcept { return m_valid; }
    [[nodiscard]] bool is_cff() const noexcept { return m_isCff; }
    [[nodiscard]] keel::u16 units_per_em() const noexcept { return m_unitsPerEm; }
    [[nodiscard]] keel::u16 num_glyphs() const noexcept { return m_numGlyphs; }
    [[nodiscard]] keel::i16 ascender() const noexcept { return m_ascender; }
    [[nodiscard]] keel::i16 descender() const noexcept { return m_descender; }
    [[nodiscard]] keel::i16 line_gap() const noexcept { return m_lineGap; }
    [[nodiscard]] keel::i16 cap_height() const noexcept { return m_capHeight; }
    [[nodiscard]] keel::i16 x_height() const noexcept { return m_xHeight; }
    [[nodiscard]] keel::i16 underline_position() const noexcept { return m_underlinePos; }
    [[nodiscard]] keel::i16 underline_thickness() const noexcept { return m_underlineThickness; }
    [[nodiscard]] keel::i16 strikeout_position() const noexcept { return m_strikeoutPos; }
    [[nodiscard]] keel::i16 strikeout_size() const noexcept { return m_strikeoutSize; }
    [[nodiscard]] keel::u16 weight_class() const noexcept { return m_weightClass; }
    [[nodiscard]] bool is_italic() const noexcept { return m_isItalic; }
    [[nodiscard]] bool is_monospace() const noexcept { return m_isMonospace; }
    [[nodiscard]] bool has_kerning() const noexcept { return m_kernOffset != 0u || !m_gposKernLookups.empty(); }
    [[nodiscard]] keel::StringRef family_name() const noexcept { return keel::StringRef(m_familyName.data(), m_familyName.size()); }
    [[nodiscard]] keel::StringRef subfamily_name() const noexcept { return keel::StringRef(m_subfamilyName.data(), m_subfamilyName.size()); }

    // --- Variable Font API (`fvar` / `avar` / `gvar` / `HVAR`) ---
    [[nodiscard]] bool is_variable() const noexcept { return !m_axes.empty(); }
    [[nodiscard]] keel::usize axis_count() const noexcept { return m_axes.size(); }
    [[nodiscard]] keel::ArrayRef<VariationAxis> axes() const noexcept {
        return keel::ArrayRef<VariationAxis>(m_axes.data(), m_axes.size());
    }
    [[nodiscard]] keel::ArrayRef<NamedInstance> named_instances() const noexcept {
        return keel::ArrayRef<NamedInstance>(m_namedInstances.data(), m_namedInstances.size());
    }
    [[nodiscard]] keel::ArrayRef<keel::f32> normalized_coords() const noexcept {
        return keel::ArrayRef<keel::f32>(m_normCoords.data(), m_normCoords.size());
    }
    bool set_variation(keel::u32 axisTag, keel::f32 userValue) noexcept;
    void set_variations(keel::ArrayRef<VariationCoord> coords) noexcept;
    bool set_named_instance(keel::usize instanceIndex) noexcept;
    void reset_variations() noexcept;

    // --- Fast Glyph Lookup, Kerning & Unrasterized Metrics ---
    /// Maps a Unicode codepoint to a font glyph index (`0` = `.notdef`). O(1) for ASCII [0..127].
    [[nodiscard]] keel::u32 glyph_index(keel::u32 codepoint) const noexcept;

    /// Returns horizontal kerning adjustment in font units between two glyph indices.
    [[nodiscard]] keel::i32 kerning_units(keel::u32 leftGlyph, keel::u32 rightGlyph) const noexcept;

    /// Returns scaled horizontal kerning adjustment in pixels between two glyph indices.
    [[nodiscard]] keel::f32 kerning_px(
        keel::u32 leftGlyph,
        keel::u32 rightGlyph,
        keel::f32 fontSizePx,
        Hinting hinting = Hinting::Light
    ) const noexcept;

    /// Returns unscaled horizontal advance width in font units (incorporating `HVAR`/`gvar` when variable).
    [[nodiscard]] keel::u16 advance_width_units(keel::u32 glyphIndex) const noexcept;

    /// Returns unscaled left side bearing in font units.
    [[nodiscard]] keel::i16 left_side_bearing_units(keel::u32 glyphIndex) const noexcept;

    /// Returns scaled horizontal advance width in pixels without rasterizing a bitmap.
    [[nodiscard]] keel::f32 advance_width_px(
        keel::u32 glyphIndex,
        keel::f32 fontSizePx,
        Hinting hinting = Hinting::Light,
        bool syntheticBold = false
    ) const noexcept;

    /// Computes scaled advance and bounding box for `glyphIndex` without rasterizing a bitmap.
    bool glyph_metrics(
        keel::u32 glyphIndex,
        keel::f32 fontSizePx,
        Hinting hinting,
        GlyphMetrics& out
    ) const;

    // --- Vector Outline Extraction & Analytical Rasterization ---
    /// Emits vector glyph contours (MoveTo / LineTo / QuadTo / CubicTo / Close) in pixel space (Y-down).
    bool extract_outline(
        keel::u32 glyphIndex,
        keel::f32 fontSizePx,
        const RasterOptions& opts,
        const OutlineSink& sink,
        RasterScratch* scratch = nullptr
    ) const;

    /// Rasterizes `glyphIndex` at `fontSizePx` into `out`. Pass a persistent `RasterScratch` for zero-alloc hot path.
    bool rasterize(
        keel::u32 glyphIndex,
        keel::f32 fontSizePx,
        const RasterOptions& opts,
        GlyphBitmap& out,
        RasterScratch* scratch = nullptr
    ) const;

private:
    struct TableRecord {
        keel::u32 offset{0};
        keel::u32 length{0};
    };

    struct AvarSegment {
        keel::f32 fromCoord{0.0f};
        keel::f32 toCoord{0.0f};
    };

    struct CffIndex {
        keel::u32 count{0};
        keel::u8 offSize{0};
        keel::u32 offsetsStart{0};
        keel::u32 dataStart{0};
        keel::u32 endOffset{0};
    };

    struct CffFdInfo {
        keel::u32 privateOffset{0};
        keel::u32 privateSize{0};
        CffIndex localSubrs{};
    };

    struct OutlineBuilder {
        keel::Array<RasterScratch::Segment>* segments{nullptr};
        const OutlineSink* sink{nullptr};
        keel::f32 curX{0.0f};
        keel::f32 curY{0.0f};
        keel::f32 startX{0.0f};
        keel::f32 startY{0.0f};
        bool hasContour{false};

        void move_to(keel::f32 x, keel::f32 y);
        void line_to(keel::f32 x, keel::f32 y);
        void quad_to(keel::f32 cx, keel::f32 cy, keel::f32 x, keel::f32 y, keel::f32 flatness);
        void cubic_to(keel::f32 c1x, keel::f32 c1y, keel::f32 c2x, keel::f32 c2y, keel::f32 x, keel::f32 y, keel::f32 flatness);
        void close_contour();
    };

    struct HintParams {
        bool enabled{false};
        bool snapStems{false};
        keel::f32 scale{1.0f};
        keel::f32 descenderUnits{0.0f};
        keel::f32 xHeightUnits{0.0f};
        keel::f32 capHeightUnits{0.0f};
        keel::f32 descenderPx{0.0f};
        keel::f32 xHeightPx{0.0f};
        keel::f32 capHeightPx{0.0f};
        keel::f32 overshootClampUnits{0.0f};

        [[nodiscard]] keel::f32 map_y(keel::f32 fy) const noexcept;
    };

    struct Transform2D {
        keel::f32 scaleX{1.0f};
        keel::f32 italicShear{0.0f};
        keel::f32 shiftX{0.0f};
        HintParams hint{};

        [[nodiscard]] keel::f32 transform_y_up(keel::f32 fy) const noexcept {
            return hint.map_y(fy);
        }
        [[nodiscard]] keel::f32 transform_x(keel::f32 fx, keel::f32 pyUp) const noexcept {
            return fx * scaleX + italicShear * pyUp + shiftX;
        }
    };

    [[nodiscard]] bool read_u8(keel::usize off, keel::u8& out) const noexcept;
    [[nodiscard]] bool read_u16(keel::usize off, keel::u16& out) const noexcept;
    [[nodiscard]] bool read_i16(keel::usize off, keel::i16& out) const noexcept;
    [[nodiscard]] bool read_u32(keel::usize off, keel::u32& out) const noexcept;
    [[nodiscard]] bool read_i32(keel::usize off, keel::i32& out) const noexcept;

    [[nodiscard]] keel::u32 lookup_cmap_uncached(keel::u32 codepoint) const noexcept;
    [[nodiscard]] bool parse_cmap(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_name(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_gpos(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_fvar(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_avar(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_gvar(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_hvar(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_cff(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_cff_index(keel::usize off, CffIndex& out) const noexcept;
    [[nodiscard]] bool get_cff_index_item(const CffIndex& idx, keel::u32 i, keel::u32& outOff, keel::u32& outLen) const noexcept;
    [[nodiscard]] bool parse_cff_private_dict(keel::u32 privOff, keel::u32 privSize, CffFdInfo& outFd) const noexcept;

    [[nodiscard]] keel::f32 normalize_axis_value(keel::usize axisIdx, keel::f32 userValue) const noexcept;
    void update_has_active_variation() noexcept;
    [[nodiscard]] keel::f32 evaluate_hvar_advance_delta(keel::u32 glyphIndex) const noexcept;
    [[nodiscard]] keel::f32 evaluate_item_variation_store(keel::u32 ivsOff, keel::u16 outerIdx, keel::u16 innerIdx) const noexcept;
    void apply_gvar_deltas(
        keel::u32 glyphIndex,
        const keel::Array<keel::i32>& origX,
        const keel::Array<keel::i32>& origY,
        const keel::Array<keel::u16>& endPts,
        keel::u32 numPts,
        RasterScratch& scratch
    ) const noexcept;

    [[nodiscard]] bool get_glyph_byte_range(keel::u32 glyphIndex, keel::u32& outOff, keel::u32& outLen) const noexcept;
    [[nodiscard]] Transform2D make_transform(
        keel::f32 fontSizePx,
        const RasterOptions& opts
    ) const noexcept;
    void apply_horizontal_stem_snapping(
        keel::Array<keel::f32>& ptsX,
        keel::Array<keel::f32>& ptsY,
        const keel::Array<keel::u8>& flags,
        const keel::Array<keel::u16>& endPts,
        const HintParams& hint
    ) const noexcept;

    [[nodiscard]] bool extract_ttf_glyph_outline(
        keel::u32 glyphIndex,
        keel::f32 m00, keel::f32 m01, keel::f32 m10, keel::f32 m11,
        keel::f32 tx, keel::f32 ty,
        const Transform2D& xf,
        keel::f32 flatness,
        keel::u32 depth,
        OutlineBuilder& out,
        RasterScratch& scratch,
        keel::Array<keel::f32>* outPointsX = nullptr,
        keel::Array<keel::f32>* outPointsY = nullptr
    ) const;

    [[nodiscard]] bool extract_cff_glyph_outline(
        keel::u32 glyphIndex,
        const Transform2D& xf,
        keel::f32 flatness,
        OutlineBuilder& out
    ) const;

    [[nodiscard]] keel::i32 lookup_gpos_kern_subtable(keel::u32 subtableOff, keel::u32 leftGlyph, keel::u32 rightGlyph) const noexcept;
    [[nodiscard]] keel::i32 lookup_coverage_index(keel::u32 covOff, keel::u32 glyphId) const noexcept;
    [[nodiscard]] keel::u16 lookup_class_def(keel::u32 classDefOff, keel::u32 glyphId) const noexcept;

    void compute_glyph_units_bbox(keel::u32 glyphIndex, keel::i16& outYMin, keel::i16& outYMax) const noexcept;

    const keel::u8* m_data{nullptr};
    keel::usize m_size{0};
    bool m_valid{false};
    bool m_isCff{false};

    // Core SFNT metrics & metadata
    keel::u16 m_unitsPerEm{1000u};
    keel::u16 m_numGlyphs{0u};
    keel::u16 m_numHMetrics{0u};
    keel::i16 m_indexToLocFormat{0};
    keel::i16 m_ascender{800};
    keel::i16 m_descender{-200};
    keel::i16 m_lineGap{0};
    keel::i16 m_capHeight{700};
    keel::i16 m_xHeight{500};
    keel::i16 m_descenderBlue{-200};
    keel::i16 m_underlinePos{-100};
    keel::i16 m_underlineThickness{50};
    keel::i16 m_strikeoutPos{260};
    keel::i16 m_strikeoutSize{50};
    keel::u16 m_weightClass{400u};
    bool m_isItalic{false};
    bool m_isMonospace{false};
    keel::SmallString<64> m_familyName{};
    keel::SmallString<64> m_subfamilyName{};

    // O(1) ASCII glyph lookup cache
    keel::StaticArray<keel::u32, 128> m_asciiGlyphs{};

    // Table offsets
    keel::u32 m_hmtxOffset{0u};
    keel::u32 m_locaOffset{0u};
    keel::u32 m_locaLength{0u};
    keel::u32 m_glyfOffset{0u};
    keel::u32 m_glyfLength{0u};
    keel::u32 m_kernOffset{0u};
    keel::u32 m_kernLength{0u};

    // Selected Unicode cmap subtable
    keel::u32 m_cmapSubtableOffset{0u};
    keel::u16 m_cmapFormat{0u};

    // OpenType GPOS PairPos subtables
    keel::Array<keel::u32> m_gposKernLookups;

    // Variable Font state (`fvar` / `avar` / `gvar` / `HVAR`)
    keel::SmallArray<VariationAxis, 8> m_axes;
    keel::SmallArray<keel::f32, 8> m_userCoords;
    keel::SmallArray<keel::f32, 8> m_normCoords;
    keel::Array<NamedInstance> m_namedInstances;
    keel::Array<keel::Array<AvarSegment>> m_avarMaps;
    bool m_hasActiveVariation{false};

    keel::u32 m_gvarOffset{0u};
    keel::u32 m_gvarLength{0u};
    keel::u16 m_gvarAxisCount{0u};
    keel::u16 m_gvarSharedTupleCount{0u};
    keel::u32 m_gvarSharedTuplesOffset{0u};
    keel::u16 m_gvarGlyphCount{0u};
    keel::u16 m_gvarFlags{0u};
    keel::u32 m_gvarDataArrayOffset{0u};

    keel::u32 m_hvarOffset{0u};
    keel::u32 m_hvarIvsOffset{0u};
    keel::u32 m_hvarAdvMapOffset{0u};

    // OpenType CFF state
    keel::u32 m_cffOffset{0u};
    keel::u32 m_cffLength{0u};
    CffIndex m_cffCharStrings{};
    CffIndex m_cffGlobalSubrs{};
    CffFdInfo m_cffDefaultFd{};
    keel::Array<CffFdInfo> m_cffFdArray;
    keel::u32 m_cffFdSelectOffset{0u};
};

} // namespace keel::font
