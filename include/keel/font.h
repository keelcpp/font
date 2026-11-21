#pragma once

#include "keel/array.h"
#include "keel/core.h"
#include "keel/ref.h"

namespace keel::font {

/// Vertical autohinting & advance snapping policy.
enum class Hinting : keel::u8 {
    None = 0,   ///< Unhinted outlines, linear subpixel advances
    Light = 1,  ///< Vertical blue-zone snapping, subpixel horizontal advances & phases
    Normal = 2, ///< Vertical blue-zone snapping + integer-snapped horizontal advances
};

/// Parameters controlling single-glyph rasterization.
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

/// Decoded SFNT (`.ttf` / `.ttc` / `.otf`) font face with zero third-party dependencies.
///
/// Non-owning view over font bytes: the caller must keep the memory buffer passed to `load(...)`
/// alive for the lifetime of this `FontFace`.
class FontFace {
public:
    FontFace() noexcept = default;

    /// Parses SFNT font tables from non-owning memory buffer.
    bool load(keel::ArrayRef<keel::u8> data) noexcept;

    /// Convenience overload taking raw pointer and byte size.
    bool load(const keel::u8* data, keel::usize size) noexcept;

    [[nodiscard]] bool is_valid() const noexcept { return m_valid; }
    [[nodiscard]] bool is_cff() const noexcept { return m_isCff; }
    [[nodiscard]] keel::u16 units_per_em() const noexcept { return m_unitsPerEm; }
    [[nodiscard]] keel::u16 num_glyphs() const noexcept { return m_numGlyphs; }
    [[nodiscard]] keel::i16 ascender() const noexcept { return m_ascender; }
    [[nodiscard]] keel::i16 descender() const noexcept { return m_descender; }
    [[nodiscard]] keel::i16 cap_height() const noexcept { return m_capHeight; }
    [[nodiscard]] keel::i16 x_height() const noexcept { return m_xHeight; }
    [[nodiscard]] bool has_kerning() const noexcept { return m_kernOffset != 0u || !m_gposKernLookups.empty(); }

    /// Maps a Unicode codepoint to a font glyph index (`0` = `.notdef`).
    [[nodiscard]] keel::u32 glyph_index(keel::u32 codepoint) const noexcept;

    /// Returns horizontal kerning adjustment in font units between two glyph indices.
    [[nodiscard]] keel::i32 kerning_units(keel::u32 leftGlyph, keel::u32 rightGlyph) const noexcept;

    /// Returns unscaled horizontal advance width in font units for `glyphIndex`.
    [[nodiscard]] keel::u16 advance_width_units(keel::u32 glyphIndex) const noexcept;

    /// Rasterizes `glyphIndex` at `fontSizePx` into `out`.
    bool rasterize(
        keel::u32 glyphIndex,
        keel::f32 fontSizePx,
        const RasterOptions& opts,
        GlyphBitmap& out
    ) const;

private:
    struct TableRecord {
        keel::u32 offset{0};
        keel::u32 length{0};
    };

    struct CffIndex {
        keel::u32 count{0};
        keel::u8 offSize{0};
        keel::u32 offsetsStart{0};
        keel::u32 dataStart{0}; ///< 1-based origin (actual offset = dataStart + off - 1)
        keel::u32 endOffset{0};
    };

    struct CffFdInfo {
        keel::u32 privateOffset{0};
        keel::u32 privateSize{0};
        CffIndex localSubrs{};
    };

    struct OutlineBuilder {
        struct Segment {
            keel::f32 x0{0.0f};
            keel::f32 y0{0.0f};
            keel::f32 x1{0.0f};
            keel::f32 y1{0.0f};
        };

        keel::Array<Segment> segments;
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
        keel::f32 scale{1.0f};
        keel::f32 xHeightUnits{0.0f};
        keel::f32 capHeightUnits{0.0f};
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

    [[nodiscard]] bool parse_cmap(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_gpos(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_cff(keel::u32 offset, keel::u32 length) noexcept;
    [[nodiscard]] bool parse_cff_index(keel::usize off, CffIndex& out) const noexcept;
    [[nodiscard]] bool get_cff_index_item(const CffIndex& idx, keel::u32 i, keel::u32& outOff, keel::u32& outLen) const noexcept;
    [[nodiscard]] bool parse_cff_private_dict(keel::u32 privOff, keel::u32 privSize, CffFdInfo& outFd) const noexcept;

    [[nodiscard]] bool get_glyph_byte_range(keel::u32 glyphIndex, keel::u32& outOff, keel::u32& outLen) const noexcept;
    [[nodiscard]] bool extract_ttf_glyph_outline(
        keel::u32 glyphIndex,
        keel::f32 m00, keel::f32 m01, keel::f32 m10, keel::f32 m11,
        keel::f32 tx, keel::f32 ty,
        const Transform2D& xf,
        keel::f32 flatness,
        keel::u32 depth,
        OutlineBuilder& out,
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

    keel::u16 m_unitsPerEm{1000u};
    keel::u16 m_numGlyphs{0u};
    keel::u16 m_numHMetrics{0u};
    keel::i16 m_indexToLocFormat{0};
    keel::i16 m_ascender{800};
    keel::i16 m_descender{-200};
    keel::i16 m_capHeight{700};
    keel::i16 m_xHeight{500};

    keel::u32 m_hmtxOffset{0u};
    keel::u32 m_locaOffset{0u};
    keel::u32 m_locaLength{0u};
    keel::u32 m_glyfOffset{0u};
    keel::u32 m_glyfLength{0u};
    keel::u32 m_kernOffset{0u};
    keel::u32 m_kernLength{0u};

    keel::u32 m_cmapSubtableOffset{0u};
    keel::u16 m_cmapFormat{0u};

    keel::Array<keel::u32> m_gposKernLookups;

    keel::u32 m_cffOffset{0u};
    keel::u32 m_cffLength{0u};
    CffIndex m_cffCharStrings{};
    CffIndex m_cffGlobalSubrs{};
    CffFdInfo m_cffDefaultFd{};
    keel::Array<CffFdInfo> m_cffFdArray;
    keel::u32 m_cffFdSelectOffset{0u};
};

} // namespace keel::font
