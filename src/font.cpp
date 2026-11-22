#include "keel/font.h"
#include "keel/math.h"
#include <cstring>

namespace keel::font {

namespace {

[[nodiscard]] constexpr keel::u32 popcount8(keel::u32 v) noexcept {
    keel::u32 c = 0u;
    v &= 0xFFu;
    while (v != 0u) {
        c += (v & 1u);
        v >>= 1u;
    }
    return c;
}

[[nodiscard]] constexpr keel::i32 cff_subr_bias(keel::u32 count) noexcept {
    if (count < 1240u) return 107;
    if (count < 33900u) return 1131;
    return 32768;
}

[[nodiscard]] constexpr bool is_near_int(keel::f32 v, keel::f32 eps = 0.08f) noexcept {
    return keel::abs(v - keel::round(v)) <= eps;
}

void append_utf8(keel::SmallString<64>& dst, keel::u32 cp) noexcept {
    if (cp == 0u) return;
    if (cp < 0x80u) {
        if (dst.size() + 1u < 64u) dst.push_back(keel::as<char>(cp));
    } else if (cp < 0x800u) {
        if (dst.size() + 2u < 64u) {
            dst.push_back(keel::as<char>(0xC0u | (cp >> 6u)));
            dst.push_back(keel::as<char>(0x80u | (cp & 0x3Fu)));
        }
    } else if (cp < 0x10000u) {
        if (dst.size() + 3u < 64u) {
            dst.push_back(keel::as<char>(0xE0u | (cp >> 12u)));
            dst.push_back(keel::as<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            dst.push_back(keel::as<char>(0x80u | (cp & 0x3Fu)));
        }
    } else if (cp <= 0x10FFFFu) {
        if (dst.size() + 4u < 64u) {
            dst.push_back(keel::as<char>(0xF0u | (cp >> 18u)));
            dst.push_back(keel::as<char>(0x80u | ((cp >> 12u) & 0x3Fu)));
            dst.push_back(keel::as<char>(0x80u | ((cp >> 6u) & 0x3Fu)));
            dst.push_back(keel::as<char>(0x80u | (cp & 0x3Fu)));
        }
    }
}

// ==============================================================================
// Exact Analytical Signed-Area Scanline Rasterizer
// ==============================================================================

void rasterize_edge(
    keel::f32* deltaBuf,
    keel::u32 width,
    keel::u32 height,
    keel::u32 stride,
    keel::f32 x0,
    keel::f32 y0,
    keel::f32 x1,
    keel::f32 y1
) noexcept {
    if (y0 == y1) {
        return;
    }

    keel::f32 dir = 1.0f;
    if (y0 > y1) {
        const keel::f32 tx = x0; x0 = x1; x1 = tx;
        const keel::f32 ty = y0; y0 = y1; y1 = ty;
        dir = -1.0f;
    }

    const keel::f32 fH = keel::as<keel::f32>(height);
    if (y1 <= 0.0f || y0 >= fH) {
        return;
    }

    const keel::f32 dxDy = (x1 - x0) / (y1 - y0);
    const keel::f32 fW = keel::as<keel::f32>(width);

    const keel::i32 rowStart = keel::max(0, keel::floor_i32(y0));
    const keel::i32 rowEnd = keel::min(keel::as<keel::i32>(height) - 1, keel::ceil_i32(y1) - 1);

    for (keel::i32 row = rowStart; row <= rowEnd; ++row) {
        const keel::f32 fRow = keel::as<keel::f32>(row);
        const keel::f32 sy0 = keel::max(y0, fRow);
        const keel::f32 sy1 = keel::min(y1, fRow + 1.0f);
        if (sy1 <= sy0) {
            continue;
        }

        const keel::f32 dy = (sy1 - sy0) * dir;
        keel::f32 sx0 = x0 + (sy0 - y0) * dxDy;
        keel::f32 sx1 = x0 + (sy1 - y0) * dxDy;
        if (sx0 > sx1) {
            const keel::f32 tmp = sx0; sx0 = sx1; sx1 = tmp;
        }

        keel::f32* rowBuf = deltaBuf + keel::as<keel::usize>(row) * keel::as<keel::usize>(stride);

        if (sx1 <= 0.0f) {
            rowBuf[0] += dy;
            continue;
        }
        if (sx0 >= fW) {
            continue;
        }

        const keel::i32 col0 = keel::floor_i32(sx0);
        const keel::i32 col1 = keel::floor_i32(sx1);

        if (col0 == col1) {
            if (col0 >= 0 && col0 < keel::as<keel::i32>(width)) {
                const keel::f32 mid = 0.5f * (sx0 + sx1) - keel::as<keel::f32>(col0);
                const keel::f32 area = dy * (1.0f - mid);
                rowBuf[col0] += area;
                rowBuf[col0 + 1] += dy - area;
            }
            continue;
        }

        const keel::f32 invSpanX = 1.0f / (sx1 - sx0);
        if (sx0 < 0.0f) {
            rowBuf[0] += dy * ((0.0f - sx0) * invSpanX);
        }

        const keel::i32 cStart = keel::max(0, col0);
        const keel::i32 cEnd = keel::min(keel::as<keel::i32>(width) - 1, col1);
        for (keel::i32 c = cStart; c <= cEnd; ++c) {
            const keel::f32 fc = keel::as<keel::f32>(c);
            const keel::f32 cx0 = keel::max(sx0, fc);
            const keel::f32 cx1 = keel::min(sx1, fc + 1.0f);
            if (cx1 <= cx0) {
                continue;
            }
            const keel::f32 partDy = dy * ((cx1 - cx0) * invSpanX);
            const keel::f32 mid = 0.5f * (cx0 + cx1) - fc;
            const keel::f32 area = partDy * (1.0f - mid);
            rowBuf[c] += area;
            rowBuf[c + 1] += partDy - area;
        }
    }
}

void apply_synthetic_bold_dilation(
    keel::Array<keel::f32>& cov,
    keel::Array<keel::f32>& rowCopy,
    keel::u32 width,
    keel::u32 height,
    keel::f32 radiusX,
    keel::f32 radiusY
) {
    if (width == 0u || height == 0u) {
        return;
    }

    if (radiusX > 0.01f) {
        const keel::i32 intR = keel::floor_i32(radiusX);
        const keel::f32 frac = radiusX - keel::as<keel::f32>(intR);
        rowCopy.resize(width);
        for (keel::u32 y = 0u; y < height; ++y) {
            keel::f32* row = cov.data() + keel::as<keel::usize>(y) * keel::as<keel::usize>(width);
            std::memcpy(rowCopy.data(), row, keel::as<keel::usize>(width) * sizeof(keel::f32));
            for (keel::u32 x = 0u; x < width; ++x) {
                keel::f32 m = rowCopy[x];
                const keel::i32 xi = keel::as<keel::i32>(x);
                for (keel::i32 k = 1; k <= intR && (xi - k) >= 0; ++k) {
                    const keel::f32 s = rowCopy[keel::as<keel::usize>(xi - k)];
                    if (s > m) m = s;
                }
                if (frac > 0.001f && (xi - intR - 1) >= 0) {
                    const keel::f32 tail = rowCopy[keel::as<keel::usize>(xi - intR - 1)] * frac;
                    if (tail > m) m = tail;
                }
                row[x] = m;
            }
        }
    }

    if (radiusY > 0.01f && height > 1u) {
        const keel::f32 fy = keel::clamp(radiusY, 0.0f, 1.0f);
        for (keel::u32 y = 0u; y + 1u < height; ++y) {
            keel::f32* dstRow = cov.data() + keel::as<keel::usize>(y) * keel::as<keel::usize>(width);
            const keel::f32* belowRow = cov.data() + keel::as<keel::usize>(y + 1u) * keel::as<keel::usize>(width);
            for (keel::u32 x = 0u; x < width; ++x) {
                const keel::f32 v = belowRow[x] * fy;
                if (v > dstRow[x]) {
                    dstRow[x] = v;
                }
            }
        }
    }
}

// ==============================================================================
// OpenType `gvar` IUP (Interpolated Untouched Points) Helper
// ==============================================================================

void apply_iup_axis(
    const keel::Array<keel::i32>& origCoord,
    keel::Array<keel::f32>& tupleDelta,
    const keel::Array<keel::u8>& touched,
    keel::u32 startIdx,
    keel::u32 endIdx
) noexcept {
    const keel::u32 count = endIdx - startIdx + 1u;
    if (count < 2u) return;

    keel::u32 firstTouched = 0xFFFFFFFFu;
    keel::u32 touchedCount = 0u;
    for (keel::u32 i = startIdx; i <= endIdx; ++i) {
        if (touched[i] != 0u) {
            if (firstTouched == 0xFFFFFFFFu) firstTouched = i;
            ++touchedCount;
        }
    }
    if (touchedCount == 0u || touchedCount == count) {
        return;
    }
    if (touchedCount == 1u) {
        const keel::f32 d = tupleDelta[firstTouched];
        for (keel::u32 i = startIdx; i <= endIdx; ++i) {
            tupleDelta[i] = d;
        }
        return;
    }

    keel::u32 pA = firstTouched;
    for (;;) {
        // Find next touched point wrapping around the contour
        keel::u32 pB = pA;
        for (keel::u32 step = 1u; step <= count; ++step) {
            const keel::u32 cand = startIdx + ((pA - startIdx + step) % count);
            if (touched[cand] != 0u) {
                pB = cand;
                break;
            }
        }

        const keel::f32 cA = keel::as<keel::f32>(origCoord[pA]);
        const keel::f32 cB = keel::as<keel::f32>(origCoord[pB]);
        const keel::f32 dA = tupleDelta[pA];
        const keel::f32 dB = tupleDelta[pB];
        const keel::f32 cMin = keel::min(cA, cB);
        const keel::f32 cMax = keel::max(cA, cB);

        for (keel::u32 step = 1u; step < count; ++step) {
            const keel::u32 q = startIdx + ((pA - startIdx + step) % count);
            if (q == pB) break;
            const keel::f32 cQ = keel::as<keel::f32>(origCoord[q]);
            if (cA == cB) {
                tupleDelta[q] = (dA == dB) ? dA : 0.0f;
            } else if (cQ <= cMin) {
                tupleDelta[q] = (cA < cB) ? dA : dB;
            } else if (cQ >= cMax) {
                tupleDelta[q] = (cA > cB) ? dA : dB;
            } else {
                const keel::f32 t = (cQ - cA) / (cB - cA);
                tupleDelta[q] = dA + t * (dB - dA);
            }
        }

        pA = pB;
        if (pA == firstTouched) break;
    }
}

} // namespace

// ==============================================================================
// RasterScratch
// ==============================================================================

void RasterScratch::release() noexcept {
    segments = {};
    deltaBuf = {};
    coverageBuf = {};
    boldRowScratch = {};
    endPts = {};
    flags = {};
    rawX = {};
    rawY = {};
    ptsX = {};
    ptsY = {};
    varDeltaX = {};
    varDeltaY = {};
    tupleDeltaX = {};
    tupleDeltaY = {};
    varTouched = {};
    varPointIndices = {};
}

// ==============================================================================
// OutlineBuilder — Vector Sink OR Adaptive Bezier Flattening
// ==============================================================================

void FontFace::OutlineBuilder::move_to(keel::f32 x, keel::f32 y) {
    close_contour();
    curX = x;
    curY = y;
    startX = x;
    startY = y;
    hasContour = true;
    if (sink && sink->move_to) {
        sink->move_to(sink->user, x, y);
    }
}

void FontFace::OutlineBuilder::line_to(keel::f32 x, keel::f32 y) {
    if (!hasContour) {
        move_to(x, y);
        return;
    }
    if (x != curX || y != curY) {
        if (sink && sink->line_to) {
            sink->line_to(sink->user, x, y);
        }
        if (segments) {
            segments->push_back(RasterScratch::Segment{curX, curY, x, y});
        }
        curX = x;
        curY = y;
    }
}

void FontFace::OutlineBuilder::quad_to(
    keel::f32 cx,
    keel::f32 cy,
    keel::f32 x,
    keel::f32 y,
    keel::f32 flatness
) {
    if (!hasContour) {
        move_to(x, y);
        return;
    }
    if (sink) {
        if (sink->quad_to) {
            sink->quad_to(sink->user, cx, cy, x, y);
            curX = x;
            curY = y;
            return;
        }
    }
    const keel::f32 x0 = curX;
    const keel::f32 y0 = curY;
    const keel::f32 ddx = 0.25f * (x0 - 2.0f * cx + x);
    const keel::f32 ddy = 0.25f * (y0 - 2.0f * cy + y);
    const keel::f32 err = keel::sqrt(ddx * ddx + ddy * ddy);
    const keel::i32 steps = keel::clamp(
        keel::ceil_i32(keel::sqrt(err / keel::max(flatness, 0.02f))),
        1,
        32
    );
    if (steps <= 1) {
        line_to(x, y);
        return;
    }
    const keel::f32 invN = 1.0f / keel::as<keel::f32>(steps);
    for (keel::i32 i = 1; i < steps; ++i) {
        const keel::f32 t = keel::as<keel::f32>(i) * invN;
        const keel::f32 u = 1.0f - t;
        const keel::f32 px = u * u * x0 + 2.0f * u * t * cx + t * t * x;
        const keel::f32 py = u * u * y0 + 2.0f * u * t * cy + t * t * y;
        line_to(px, py);
    }
    line_to(x, y);
}

void FontFace::OutlineBuilder::cubic_to(
    keel::f32 c1x,
    keel::f32 c1y,
    keel::f32 c2x,
    keel::f32 c2y,
    keel::f32 x,
    keel::f32 y,
    keel::f32 flatness
) {
    if (!hasContour) {
        move_to(x, y);
        return;
    }
    if (sink) {
        if (sink->cubic_to) {
            sink->cubic_to(sink->user, c1x, c1y, c2x, c2y, x, y);
            curX = x;
            curY = y;
            return;
        }
    }
    const keel::f32 x0 = curX;
    const keel::f32 y0 = curY;
    const keel::f32 ax = x0 - 2.0f * c1x + c2x;
    const keel::f32 ay = y0 - 2.0f * c1y + c2y;
    const keel::f32 bx = c1x - 2.0f * c2x + x;
    const keel::f32 by = c1y - 2.0f * c2y + y;
    const keel::f32 maxD = keel::max(keel::sqrt(ax * ax + ay * ay), keel::sqrt(bx * bx + by * by));
    const keel::i32 steps = keel::clamp(
        keel::ceil_i32(keel::sqrt(0.75f * maxD / keel::max(flatness, 0.02f))),
        1,
        48
    );
    if (steps <= 1) {
        line_to(x, y);
        return;
    }
    const keel::f32 invN = 1.0f / keel::as<keel::f32>(steps);
    for (keel::i32 i = 1; i < steps; ++i) {
        const keel::f32 t = keel::as<keel::f32>(i) * invN;
        const keel::f32 u = 1.0f - t;
        const keel::f32 u2 = u * u;
        const keel::f32 t2 = t * t;
        const keel::f32 px = u2 * u * x0 + 3.0f * u2 * t * c1x + 3.0f * u * t2 * c2x + t2 * t * x;
        const keel::f32 py = u2 * u * y0 + 3.0f * u2 * t * c1y + 3.0f * u * t2 * c2y + t2 * t * y;
        line_to(px, py);
    }
    line_to(x, y);
}

void FontFace::OutlineBuilder::close_contour() {
    if (hasContour) {
        if (segments && (curX != startX || curY != startY)) {
            segments->push_back(RasterScratch::Segment{curX, curY, startX, startY});
            curX = startX;
            curY = startY;
        }
        if (sink && sink->close) {
            sink->close(sink->user);
        }
        hasContour = false;
    }
}

// ==============================================================================
// Vertical Blue-Zone Autohinting + Horizontal Stem Snapping
// ==============================================================================

keel::f32 FontFace::HintParams::map_y(keel::f32 fy) const noexcept {
    if (!enabled || xHeightUnits <= 0.0f || capHeightUnits <= xHeightUnits) {
        return fy * scale;
    }

    if (overshootClampUnits > 0.0f) {
        if (fy < 0.0f && fy >= -overshootClampUnits) {
            return 0.0f;
        }
        if (fy > xHeightUnits && fy <= xHeightUnits + overshootClampUnits) {
            return xHeightPx;
        }
        if (fy > capHeightUnits && fy <= capHeightUnits + overshootClampUnits) {
            return capHeightPx;
        }
        if (descenderUnits < 0.0f && fy < descenderUnits && fy >= descenderUnits - overshootClampUnits) {
            return descenderPx;
        }
    }

    if (fy < 0.0f) {
        if (descenderUnits < 0.0f && fy >= descenderUnits) {
            return fy * (descenderPx / descenderUnits);
        }
        if (descenderUnits < 0.0f) {
            return descenderPx + (fy - descenderUnits) * scale;
        }
        return fy * scale;
    }
    if (fy <= xHeightUnits) {
        return fy * (xHeightPx / xHeightUnits);
    }
    if (fy <= capHeightUnits) {
        return xHeightPx + (fy - xHeightUnits) * ((capHeightPx - xHeightPx) / (capHeightUnits - xHeightUnits));
    }
    return capHeightPx + (fy - capHeightUnits) * scale;
}

void FontFace::apply_horizontal_stem_snapping(
    keel::Array<keel::f32>& ptsX,
    keel::Array<keel::f32>& ptsY,
    const keel::Array<keel::u8>& flags,
    const keel::Array<keel::u16>& endPts,
    const HintParams& hint
) const noexcept {
    if (!hint.enabled || !hint.snapStems || ptsY.empty()) {
        return;
    }

    struct HEdge {
        keel::f32 y{0.0f};
        keel::f32 xMin{0.0f};
        keel::f32 xMax{0.0f};
        keel::i8 dir{0}; ///< +1 if dx > 0, -1 if dx < 0
    };

    keel::SmallArray<HEdge, 32> edges;
    const keel::u32 numPts = keel::as<keel::u32>(ptsY.size());
    keel::u32 startIdx = 0u;

    for (keel::usize c = 0u; c < endPts.size(); ++c) {
        const keel::u32 endIdx = endPts[c];
        if (endIdx >= numPts || endIdx < startIdx) break;
        const keel::u32 count = endIdx - startIdx + 1u;
        if (count < 2u) {
            startIdx = endIdx + 1u;
            continue;
        }
        for (keel::u32 i = 0u; i < count; ++i) {
            const keel::u32 a = startIdx + i;
            const keel::u32 b = startIdx + ((i + 1u) % count);
            if ((flags[a] & 0x01u) == 0u || (flags[b] & 0x01u) == 0u) continue;
            const keel::f32 dy = keel::abs(ptsY[b] - ptsY[a]);
            const keel::f32 dx = ptsX[b] - ptsX[a];
            if (dy <= 0.08f && keel::abs(dx) >= 1.1f) {
                const keel::f32 yAvg = 0.5f * (ptsY[a] + ptsY[b]);
                if (edges.size() < 32u) {
                    edges.push_back(HEdge{
                        .y = yAvg,
                        .xMin = keel::min(ptsX[a], ptsX[b]),
                        .xMax = keel::max(ptsX[a], ptsX[b]),
                        .dir = (dx > 0.0f) ? keel::as<keel::i8>(1) : keel::as<keel::i8>(-1)
                    });
                }
            }
        }
        startIdx = endIdx + 1u;
    }

    if (edges.size() < 2u) {
        return;
    }

    struct SnapRule {
        keel::f32 srcY{0.0f};
        keel::f32 dstY{0.0f};
    };
    keel::SmallArray<SnapRule, 16> rules;
    keel::StaticArray<keel::u8, 32> paired{};

    for (keel::usize i = 0u; i < edges.size(); ++i) {
        if (paired[i] != 0u) continue;
        keel::usize bestJ = edges.size();
        keel::f32 bestDist = 3.5f;
        for (keel::usize j = i + 1u; j < edges.size(); ++j) {
            if (paired[j] != 0u) continue;
            if (edges[i].dir == edges[j].dir) continue;
            const keel::f32 overlap = keel::min(edges[i].xMax, edges[j].xMax) - keel::max(edges[i].xMin, edges[j].xMin);
            if (overlap < 0.8f) continue;
            const keel::f32 dist = keel::abs(edges[i].y - edges[j].y);
            if (dist >= 0.72f && dist < bestDist) {
                bestDist = dist;
                bestJ = j;
            }
        }
        if (bestJ < edges.size()) {
            paired[i] = 1u;
            paired[bestJ] = 1u;
            const keel::f32 yTop = keel::min(edges[i].y, edges[bestJ].y);
            const keel::f32 yBot = keel::max(edges[i].y, edges[bestJ].y);
            // Skip stems whose edges are already aligned to blue-zone integer rows
            if (is_near_int(yTop) && is_near_int(yBot)) {
                continue;
            }
            const keel::f32 thick = yBot - yTop;
            const keel::f32 snappedThick = keel::max(1.0f, keel::round(thick));
            const keel::f32 mid = 0.5f * (yTop + yBot);
            keel::f32 newTop = keel::round(mid - 0.5f * snappedThick);
            keel::f32 newBot = newTop + snappedThick;
            // If one edge was already anchored on a blue zone (0, -xHeightPx, -capHeightPx), preserve that anchor
            if (is_near_int(yBot, 0.05f)) {
                newBot = keel::round(yBot);
                newTop = newBot - snappedThick;
            } else if (is_near_int(yTop, 0.05f)) {
                newTop = keel::round(yTop);
                newBot = newTop + snappedThick;
            }
            if (keel::abs(newTop - yTop) <= 0.65f && keel::abs(newBot - yBot) <= 0.65f) {
                if (rules.size() + 2u <= 16u) {
                    rules.push_back(SnapRule{yTop, newTop});
                    rules.push_back(SnapRule{yBot, newBot});
                }
            }
        }
    }

    if (rules.empty()) {
        return;
    }

    for (keel::usize p = 0u; p < ptsY.size(); ++p) {
        const keel::f32 y = ptsY[p];
        for (keel::usize r = 0u; r < rules.size(); ++r) {
            if (keel::abs(y - rules[r].srcY) <= 0.12f) {
                ptsY[p] = rules[r].dstY;
                break;
            }
        }
    }
}

// ==============================================================================
// Bounds-Checked Big-Endian Readers
// ==============================================================================

bool FontFace::read_u8(keel::usize off, keel::u8& out) const noexcept {
    if (off >= m_size) return false;
    out = m_data[off];
    return true;
}

bool FontFace::read_u16(keel::usize off, keel::u16& out) const noexcept {
    if (off + 2u > m_size) return false;
    out = keel::as<keel::u16>((keel::as<keel::u32>(m_data[off]) << 8u) | keel::as<keel::u32>(m_data[off + 1u]));
    return true;
}

bool FontFace::read_i16(keel::usize off, keel::i16& out) const noexcept {
    keel::u16 u = 0u;
    if (!read_u16(off, u)) return false;
    out = keel::as<keel::i16>(u);
    return true;
}

bool FontFace::read_u32(keel::usize off, keel::u32& out) const noexcept {
    if (off + 4u > m_size) return false;
    out = (keel::as<keel::u32>(m_data[off]) << 24u) |
          (keel::as<keel::u32>(m_data[off + 1u]) << 16u) |
          (keel::as<keel::u32>(m_data[off + 2u]) << 8u) |
          keel::as<keel::u32>(m_data[off + 3u]);
    return true;
}

bool FontFace::read_i32(keel::usize off, keel::i32& out) const noexcept {
    keel::u32 u = 0u;
    if (!read_u32(off, u)) return false;
    out = keel::as<keel::i32>(u);
    return true;
}

// ==============================================================================
// SFNT Loading (TTF / TTC Multi-Face / OTF) & Metadata (`name`, `OS/2`, `post`)
// ==============================================================================

keel::u32 FontFace::face_count(keel::ArrayRef<keel::u8> data) noexcept {
    return face_count(data.data(), data.size());
}

keel::u32 FontFace::face_count(const keel::u8* data, keel::usize size) noexcept {
    if (!data || size < 12u) return 0u;
    const keel::u32 magic =
        (keel::as<keel::u32>(data[0]) << 24u) |
        (keel::as<keel::u32>(data[1]) << 16u) |
        (keel::as<keel::u32>(data[2]) << 8u) |
        keel::as<keel::u32>(data[3]);
    if (magic == make_tag('t', 't', 'c', 'f')) {
        const keel::u32 numFonts =
            (keel::as<keel::u32>(data[8]) << 24u) |
            (keel::as<keel::u32>(data[9]) << 16u) |
            (keel::as<keel::u32>(data[10]) << 8u) |
            keel::as<keel::u32>(data[11]);
        return numFonts;
    }
    if (magic == 0x00010000u || magic == make_tag('t', 'r', 'u', 'e') || magic == make_tag('O', 'T', 'T', 'O')) {
        return 1u;
    }
    return 0u;
}

bool FontFace::load(keel::ArrayRef<keel::u8> data, keel::u32 faceIndex) noexcept {
    return load(data.data(), data.size(), faceIndex);
}

bool FontFace::load(const keel::u8* data, keel::usize size, keel::u32 faceIndex) noexcept {
    *this = FontFace{};
    if (!data || size < 12u) {
        return false;
    }
    m_data = data;
    m_size = size;

    keel::u32 sfntOffset = 0u;
    keel::u32 magic = 0u;
    if (!read_u32(0u, magic)) {
        return false;
    }

    if (magic == make_tag('t', 't', 'c', 'f')) {
        keel::u32 numFonts = 0u;
        if (!read_u32(8u, numFonts) || faceIndex >= numFonts) {
            return false;
        }
        if (!read_u32(12u + keel::as<keel::usize>(faceIndex) * 4u, sfntOffset)) {
            return false;
        }
        if (!read_u32(sfntOffset, magic)) {
            return false;
        }
    } else if (faceIndex != 0u) {
        return false;
    }

    const bool isTtf = (magic == 0x00010000u || magic == make_tag('t', 'r', 'u', 'e'));
    const bool isOtf = (magic == make_tag('O', 'T', 'T', 'O'));
    if (!isTtf && !isOtf) {
        return false;
    }

    keel::u16 numTables = 0u;
    if (!read_u16(keel::as<keel::usize>(sfntOffset) + 4u, numTables) || numTables == 0u) {
        return false;
    }

    TableRecord headTbl{}, maxpTbl{}, hheaTbl{}, hmtxTbl{}, cmapTbl{};
    TableRecord locaTbl{}, glyfTbl{}, cffTbl{}, os2Tbl{}, postTbl{}, nameTbl{};
    TableRecord kernTbl{}, gposTbl{}, fvarTbl{}, avarTbl{}, gvarTbl{}, hvarTbl{};

    const keel::usize dirStart = keel::as<keel::usize>(sfntOffset) + 12u;
    for (keel::u16 i = 0u; i < numTables; ++i) {
        const keel::usize recOff = dirStart + keel::as<keel::usize>(i) * 16u;
        keel::u32 tag = 0u, off = 0u, len = 0u;
        if (!read_u32(recOff, tag) || !read_u32(recOff + 8u, off) || !read_u32(recOff + 12u, len)) {
            return false;
        }
        if (keel::as<keel::usize>(off) + keel::as<keel::usize>(len) > m_size) {
            continue;
        }
        const TableRecord rec{off, len};
        if (tag == make_tag('h', 'e', 'a', 'd')) headTbl = rec;
        else if (tag == make_tag('m', 'a', 'x', 'p')) maxpTbl = rec;
        else if (tag == make_tag('h', 'h', 'e', 'a')) hheaTbl = rec;
        else if (tag == make_tag('h', 'm', 't', 'x')) hmtxTbl = rec;
        else if (tag == make_tag('c', 'm', 'a', 'p')) cmapTbl = rec;
        else if (tag == make_tag('l', 'o', 'c', 'a')) locaTbl = rec;
        else if (tag == make_tag('g', 'l', 'y', 'f')) glyfTbl = rec;
        else if (tag == make_tag('C', 'F', 'F', ' ')) cffTbl = rec;
        else if (tag == make_tag('O', 'S', '/', '2')) os2Tbl = rec;
        else if (tag == make_tag('p', 'o', 's', 't')) postTbl = rec;
        else if (tag == make_tag('n', 'a', 'm', 'e')) nameTbl = rec;
        else if (tag == make_tag('k', 'e', 'r', 'n')) kernTbl = rec;
        else if (tag == make_tag('G', 'P', 'O', 'S')) gposTbl = rec;
        else if (tag == make_tag('f', 'v', 'a', 'r')) fvarTbl = rec;
        else if (tag == make_tag('a', 'v', 'a', 'r')) avarTbl = rec;
        else if (tag == make_tag('g', 'v', 'a', 'r')) gvarTbl = rec;
        else if (tag == make_tag('H', 'V', 'A', 'R')) hvarTbl = rec;
    }

    if (headTbl.length < 54u || maxpTbl.length < 6u || hheaTbl.length < 36u || hmtxTbl.length < 4u || cmapTbl.length < 4u) {
        return false;
    }

    if (!read_u16(keel::as<keel::usize>(headTbl.offset) + 18u, m_unitsPerEm) || m_unitsPerEm == 0u) {
        return false;
    }
    keel::u16 macStyle = 0u;
    (void)read_u16(keel::as<keel::usize>(headTbl.offset) + 44u, macStyle);
    m_isItalic = (macStyle & 0x0002u) != 0u;
    if (!read_i16(keel::as<keel::usize>(headTbl.offset) + 50u, m_indexToLocFormat)) {
        return false;
    }
    if (!read_u16(keel::as<keel::usize>(maxpTbl.offset) + 4u, m_numGlyphs) || m_numGlyphs == 0u) {
        return false;
    }
    if (!read_i16(keel::as<keel::usize>(hheaTbl.offset) + 4u, m_ascender) ||
        !read_i16(keel::as<keel::usize>(hheaTbl.offset) + 6u, m_descender) ||
        !read_i16(keel::as<keel::usize>(hheaTbl.offset) + 8u, m_lineGap) ||
        !read_u16(keel::as<keel::usize>(hheaTbl.offset) + 34u, m_numHMetrics) ||
        m_numHMetrics == 0u) {
        return false;
    }

    m_hmtxOffset = hmtxTbl.offset;

    // Optional `OS/2` metadata
    m_xHeight = 0;
    m_capHeight = 0;
    if (os2Tbl.length >= 64u) {
        keel::u16 os2Version = 0u;
        (void)read_u16(os2Tbl.offset, os2Version);
        (void)read_u16(keel::as<keel::usize>(os2Tbl.offset) + 4u, m_weightClass);
        (void)read_i16(keel::as<keel::usize>(os2Tbl.offset) + 26u, m_strikeoutSize);
        (void)read_i16(keel::as<keel::usize>(os2Tbl.offset) + 28u, m_strikeoutPos);
        keel::u16 fsSel = 0u;
        if (read_u16(keel::as<keel::usize>(os2Tbl.offset) + 62u, fsSel)) {
            if ((fsSel & 0x0001u) != 0u) m_isItalic = true;
        }
        if (os2Version >= 2u && os2Tbl.length >= 90u) {
            (void)read_i16(keel::as<keel::usize>(os2Tbl.offset) + 86u, m_xHeight);
            (void)read_i16(keel::as<keel::usize>(os2Tbl.offset) + 88u, m_capHeight);
        }
    }

    // Optional `post` metadata (underline metrics & monospace flag)
    if (postTbl.length >= 16u) {
        (void)read_i16(keel::as<keel::usize>(postTbl.offset) + 8u, m_underlinePos);
        (void)read_i16(keel::as<keel::usize>(postTbl.offset) + 10u, m_underlineThickness);
        keel::u32 fixedPitch = 0u;
        if (read_u32(keel::as<keel::usize>(postTbl.offset) + 12u, fixedPitch)) {
            m_isMonospace = (fixedPitch != 0u);
        }
    }

    if (nameTbl.length >= 6u) {
        (void)parse_name(nameTbl.offset, nameTbl.length);
    }

    if (!parse_cmap(cmapTbl.offset, cmapTbl.length)) {
        return false;
    }

    if (cffTbl.length > 0u && (isOtf || glyfTbl.length == 0u)) {
        if (!parse_cff(cffTbl.offset, cffTbl.length)) {
            return false;
        }
        m_isCff = true;
    } else if (locaTbl.length > 0u && glyfTbl.length > 0u) {
        m_locaOffset = locaTbl.offset;
        m_locaLength = locaTbl.length;
        m_glyfOffset = glyfTbl.offset;
        m_glyfLength = glyfTbl.length;
        m_isCff = false;
    } else {
        return false;
    }

    if (kernTbl.length >= 4u) {
        m_kernOffset = kernTbl.offset;
        m_kernLength = kernTbl.length;
    }
    if (gposTbl.length >= 10u) {
        (void)parse_gpos(gposTbl.offset, gposTbl.length);
    }

    // Optional TrueType Variable Font tables (`fvar`, `avar`, `gvar`, `HVAR`)
    if (fvarTbl.length >= 16u && parse_fvar(fvarTbl.offset, fvarTbl.length)) {
        if (avarTbl.length >= 8u) {
            (void)parse_avar(avarTbl.offset, avarTbl.length);
        }
        if (gvarTbl.length >= 20u) {
            (void)parse_gvar(gvarTbl.offset, gvarTbl.length);
        }
        if (hvarTbl.length >= 20u) {
            (void)parse_hvar(hvarTbl.offset, hvarTbl.length);
        }
    }

    m_valid = true;

    // Populate O(1) ASCII glyph lookup cache
    for (keel::u32 cp = 0u; cp < 128u; ++cp) {
        m_asciiGlyphs[cp] = lookup_cmap_uncached(cp);
    }

    // Resolve blue-zone reference heights ('x', 'H', 'p')
    if (m_xHeight <= 0) {
        keel::i16 yMin = 0, yMax = 0;
        const keel::u32 gx = glyph_index(keel::as<keel::u32>('x'));
        if (gx != 0u) {
            compute_glyph_units_bbox(gx, yMin, yMax);
            if (yMax > 0) m_xHeight = yMax;
        }
    }
    if (m_capHeight <= 0) {
        keel::i16 yMin = 0, yMax = 0;
        const keel::u32 gH = glyph_index(keel::as<keel::u32>('H'));
        if (gH != 0u) {
            compute_glyph_units_bbox(gH, yMin, yMax);
            if (yMax > 0) m_capHeight = yMax;
        }
    }
    {
        keel::i16 yMin = 0, yMax = 0;
        const keel::u32 gp = glyph_index(keel::as<keel::u32>('p'));
        if (gp != 0u) {
            compute_glyph_units_bbox(gp, yMin, yMax);
            m_descenderBlue = (yMin < 0) ? yMin : m_descender;
        } else {
            m_descenderBlue = m_descender;
        }
    }
    if (m_capHeight <= 0) {
        m_capHeight = keel::as<keel::i16>(keel::round_i32(keel::as<keel::f32>(m_unitsPerEm) * 0.72f));
    }
    if (m_xHeight <= 0 || m_xHeight >= m_capHeight) {
        m_xHeight = keel::as<keel::i16>(keel::round_i32(keel::as<keel::f32>(m_capHeight) * 0.72f));
    }

    return true;
}

bool FontFace::parse_name(keel::u32 offset, keel::u32 length) noexcept {
    if (length < 6u) return false;
    keel::u16 count = 0u, strOff = 0u;
    if (!read_u16(keel::as<keel::usize>(offset) + 2u, count) ||
        !read_u16(keel::as<keel::usize>(offset) + 4u, strOff)) {
        return false;
    }
    const keel::usize storageBase = keel::as<keel::usize>(offset) + strOff;

    keel::i32 bestFamRank = -1;
    keel::i32 bestSubRank = -1;

    auto decodeRecord = [&](keel::u16 platId, keel::u16 recOff, keel::u16 recLen, keel::SmallString<64>& out) {
        out.clear();
        const keel::usize start = storageBase + recOff;
        if (start + recLen > m_size) return;
        if (platId == 3u || platId == 0u) {
            for (keel::u32 i = 0u; i + 1u < recLen; i += 2u) {
                keel::u16 cu = 0u;
                if (!read_u16(start + i, cu)) break;
                append_utf8(out, cu);
            }
        } else if (platId == 1u) {
            for (keel::u32 i = 0u; i < recLen; ++i) {
                keel::u8 ch = 0u;
                if (!read_u8(start + i, ch)) break;
                if (ch >= 0x20u && ch < 0x7Fu) {
                    append_utf8(out, ch);
                }
            }
        }
    };

    for (keel::u16 i = 0u; i < count; ++i) {
        const keel::usize r = keel::as<keel::usize>(offset) + 6u + keel::as<keel::usize>(i) * 12u;
        keel::u16 platId = 0u, encId = 0u, langId = 0u, nameId = 0u, recLen = 0u, recOff = 0u;
        if (!read_u16(r, platId) || !read_u16(r + 2u, encId) || !read_u16(r + 4u, langId) ||
            !read_u16(r + 6u, nameId) || !read_u16(r + 8u, recLen) || !read_u16(r + 10u, recOff)) {
            break;
        }
        if (recLen == 0u) continue;
        if (nameId != 1u && nameId != 2u && nameId != 16u && nameId != 17u) continue;

        keel::i32 rank = 0;
        if (platId == 3u && encId == 1u && langId == 0x0409u) rank = 100;
        else if (platId == 3u && encId == 1u) rank = 80;
        else if (platId == 0u) rank = 70;
        else if (platId == 1u && encId == 0u && langId == 0u) rank = 50;
        else continue;

        if (nameId == 16u || nameId == 17u) rank += 20; // Prefer typographic family/subfamily when present

        if ((nameId == 1u || nameId == 16u) && rank > bestFamRank) {
            bestFamRank = rank;
            decodeRecord(platId, recOff, recLen, m_familyName);
        } else if ((nameId == 2u || nameId == 17u) && rank > bestSubRank) {
            bestSubRank = rank;
            decodeRecord(platId, recOff, recLen, m_subfamilyName);
        }
    }
    return true;
}

// ==============================================================================
// Unicode cmap Parsing & Lookup (Format 12, 4, 6, 0) + O(1) ASCII LUT
// ==============================================================================

bool FontFace::parse_cmap(keel::u32 offset, keel::u32 length) noexcept {
    if (length < 4u) return false;
    keel::u16 numRecords = 0u;
    if (!read_u16(keel::as<keel::usize>(offset) + 2u, numRecords)) return false;

    keel::i32 bestRank = -1;
    keel::u32 bestSubOff = 0u;
    keel::u16 bestFmt = 0u;

    for (keel::u16 i = 0u; i < numRecords; ++i) {
        const keel::usize recOff = keel::as<keel::usize>(offset) + 4u + keel::as<keel::usize>(i) * 8u;
        keel::u16 platId = 0u, encId = 0u;
        keel::u32 subRel = 0u;
        if (!read_u16(recOff, platId) || !read_u16(recOff + 2u, encId) || !read_u32(recOff + 4u, subRel)) {
            break;
        }
        const keel::usize subAbs = keel::as<keel::usize>(offset) + keel::as<keel::usize>(subRel);
        keel::u16 fmt = 0u;
        if (!read_u16(subAbs, fmt)) continue;
        if (fmt != 12u && fmt != 4u && fmt != 6u && fmt != 0u) continue;

        keel::i32 rank = 0;
        if (fmt == 12u && ((platId == 3u && encId == 10u) || (platId == 0u && (encId == 4u || encId == 6u)))) {
            rank = 100;
        } else if (fmt == 4u && ((platId == 3u && encId == 1u) || (platId == 0u && encId == 3u))) {
            rank = 80;
        } else if (fmt == 12u) {
            rank = 70;
        } else if (fmt == 4u && (platId == 0u || platId == 3u)) {
            rank = 60;
        } else if (fmt == 6u && (platId == 0u || platId == 3u)) {
            rank = 30;
        } else if (fmt == 0u) {
            rank = 10;
        }

        if (rank > bestRank) {
            bestRank = rank;
            bestSubOff = keel::as<keel::u32>(subAbs);
            bestFmt = fmt;
        }
    }

    if (bestRank < 0) {
        return false;
    }
    m_cmapSubtableOffset = bestSubOff;
    m_cmapFormat = bestFmt;
    return true;
}

keel::u32 FontFace::glyph_index(keel::u32 codepoint) const noexcept {
    if (!m_valid) return 0u;
    if (codepoint < 128u) {
        return m_asciiGlyphs[codepoint];
    }
    return lookup_cmap_uncached(codepoint);
}

keel::u32 FontFace::lookup_cmap_uncached(keel::u32 codepoint) const noexcept {
    if (m_cmapSubtableOffset == 0u) {
        return 0u;
    }

    const keel::usize sub = keel::as<keel::usize>(m_cmapSubtableOffset);

    if (m_cmapFormat == 12u) {
        keel::u32 numGroups = 0u;
        if (!read_u32(sub + 12u, numGroups) || numGroups == 0u) return 0u;
        keel::u32 lo = 0u;
        keel::u32 hi = numGroups;
        while (lo < hi) {
            const keel::u32 mid = lo + ((hi - lo) >> 1u);
            const keel::usize gOff = sub + 16u + keel::as<keel::usize>(mid) * 12u;
            keel::u32 startCp = 0u, endCp = 0u, startGid = 0u;
            if (!read_u32(gOff, startCp) || !read_u32(gOff + 4u, endCp) || !read_u32(gOff + 8u, startGid)) {
                return 0u;
            }
            if (codepoint < startCp) {
                hi = mid;
            } else if (codepoint > endCp) {
                lo = mid + 1u;
            } else {
                const keel::u32 gid = startGid + (codepoint - startCp);
                return (gid < m_numGlyphs) ? gid : 0u;
            }
        }
        return 0u;
    }

    if (m_cmapFormat == 4u) {
        if (codepoint > 0xFFFFu) return 0u;
        keel::u16 segCountX2 = 0u;
        if (!read_u16(sub + 6u, segCountX2) || segCountX2 < 2u) return 0u;
        const keel::u32 segCount = keel::as<keel::u32>(segCountX2) >> 1u;
        const keel::usize endCodesOff = sub + 14u;
        const keel::usize startCodesOff = endCodesOff + segCountX2 + 2u;
        const keel::usize idDeltaOff = startCodesOff + segCountX2;
        const keel::usize idRangeOff = idDeltaOff + segCountX2;

        keel::u32 lo = 0u;
        keel::u32 hi = segCount;
        while (lo < hi) {
            const keel::u32 mid = lo + ((hi - lo) >> 1u);
            keel::u16 endC = 0u;
            if (!read_u16(endCodesOff + keel::as<keel::usize>(mid) * 2u, endC)) return 0u;
            if (codepoint > endC) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }
        if (lo >= segCount) return 0u;

        keel::u16 startC = 0u, endC = 0u, rangeOff = 0u;
        keel::i16 delta = 0;
        const keel::usize idx2 = keel::as<keel::usize>(lo) * 2u;
        if (!read_u16(startCodesOff + idx2, startC) ||
            !read_u16(endCodesOff + idx2, endC) ||
            !read_i16(idDeltaOff + idx2, delta) ||
            !read_u16(idRangeOff + idx2, rangeOff)) {
            return 0u;
        }
        if (codepoint < startC || codepoint > endC) return 0u;

        keel::u32 gid = 0u;
        if (rangeOff == 0u) {
            gid = keel::as<keel::u16>(keel::as<keel::i32>(codepoint) + keel::as<keel::i32>(delta));
        } else {
            const keel::usize glyphIdAddr = idRangeOff + idx2 + keel::as<keel::usize>(rangeOff) +
                                            keel::as<keel::usize>(codepoint - startC) * 2u;
            keel::u16 rawGid = 0u;
            if (!read_u16(glyphIdAddr, rawGid) || rawGid == 0u) return 0u;
            gid = keel::as<keel::u16>(keel::as<keel::i32>(rawGid) + keel::as<keel::i32>(delta));
        }
        return (gid < m_numGlyphs) ? gid : 0u;
    }

    if (m_cmapFormat == 6u) {
        if (codepoint > 0xFFFFu) return 0u;
        keel::u16 firstCode = 0u, entryCount = 0u;
        if (!read_u16(sub + 6u, firstCode) || !read_u16(sub + 8u, entryCount)) return 0u;
        if (codepoint < firstCode || codepoint >= keel::as<keel::u32>(firstCode) + keel::as<keel::u32>(entryCount)) {
            return 0u;
        }
        keel::u16 gid = 0u;
        if (!read_u16(sub + 10u + keel::as<keel::usize>(codepoint - firstCode) * 2u, gid)) return 0u;
        return (gid < m_numGlyphs) ? gid : 0u;
    }

    if (m_cmapFormat == 0u) {
        if (codepoint >= 256u) return 0u;
        keel::u8 gid = 0u;
        if (!read_u8(sub + 6u + codepoint, gid)) return 0u;
        return (gid < m_numGlyphs) ? gid : 0u;
    }

    return 0u;
}

// ==============================================================================
// Variable Fonts (`fvar`, `avar`, `gvar` with IUP, `HVAR`)
// ==============================================================================

bool FontFace::parse_fvar(keel::u32 offset, keel::u32 length) noexcept {
    if (length < 16u) return false;
    keel::u16 major = 0u, axesOff = 0u, axisCount = 0u, axisSize = 0u, instCount = 0u, instSize = 0u;
    if (!read_u16(offset, major) || major != 1u ||
        !read_u16(keel::as<keel::usize>(offset) + 4u, axesOff) ||
        !read_u16(keel::as<keel::usize>(offset) + 8u, axisCount) ||
        !read_u16(keel::as<keel::usize>(offset) + 10u, axisSize) ||
        !read_u16(keel::as<keel::usize>(offset) + 12u, instCount) ||
        !read_u16(keel::as<keel::usize>(offset) + 14u, instSize)) {
        return false;
    }
    if (axisCount == 0u || axisCount > 64u || axisSize < 20u) {
        return false;
    }

    m_axes.clear();
    m_userCoords.clear();
    m_normCoords.clear();
    m_namedInstances.clear();

    const keel::usize axesBase = keel::as<keel::usize>(offset) + axesOff;
    for (keel::u16 i = 0u; i < axisCount; ++i) {
        const keel::usize rec = axesBase + keel::as<keel::usize>(i) * axisSize;
        keel::u32 tag = 0u;
        keel::i32 minFx = 0, defFx = 0, maxFx = 0;
        keel::u16 flags = 0u, nameId = 0u;
        if (!read_u32(rec, tag) || !read_i32(rec + 4u, minFx) || !read_i32(rec + 8u, defFx) ||
            !read_i32(rec + 12u, maxFx) || !read_u16(rec + 16u, flags) || !read_u16(rec + 18u, nameId)) {
            m_axes.clear();
            m_userCoords.clear();
            m_normCoords.clear();
            return false;
        }
        VariationAxis ax{};
        ax.tag = tag;
        ax.minValue = keel::as<keel::f32>(minFx) / 65536.0f;
        ax.defaultValue = keel::as<keel::f32>(defFx) / 65536.0f;
        ax.maxValue = keel::as<keel::f32>(maxFx) / 65536.0f;
        ax.flags = flags;
        ax.nameId = nameId;
        m_axes.push_back(ax);
        m_userCoords.push_back(ax.defaultValue);
        m_normCoords.push_back(0.0f);
    }

    const keel::usize instBase = axesBase + keel::as<keel::usize>(axisCount) * axisSize;
    if (instCount > 0u && instSize >= 4u + axisCount * 4u) {
        for (keel::u16 i = 0u; i < instCount; ++i) {
            const keel::usize rec = instBase + keel::as<keel::usize>(i) * instSize;
            keel::u16 subId = 0u;
            if (!read_u16(rec, subId)) break;
            NamedInstance inst{};
            inst.subfamilyNameId = subId;
            bool ok = true;
            for (keel::u16 a = 0u; a < axisCount; ++a) {
                keel::i32 cFx = 0;
                if (!read_i32(rec + 4u + keel::as<keel::usize>(a) * 4u, cFx)) {
                    ok = false;
                    break;
                }
                inst.coords.push_back(keel::as<keel::f32>(cFx) / 65536.0f);
            }
            if (ok) {
                m_namedInstances.push_back(keel::move(inst));
            }
        }
    }

    m_hasActiveVariation = false;
    return true;
}

bool FontFace::parse_avar(keel::u32 offset, keel::u32 length) noexcept {
    if (length < 8u || m_axes.empty()) return false;
    keel::u16 major = 0u, axisCount = 0u;
    if (!read_u16(offset, major) || major != 1u ||
        !read_u16(keel::as<keel::usize>(offset) + 6u, axisCount) ||
        axisCount != m_axes.size()) {
        return false;
    }

    m_avarMaps.resize(axisCount);
    keel::usize cursor = keel::as<keel::usize>(offset) + 8u;
    for (keel::u16 a = 0u; a < axisCount; ++a) {
        keel::u16 mapCount = 0u;
        if (!read_u16(cursor, mapCount)) {
            m_avarMaps.clear();
            return false;
        }
        cursor += 2u;
        m_avarMaps[a].reserve(mapCount);
        for (keel::u16 m = 0u; m < mapCount; ++m) {
            keel::i16 fromC = 0, toC = 0;
            if (!read_i16(cursor, fromC) || !read_i16(cursor + 2u, toC)) {
                m_avarMaps.clear();
                return false;
            }
            cursor += 4u;
            m_avarMaps[a].push_back(AvarSegment{
                keel::as<keel::f32>(fromC) / 16384.0f,
                keel::as<keel::f32>(toC) / 16384.0f
            });
        }
    }
    return true;
}

bool FontFace::parse_gvar(keel::u32 offset, keel::u32 length) noexcept {
    if (length < 20u) return false;
    keel::u16 major = 0u;
    if (!read_u16(offset, major) || major != 1u ||
        !read_u16(keel::as<keel::usize>(offset) + 4u, m_gvarAxisCount) ||
        !read_u16(keel::as<keel::usize>(offset) + 6u, m_gvarSharedTupleCount) ||
        !read_u32(keel::as<keel::usize>(offset) + 8u, m_gvarSharedTuplesOffset) ||
        !read_u16(keel::as<keel::usize>(offset) + 12u, m_gvarGlyphCount) ||
        !read_u16(keel::as<keel::usize>(offset) + 14u, m_gvarFlags) ||
        !read_u32(keel::as<keel::usize>(offset) + 16u, m_gvarDataArrayOffset)) {
        return false;
    }
    if (m_gvarAxisCount != m_axes.size()) {
        m_gvarOffset = 0u;
        return false;
    }
    m_gvarOffset = offset;
    m_gvarLength = length;
    return true;
}

bool FontFace::parse_hvar(keel::u32 offset, keel::u32 length) noexcept {
    if (length < 20u) return false;
    keel::u16 major = 0u;
    keel::u32 ivsRel = 0u, advMapRel = 0u;
    if (!read_u16(offset, major) || major != 1u ||
        !read_u32(keel::as<keel::usize>(offset) + 4u, ivsRel) ||
        !read_u32(keel::as<keel::usize>(offset) + 8u, advMapRel) ||
        ivsRel == 0u) {
        return false;
    }
    m_hvarOffset = offset;
    m_hvarIvsOffset = offset + ivsRel;
    m_hvarAdvMapOffset = (advMapRel != 0u) ? (offset + advMapRel) : 0u;
    return true;
}

keel::f32 FontFace::normalize_axis_value(keel::usize axisIdx, keel::f32 userValue) const noexcept {
    const VariationAxis& ax = m_axes[axisIdx];
    const keel::f32 v = keel::clamp(userValue, ax.minValue, ax.maxValue);
    keel::f32 norm = 0.0f;
    if (v < ax.defaultValue && ax.defaultValue > ax.minValue) {
        norm = (v - ax.defaultValue) / (ax.defaultValue - ax.minValue);
    } else if (v > ax.defaultValue && ax.maxValue > ax.defaultValue) {
        norm = (v - ax.defaultValue) / (ax.maxValue - ax.defaultValue);
    }

    if (axisIdx < m_avarMaps.size() && m_avarMaps[axisIdx].size() >= 2u) {
        const auto& segs = m_avarMaps[axisIdx];
        if (norm <= segs[0].fromCoord) {
            return segs[0].toCoord;
        }
        const keel::usize last = segs.size() - 1u;
        if (norm >= segs[last].fromCoord) {
            return segs[last].toCoord;
        }
        for (keel::usize i = 0u; i + 1u < segs.size(); ++i) {
            if (norm >= segs[i].fromCoord && norm <= segs[i + 1u].fromCoord) {
                const keel::f32 span = segs[i + 1u].fromCoord - segs[i].fromCoord;
                if (span <= 1.0e-6f) return segs[i].toCoord;
                const keel::f32 t = (norm - segs[i].fromCoord) / span;
                return segs[i].toCoord + t * (segs[i + 1u].toCoord - segs[i].toCoord);
            }
        }
    }
    return keel::clamp(norm, -1.0f, 1.0f);
}

void FontFace::update_has_active_variation() noexcept {
    bool active = false;
    for (keel::usize i = 0u; i < m_normCoords.size(); ++i) {
        if (keel::abs(m_normCoords[i]) > 1.0e-5f) {
            active = true;
            break;
        }
    }
    m_hasActiveVariation = active;
}

bool FontFace::set_variation(keel::u32 axisTag, keel::f32 userValue) noexcept {
    for (keel::usize i = 0u; i < m_axes.size(); ++i) {
        if (m_axes[i].tag == axisTag) {
            m_userCoords[i] = keel::clamp(userValue, m_axes[i].minValue, m_axes[i].maxValue);
            m_normCoords[i] = normalize_axis_value(i, m_userCoords[i]);
            update_has_active_variation();
            return true;
        }
    }
    return false;
}

void FontFace::set_variations(keel::ArrayRef<VariationCoord> coords) noexcept {
    for (keel::usize i = 0u; i < coords.size(); ++i) {
        (void)set_variation(coords[i].tag, coords[i].value);
    }
}

bool FontFace::set_named_instance(keel::usize instanceIndex) noexcept {
    if (instanceIndex >= m_namedInstances.size()) return false;
    const auto& inst = m_namedInstances[instanceIndex];
    const keel::usize n = keel::min(inst.coords.size(), m_axes.size());
    for (keel::usize i = 0u; i < n; ++i) {
        m_userCoords[i] = keel::clamp(inst.coords[i], m_axes[i].minValue, m_axes[i].maxValue);
        m_normCoords[i] = normalize_axis_value(i, m_userCoords[i]);
    }
    update_has_active_variation();
    return true;
}

void FontFace::reset_variations() noexcept {
    for (keel::usize i = 0u; i < m_axes.size(); ++i) {
        m_userCoords[i] = m_axes[i].defaultValue;
        m_normCoords[i] = 0.0f;
    }
    m_hasActiveVariation = false;
}

keel::f32 FontFace::evaluate_item_variation_store(
    keel::u32 ivsOff,
    keel::u16 outerIdx,
    keel::u16 innerIdx
) const noexcept {
    if (ivsOff == 0u) return 0.0f;
    const keel::usize base = keel::as<keel::usize>(ivsOff);
    keel::u16 fmt = 0u, ivdCount = 0u;
    keel::u32 regListRel = 0u;
    if (!read_u16(base, fmt) || fmt != 1u ||
        !read_u32(base + 2u, regListRel) ||
        !read_u16(base + 6u, ivdCount) ||
        outerIdx >= ivdCount) {
        return 0.0f;
    }

    keel::u32 ivdRel = 0u;
    if (!read_u32(base + 8u + keel::as<keel::usize>(outerIdx) * 4u, ivdRel) || ivdRel == 0u) {
        return 0.0f;
    }
    const keel::usize ivdOff = base + ivdRel;
    keel::u16 itemCount = 0u, wordDeltaCount = 0u, regionIdxCount = 0u;
    if (!read_u16(ivdOff, itemCount) || !read_u16(ivdOff + 2u, wordDeltaCount) ||
        !read_u16(ivdOff + 4u, regionIdxCount) || innerIdx >= itemCount) {
        return 0.0f;
    }

    const bool longWords = (wordDeltaCount & 0x8000u) != 0u;
    const keel::u16 shortCount = wordDeltaCount & 0x7FFFu;
    if (shortCount > regionIdxCount) return 0.0f;

    const keel::usize regListBase = base + regListRel;
    keel::u16 regAxisCount = 0u, regCount = 0u;
    if (!read_u16(regListBase, regAxisCount) || !read_u16(regListBase + 2u, regCount) ||
        regAxisCount != m_normCoords.size()) {
        return 0.0f;
    }

    const keel::usize regionIdxsOff = ivdOff + 6u;
    const keel::usize deltaSetOff = regionIdxsOff + keel::as<keel::usize>(regionIdxCount) * 2u;
    const keel::usize rowBytes = longWords
        ? (keel::as<keel::usize>(shortCount) * 4u + keel::as<keel::usize>(regionIdxCount - shortCount) * 2u)
        : (keel::as<keel::usize>(shortCount) * 2u + keel::as<keel::usize>(regionIdxCount - shortCount));
    keel::usize rowPos = deltaSetOff + keel::as<keel::usize>(innerIdx) * rowBytes;

    keel::f32 totalDelta = 0.0f;
    for (keel::u16 r = 0u; r < regionIdxCount; ++r) {
        keel::i32 delta = 0;
        if (longWords) {
            if (r < shortCount) {
                if (!read_i32(rowPos, delta)) break;
                rowPos += 4u;
            } else {
                keel::i16 d16 = 0;
                if (!read_i16(rowPos, d16)) break;
                delta = d16;
                rowPos += 2u;
            }
        } else {
            if (r < shortCount) {
                keel::i16 d16 = 0;
                if (!read_i16(rowPos, d16)) break;
                delta = d16;
                rowPos += 2u;
            } else {
                keel::u8 d8 = 0u;
                if (!read_u8(rowPos++, d8)) break;
                delta = keel::as<keel::i8>(d8);
            }
        }
        if (delta == 0) continue;

        keel::u16 regIdx = 0u;
        if (!read_u16(regionIdxsOff + keel::as<keel::usize>(r) * 2u, regIdx) || regIdx >= regCount) continue;

        const keel::usize regOff = regListBase + 4u + keel::as<keel::usize>(regIdx) * keel::as<keel::usize>(regAxisCount) * 6u;
        keel::f32 scalar = 1.0f;
        for (keel::u16 a = 0u; a < regAxisCount; ++a) {
            keel::i16 s14 = 0, p14 = 0, e14 = 0;
            const keel::usize axOff = regOff + keel::as<keel::usize>(a) * 6u;
            if (!read_i16(axOff, s14) || !read_i16(axOff + 2u, p14) || !read_i16(axOff + 4u, e14)) {
                scalar = 0.0f;
                break;
            }
            const keel::f32 start = keel::as<keel::f32>(s14) / 16384.0f;
            const keel::f32 peak = keel::as<keel::f32>(p14) / 16384.0f;
            const keel::f32 end = keel::as<keel::f32>(e14) / 16384.0f;
            const keel::f32 v = m_normCoords[a];

            if (peak == 0.0f) continue;
            if (start > peak || peak > end || (start < 0.0f && end > 0.0f)) continue;
            if (v == peak) continue;
            if (v <= start || v >= end) {
                scalar = 0.0f;
                break;
            }
            if (v < peak) {
                scalar *= (v - start) / (peak - start);
            } else {
                scalar *= (end - v) / (end - peak);
            }
        }
        totalDelta += keel::as<keel::f32>(delta) * scalar;
    }
    return totalDelta;
}

keel::f32 FontFace::evaluate_hvar_advance_delta(keel::u32 glyphIndex) const noexcept {
    if (!m_hasActiveVariation || m_hvarIvsOffset == 0u) return 0.0f;

    keel::u16 outerIdx = 0u;
    keel::u16 innerIdx = keel::as<keel::u16>(glyphIndex);

    if (m_hvarAdvMapOffset != 0u) {
        const keel::usize mapOff = keel::as<keel::usize>(m_hvarAdvMapOffset);
        keel::u8 fmt = 0u, entryFmt = 0u;
        if (!read_u8(mapOff, fmt) || !read_u8(mapOff + 1u, entryFmt)) return 0.0f;

        keel::u32 mapCount = 0u;
        keel::usize entriesOff = 0u;
        if (fmt == 0u) {
            keel::u16 mc16 = 0u;
            if (!read_u16(mapOff + 2u, mc16)) return 0.0f;
            mapCount = mc16;
            entriesOff = mapOff + 4u;
        } else if (fmt == 1u) {
            if (!read_u32(mapOff + 2u, mapCount)) return 0.0f;
            entriesOff = mapOff + 6u;
        } else {
            return 0.0f;
        }
        if (mapCount == 0u) return 0.0f;

        const keel::u32 innerBits = keel::as<keel::u32>(entryFmt & 0x0Fu) + 1u;
        const keel::u32 entrySize = keel::as<keel::u32>((entryFmt >> 4u) & 0x03u) + 1u;
        const keel::u32 clampedGid = (glyphIndex < mapCount) ? glyphIndex : (mapCount - 1u);
        const keel::usize pos = entriesOff + keel::as<keel::usize>(clampedGid) * entrySize;
        keel::u32 val = 0u;
        for (keel::u32 b = 0u; b < entrySize; ++b) {
            keel::u8 byte = 0u;
            if (!read_u8(pos + b, byte)) return 0.0f;
            val = (val << 8u) | byte;
        }
        outerIdx = keel::as<keel::u16>(val >> innerBits);
        innerIdx = keel::as<keel::u16>(val & ((1u << innerBits) - 1u));
    }

    return evaluate_item_variation_store(m_hvarIvsOffset, outerIdx, innerIdx);
}

void FontFace::apply_gvar_deltas(
    keel::u32 glyphIndex,
    const keel::Array<keel::i32>& origX,
    const keel::Array<keel::i32>& origY,
    const keel::Array<keel::u16>& endPts,
    keel::u32 numPts,
    RasterScratch& scratch
) const noexcept {
    const keel::u32 totalPts = numPts + 4u; // + 4 phantom points
    scratch.varDeltaX.assign(totalPts, 0.0f);
    scratch.varDeltaY.assign(totalPts, 0.0f);

    if (!m_hasActiveVariation || m_gvarOffset == 0u || glyphIndex >= m_gvarGlyphCount) {
        return;
    }

    const keel::usize gvarBase = keel::as<keel::usize>(m_gvarOffset);
    keel::u32 d0 = 0u, d1 = 0u;
    if ((m_gvarFlags & 0x0001u) == 0u) {
        keel::u16 w0 = 0u, w1 = 0u;
        const keel::usize offPos = gvarBase + 20u + keel::as<keel::usize>(glyphIndex) * 2u;
        if (!read_u16(offPos, w0) || !read_u16(offPos + 2u, w1)) return;
        d0 = keel::as<keel::u32>(w0) * 2u;
        d1 = keel::as<keel::u32>(w1) * 2u;
    } else {
        const keel::usize offPos = gvarBase + 20u + keel::as<keel::usize>(glyphIndex) * 4u;
        if (!read_u32(offPos, d0) || !read_u32(offPos + 4u, d1)) return;
    }
    if (d1 <= d0) return;

    const keel::usize gDataStart = gvarBase + m_gvarDataArrayOffset + d0;
    const keel::usize gDataEnd = gvarBase + m_gvarDataArrayOffset + d1;
    if (gDataEnd > m_size || gDataStart + 4u > gDataEnd) return;

    keel::u16 tupleVarCountWord = 0u, serRel = 0u;
    if (!read_u16(gDataStart, tupleVarCountWord) || !read_u16(gDataStart + 2u, serRel)) return;

    const keel::u16 tupleCount = tupleVarCountWord & 0x0FFFu;
    const bool hasSharedPoints = (tupleVarCountWord & 0x8000u) != 0u;
    keel::usize hdrCursor = gDataStart + 4u;
    keel::usize serCursor = gDataStart + serRel;
    if (serCursor > gDataEnd) return;

    auto readPackedPointNumbers = [&](keel::usize& pos, keel::usize limit, keel::Array<keel::u32>& outPts, bool& outAll) -> bool {
        outPts.clear();
        outAll = false;
        if (pos >= limit) return false;
        keel::u8 b0 = 0u;
        if (!read_u8(pos++, b0)) return false;
        if (b0 == 0u) {
            outAll = true;
            return true;
        }
        keel::u32 count = b0;
        if ((b0 & 0x80u) != 0u) {
            keel::u8 b1 = 0u;
            if (pos >= limit || !read_u8(pos++, b1)) return false;
            count = (keel::as<keel::u32>(b0 & 0x7Fu) << 8u) | b1;
        }
        outPts.reserve(count);
        keel::u32 curPt = 0u;
        while (outPts.size() < count && pos < limit) {
            keel::u8 ctrl = 0u;
            if (!read_u8(pos++, ctrl)) return false;
            const bool is16 = (ctrl & 0x80u) != 0u;
            const keel::u32 run = keel::as<keel::u32>(ctrl & 0x7Fu) + 1u;
            for (keel::u32 r = 0u; r < run && outPts.size() < count; ++r) {
                if (is16) {
                    keel::u16 d16 = 0u;
                    if (pos + 2u > limit || !read_u16(pos, d16)) return false;
                    pos += 2u;
                    curPt += d16;
                } else {
                    keel::u8 d8 = 0u;
                    if (pos >= limit || !read_u8(pos++, d8)) return false;
                    curPt += d8;
                }
                outPts.push_back(curPt);
            }
        }
        return true;
    };

    auto readPackedDeltas = [&](keel::usize& pos, keel::usize limit, keel::u32 count, keel::Array<keel::f32>& dst, const keel::Array<keel::u32>& ptIdxs, bool allPts) -> bool {
        keel::u32 decoded = 0u;
        while (decoded < count && pos < limit) {
            keel::u8 ctrl = 0u;
            if (!read_u8(pos++, ctrl)) return false;
            const keel::u32 run = keel::as<keel::u32>(ctrl & 0x3Fu) + 1u;
            const bool isZero = (ctrl & 0x80u) != 0u;
            const bool isWord = (ctrl & 0x40u) != 0u;
            for (keel::u32 r = 0u; r < run && decoded < count; ++r) {
                keel::i32 val = 0;
                if (!isZero) {
                    if (isWord) {
                        keel::i16 v16 = 0;
                        if (pos + 2u > limit || !read_i16(pos, v16)) return false;
                        pos += 2u;
                        val = v16;
                    } else {
                        keel::u8 v8 = 0u;
                        if (pos >= limit || !read_u8(pos++, v8)) return false;
                        val = keel::as<keel::i8>(v8);
                    }
                }
                const keel::u32 targetIdx = allPts ? decoded : ptIdxs[decoded];
                if (targetIdx < totalPts) {
                    dst[targetIdx] = keel::as<keel::f32>(val);
                }
                ++decoded;
            }
        }
        return true;
    };

    keel::Array<keel::u32> sharedPts;
    bool sharedAll = true;
    if (hasSharedPoints) {
        if (!readPackedPointNumbers(serCursor, gDataEnd, sharedPts, sharedAll)) return;
    }

    const keel::usize axisCount = m_normCoords.size();
    keel::SmallArray<keel::f32, 8> peakTuple;
    keel::SmallArray<keel::f32, 8> startTuple;
    keel::SmallArray<keel::f32, 8> endTuple;

    for (keel::u16 t = 0u; t < tupleCount; ++t) {
        keel::u16 varDataSize = 0u, tupleIdxWord = 0u;
        if (hdrCursor + 4u > gDataEnd || !read_u16(hdrCursor, varDataSize) || !read_u16(hdrCursor + 2u, tupleIdxWord)) {
            break;
        }
        hdrCursor += 4u;

        const bool embeddedPeak = (tupleIdxWord & 0x8000u) != 0u;
        const bool hasInterm = (tupleIdxWord & 0x4000u) != 0u;
        const bool privatePts = (tupleIdxWord & 0x2000u) != 0u;
        const keel::u16 sharedIdx = tupleIdxWord & 0x0FFFu;

        peakTuple.clear();
        if (embeddedPeak) {
            for (keel::usize a = 0u; a < axisCount; ++a) {
                keel::i16 p14 = 0;
                if (!read_i16(hdrCursor, p14)) return;
                hdrCursor += 2u;
                peakTuple.push_back(keel::as<keel::f32>(p14) / 16384.0f);
            }
        } else {
            if (sharedIdx >= m_gvarSharedTupleCount) {
                serCursor += varDataSize;
                continue;
            }
            const keel::usize sOff = gvarBase + m_gvarSharedTuplesOffset + keel::as<keel::usize>(sharedIdx) * axisCount * 2u;
            for (keel::usize a = 0u; a < axisCount; ++a) {
                keel::i16 p14 = 0;
                if (!read_i16(sOff + a * 2u, p14)) return;
                peakTuple.push_back(keel::as<keel::f32>(p14) / 16384.0f);
            }
        }

        startTuple.clear();
        endTuple.clear();
        if (hasInterm) {
            for (keel::usize a = 0u; a < axisCount; ++a) {
                keel::i16 s14 = 0;
                if (!read_i16(hdrCursor, s14)) return;
                hdrCursor += 2u;
                startTuple.push_back(keel::as<keel::f32>(s14) / 16384.0f);
            }
            for (keel::usize a = 0u; a < axisCount; ++a) {
                keel::i16 e14 = 0;
                if (!read_i16(hdrCursor, e14)) return;
                hdrCursor += 2u;
                endTuple.push_back(keel::as<keel::f32>(e14) / 16384.0f);
            }
        }

        const keel::usize tupleSerStart = serCursor;
        const keel::usize tupleSerEnd = keel::min(serCursor + varDataSize, gDataEnd);
        serCursor = tupleSerEnd;

        // Compute tuple scalar weight
        keel::f32 scalar = 1.0f;
        for (keel::usize a = 0u; a < axisCount; ++a) {
            const keel::f32 p = peakTuple[a];
            const keel::f32 v = m_normCoords[a];
            if (p == 0.0f) continue;
            const keel::f32 start = hasInterm ? startTuple[a] : keel::min(0.0f, p);
            const keel::f32 end = hasInterm ? endTuple[a] : keel::max(0.0f, p);
            if (v == p) continue;
            if (v == 0.0f || v <= start || v >= end) {
                scalar = 0.0f;
                break;
            }
            if (v < p) {
                scalar *= (v - start) / (p - start);
            } else {
                scalar *= (end - v) / (end - p);
            }
        }
        if (scalar == 0.0f) continue;

        keel::usize pos = tupleSerStart;
        bool allPts = sharedAll;
        const keel::Array<keel::u32>* activePts = &sharedPts;
        if (privatePts) {
            if (!readPackedPointNumbers(pos, tupleSerEnd, scratch.varPointIndices, allPts)) continue;
            activePts = &scratch.varPointIndices;
        }

        const keel::u32 deltaCount = allPts ? totalPts : keel::as<keel::u32>(activePts->size());
        scratch.tupleDeltaX.assign(totalPts, 0.0f);
        scratch.tupleDeltaY.assign(totalPts, 0.0f);

        if (!readPackedDeltas(pos, tupleSerEnd, deltaCount, scratch.tupleDeltaX, *activePts, allPts)) continue;
        if (!readPackedDeltas(pos, tupleSerEnd, deltaCount, scratch.tupleDeltaY, *activePts, allPts)) continue;

        // Interpolate untouched contour points (IUP) when a sparse point set was supplied
        if (!allPts && numPts > 0u) {
            scratch.varTouched.assign(numPts, 0u);
            for (keel::usize i = 0u; i < activePts->size(); ++i) {
                const keel::u32 idx = (*activePts)[i];
                if (idx < numPts) scratch.varTouched[idx] = 1u;
            }
            keel::u32 startIdx = 0u;
            for (keel::usize c = 0u; c < endPts.size(); ++c) {
                const keel::u32 endIdx = endPts[c];
                if (endIdx >= numPts || endIdx < startIdx) break;
                apply_iup_axis(origX, scratch.tupleDeltaX, scratch.varTouched, startIdx, endIdx);
                apply_iup_axis(origY, scratch.tupleDeltaY, scratch.varTouched, startIdx, endIdx);
                startIdx = endIdx + 1u;
            }
        }

        for (keel::u32 i = 0u; i < totalPts; ++i) {
            scratch.varDeltaX[i] += scratch.tupleDeltaX[i] * scalar;
            scratch.varDeltaY[i] += scratch.tupleDeltaY[i] * scalar;
        }
    }
}

// ==============================================================================
// Unrasterized Metrics & Kerning
// ==============================================================================

keel::u16 FontFace::advance_width_units(keel::u32 glyphIndex) const noexcept {
    if (!m_valid || m_hmtxOffset == 0u || m_numHMetrics == 0u) {
        return m_unitsPerEm / 2u;
    }
    const keel::u32 clamped = (glyphIndex < m_numHMetrics) ? glyphIndex : (keel::as<keel::u32>(m_numHMetrics) - 1u);
    keel::u16 adv = 0u;
    if (!read_u16(keel::as<keel::usize>(m_hmtxOffset) + keel::as<keel::usize>(clamped) * 4u, adv)) {
        return m_unitsPerEm / 2u;
    }
    if (m_hasActiveVariation && m_hvarIvsOffset != 0u) {
        const keel::i32 adj = keel::as<keel::i32>(adv) + keel::round_i32(evaluate_hvar_advance_delta(glyphIndex));
        return keel::as<keel::u16>(keel::clamp(adj, 0, 65535));
    }
    return adv;
}

keel::i16 FontFace::left_side_bearing_units(keel::u32 glyphIndex) const noexcept {
    if (!m_valid || m_hmtxOffset == 0u || m_numHMetrics == 0u) {
        return 0;
    }
    keel::i16 lsb = 0;
    if (glyphIndex < m_numHMetrics) {
        (void)read_i16(keel::as<keel::usize>(m_hmtxOffset) + keel::as<keel::usize>(glyphIndex) * 4u + 2u, lsb);
    } else {
        const keel::usize tailOff = keel::as<keel::usize>(m_hmtxOffset) +
                                    keel::as<keel::usize>(m_numHMetrics) * 4u +
                                    keel::as<keel::usize>(glyphIndex - m_numHMetrics) * 2u;
        (void)read_i16(tailOff, lsb);
    }
    return lsb;
}

keel::f32 FontFace::advance_width_px(
    keel::u32 glyphIndex,
    keel::f32 fontSizePx,
    Hinting hinting,
    bool syntheticBold
) const noexcept {
    if (!m_valid || fontSizePx <= 0.0f || m_unitsPerEm == 0u) {
        return 0.0f;
    }
    const keel::f32 scale = fontSizePx / keel::as<keel::f32>(m_unitsPerEm);
    keel::f32 advPx = keel::as<keel::f32>(advance_width_units(glyphIndex)) * scale;
    if (hinting == Hinting::Normal) {
        advPx = keel::round(advPx);
    }
    if (syntheticBold) {
        advPx += 0.35f;
    }
    return advPx;
}

keel::f32 FontFace::kerning_px(
    keel::u32 leftGlyph,
    keel::u32 rightGlyph,
    keel::f32 fontSizePx,
    Hinting hinting
) const noexcept {
    if (!m_valid || fontSizePx <= 0.0f || m_unitsPerEm == 0u) {
        return 0.0f;
    }
    const keel::i32 units = kerning_units(leftGlyph, rightGlyph);
    if (units == 0) return 0.0f;
    const keel::f32 px = keel::as<keel::f32>(units) * (fontSizePx / keel::as<keel::f32>(m_unitsPerEm));
    return (hinting == Hinting::Normal) ? keel::round(px) : px;
}

bool FontFace::glyph_metrics(
    keel::u32 glyphIndex,
    keel::f32 fontSizePx,
    Hinting hinting,
    GlyphMetrics& out
) const {
    out = GlyphMetrics{};
    if (!m_valid || fontSizePx <= 0.0f || m_unitsPerEm == 0u) {
        return false;
    }
    const keel::f32 scale = fontSizePx / keel::as<keel::f32>(m_unitsPerEm);
    out.advancePx = advance_width_px(glyphIndex, fontSizePx, hinting, false);
    out.lsbPx = keel::as<keel::f32>(left_side_bearing_units(glyphIndex)) * scale;

    if (!m_isCff && !m_hasActiveVariation) {
        keel::u32 gOff = 0u, gLen = 0u;
        if (get_glyph_byte_range(glyphIndex, gOff, gLen) && gLen >= 10u) {
            keel::i16 x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            if (read_i16(keel::as<keel::usize>(gOff) + 2u, x0) &&
                read_i16(keel::as<keel::usize>(gOff) + 4u, y0) &&
                read_i16(keel::as<keel::usize>(gOff) + 6u, x1) &&
                read_i16(keel::as<keel::usize>(gOff) + 8u, y1)) {
                const Transform2D xf = make_transform(fontSizePx, RasterOptions{.hinting = hinting});
                out.minX = keel::as<keel::f32>(x0) * scale;
                out.maxX = keel::as<keel::f32>(x1) * scale;
                out.minY = -xf.transform_y_up(keel::as<keel::f32>(y1));
                out.maxY = -xf.transform_y_up(keel::as<keel::f32>(y0));
                out.empty = (out.maxX <= out.minX || out.maxY <= out.minY);
                return true;
            }
        }
        out.empty = true;
        return true;
    }

    // Variable TTF or CFF: compute exact bbox from outline segments
    RasterScratch localScratch;
    OutlineBuilder ob{};
    ob.segments = &localScratch.segments;
    const Transform2D xf = make_transform(fontSizePx, RasterOptions{.hinting = hinting});
    const bool ok = m_isCff
        ? extract_cff_glyph_outline(glyphIndex, xf, 0.5f, ob)
        : extract_ttf_glyph_outline(glyphIndex, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, xf, 0.5f, 0u, ob, localScratch);
    if (!ok || localScratch.segments.empty()) {
        out.empty = true;
        return true;
    }
    keel::f32 minX = 1.0e9f, maxX = -1.0e9f, minY = 1.0e9f, maxY = -1.0e9f;
    for (keel::usize i = 0u; i < localScratch.segments.size(); ++i) {
        const auto& s = localScratch.segments[i];
        minX = keel::min(minX, keel::min(s.x0, s.x1));
        maxX = keel::max(maxX, keel::max(s.x0, s.x1));
        minY = keel::min(minY, keel::min(s.y0, s.y1));
        maxY = keel::max(maxY, keel::max(s.y0, s.y1));
    }
    out.minX = minX;
    out.maxX = maxX;
    out.minY = minY;
    out.maxY = maxY;
    out.empty = (maxX <= minX || maxY <= minY);
    return true;
}

// ==============================================================================
// Kerning: Legacy `kern` (Format 0) + OpenType `GPOS` PairPos (Format 1 & 2)
// ==============================================================================

bool FontFace::parse_gpos(keel::u32 offset, keel::u32 length) noexcept {
    if (length < 10u) return false;
    keel::u16 featOff = 0u, lookupListOff = 0u;
    if (!read_u16(keel::as<keel::usize>(offset) + 6u, featOff) ||
        !read_u16(keel::as<keel::usize>(offset) + 8u, lookupListOff)) {
        return false;
    }

    const keel::usize featBase = keel::as<keel::usize>(offset) + featOff;
    const keel::usize lookupBase = keel::as<keel::usize>(offset) + lookupListOff;

    keel::u16 featCount = 0u, totalLookups = 0u;
    if (!read_u16(featBase, featCount) || !read_u16(lookupBase, totalLookups) || totalLookups == 0u) {
        return false;
    }

    keel::Array<keel::u16> kernLookupIndices;
    for (keel::u16 f = 0u; f < featCount; ++f) {
        const keel::usize recOff = featBase + 2u + keel::as<keel::usize>(f) * 6u;
        keel::u32 tag = 0u;
        keel::u16 fRel = 0u;
        if (!read_u32(recOff, tag) || !read_u16(recOff + 4u, fRel)) break;
        if (tag != make_tag('k', 'e', 'r', 'n')) continue;

        const keel::usize fTbl = featBase + fRel;
        keel::u16 lCount = 0u;
        if (!read_u16(fTbl + 2u, lCount)) continue;
        for (keel::u16 i = 0u; i < lCount; ++i) {
            keel::u16 lIdx = 0u;
            if (!read_u16(fTbl + 4u + keel::as<keel::usize>(i) * 2u, lIdx) || lIdx >= totalLookups) continue;
            bool exists = false;
            for (keel::usize j = 0u; j < kernLookupIndices.size(); ++j) {
                if (kernLookupIndices[j] == lIdx) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                kernLookupIndices.push_back(lIdx);
            }
        }
    }

    for (keel::usize i = 0u; i < kernLookupIndices.size(); ++i) {
        keel::u16 lRel = 0u;
        if (!read_u16(lookupBase + 2u + keel::as<keel::usize>(kernLookupIndices[i]) * 2u, lRel)) continue;
        const keel::usize lTbl = lookupBase + lRel;
        keel::u16 lType = 0u, subCount = 0u;
        if (!read_u16(lTbl, lType) || !read_u16(lTbl + 4u, subCount)) continue;

        for (keel::u16 s = 0u; s < subCount; ++s) {
            keel::u16 sRel = 0u;
            if (!read_u16(lTbl + 6u + keel::as<keel::usize>(s) * 2u, sRel)) continue;
            keel::usize subAbs = lTbl + sRel;
            keel::u16 effType = lType;

            if (effType == 9u) {
                keel::u16 extFmt = 0u, extType = 0u;
                keel::u32 extOff = 0u;
                if (!read_u16(subAbs, extFmt) || extFmt != 1u ||
                    !read_u16(subAbs + 2u, extType) ||
                    !read_u32(subAbs + 4u, extOff)) {
                    continue;
                }
                effType = extType;
                subAbs += extOff;
            }

            if (effType == 2u && subAbs < m_size) {
                m_gposKernLookups.push_back(keel::as<keel::u32>(subAbs));
            }
        }
    }

    return !m_gposKernLookups.empty();
}

keel::i32 FontFace::lookup_coverage_index(keel::u32 covOff, keel::u32 glyphId) const noexcept {
    const keel::usize base = keel::as<keel::usize>(covOff);
    keel::u16 fmt = 0u, count = 0u;
    if (!read_u16(base, fmt) || !read_u16(base + 2u, count) || count == 0u) return -1;

    if (fmt == 1u) {
        keel::u32 lo = 0u, hi = count;
        while (lo < hi) {
            const keel::u32 mid = lo + ((hi - lo) >> 1u);
            keel::u16 g = 0u;
            if (!read_u16(base + 4u + keel::as<keel::usize>(mid) * 2u, g)) return -1;
            if (glyphId < g) hi = mid;
            else if (glyphId > g) lo = mid + 1u;
            else return keel::as<keel::i32>(mid);
        }
        return -1;
    }

    if (fmt == 2u) {
        keel::u32 lo = 0u, hi = count;
        while (lo < hi) {
            const keel::u32 mid = lo + ((hi - lo) >> 1u);
            const keel::usize rOff = base + 4u + keel::as<keel::usize>(mid) * 6u;
            keel::u16 startG = 0u, endG = 0u, startCov = 0u;
            if (!read_u16(rOff, startG) || !read_u16(rOff + 2u, endG) || !read_u16(rOff + 4u, startCov)) return -1;
            if (glyphId < startG) hi = mid;
            else if (glyphId > endG) lo = mid + 1u;
            else return keel::as<keel::i32>(startCov) + keel::as<keel::i32>(glyphId - startG);
        }
    }

    return -1;
}

keel::u16 FontFace::lookup_class_def(keel::u32 classDefOff, keel::u32 glyphId) const noexcept {
    const keel::usize base = keel::as<keel::usize>(classDefOff);
    keel::u16 fmt = 0u;
    if (!read_u16(base, fmt)) return 0u;

    if (fmt == 1u) {
        keel::u16 startG = 0u, count = 0u;
        if (!read_u16(base + 2u, startG) || !read_u16(base + 4u, count)) return 0u;
        if (glyphId < startG || glyphId >= keel::as<keel::u32>(startG) + keel::as<keel::u32>(count)) return 0u;
        keel::u16 cls = 0u;
        (void)read_u16(base + 6u + keel::as<keel::usize>(glyphId - startG) * 2u, cls);
        return cls;
    }

    if (fmt == 2u) {
        keel::u16 count = 0u;
        if (!read_u16(base + 2u, count) || count == 0u) return 0u;
        keel::u32 lo = 0u, hi = count;
        while (lo < hi) {
            const keel::u32 mid = lo + ((hi - lo) >> 1u);
            const keel::usize rOff = base + 4u + keel::as<keel::usize>(mid) * 6u;
            keel::u16 startG = 0u, endG = 0u, cls = 0u;
            if (!read_u16(rOff, startG) || !read_u16(rOff + 2u, endG) || !read_u16(rOff + 4u, cls)) return 0u;
            if (glyphId < startG) hi = mid;
            else if (glyphId > endG) lo = mid + 1u;
            else return cls;
        }
    }

    return 0u;
}

keel::i32 FontFace::lookup_gpos_kern_subtable(
    keel::u32 subtableOff,
    keel::u32 leftGlyph,
    keel::u32 rightGlyph
) const noexcept {
    const keel::usize base = keel::as<keel::usize>(subtableOff);
    keel::u16 fmt = 0u, covRel = 0u, vf1 = 0u, vf2 = 0u;
    if (!read_u16(base, fmt) || !read_u16(base + 2u, covRel) ||
        !read_u16(base + 4u, vf1) || !read_u16(base + 6u, vf2)) {
        return 0;
    }
    if ((vf1 & 0x0004u) == 0u) {
        return 0;
    }

    const keel::i32 covIdx = lookup_coverage_index(keel::as<keel::u32>(base + covRel), leftGlyph);
    if (covIdx < 0) return 0;

    const keel::usize vr1Size = keel::as<keel::usize>(popcount8(vf1)) * 2u;
    const keel::usize vr2Size = keel::as<keel::usize>(popcount8(vf2)) * 2u;
    const keel::usize xAdvRel = keel::as<keel::usize>(popcount8(vf1 & 0x0003u)) * 2u;

    if (fmt == 1u) {
        keel::u16 pairSetCount = 0u;
        if (!read_u16(base + 8u, pairSetCount) || keel::as<keel::u32>(covIdx) >= pairSetCount) return 0;
        keel::u16 psRel = 0u;
        if (!read_u16(base + 10u + keel::as<keel::usize>(covIdx) * 2u, psRel)) return 0;
        const keel::usize psBase = base + psRel;
        keel::u16 pvCount = 0u;
        if (!read_u16(psBase, pvCount) || pvCount == 0u) return 0;

        const keel::usize recSize = 2u + vr1Size + vr2Size;
        keel::u32 lo = 0u, hi = pvCount;
        while (lo < hi) {
            const keel::u32 mid = lo + ((hi - lo) >> 1u);
            const keel::usize recOff = psBase + 2u + keel::as<keel::usize>(mid) * recSize;
            keel::u16 secondG = 0u;
            if (!read_u16(recOff, secondG)) return 0;
            if (rightGlyph < secondG) hi = mid;
            else if (rightGlyph > secondG) lo = mid + 1u;
            else {
                keel::i16 xAdv = 0;
                if (read_i16(recOff + 2u + xAdvRel, xAdv)) return xAdv;
                return 0;
            }
        }
        return 0;
    }

    if (fmt == 2u) {
        keel::u16 cd1Rel = 0u, cd2Rel = 0u, c1Count = 0u, c2Count = 0u;
        if (!read_u16(base + 8u, cd1Rel) || !read_u16(base + 10u, cd2Rel) ||
            !read_u16(base + 12u, c1Count) || !read_u16(base + 14u, c2Count)) {
            return 0;
        }
        const keel::u16 c1 = lookup_class_def(keel::as<keel::u32>(base + cd1Rel), leftGlyph);
        const keel::u16 c2 = lookup_class_def(keel::as<keel::u32>(base + cd2Rel), rightGlyph);
        if (c1 >= c1Count || c2 >= c2Count) return 0;

        const keel::usize cellSize = vr1Size + vr2Size;
        const keel::usize cellOff = base + 16u + (keel::as<keel::usize>(c1) * keel::as<keel::usize>(c2Count) + keel::as<keel::usize>(c2)) * cellSize;
        keel::i16 xAdv = 0;
        if (read_i16(cellOff + xAdvRel, xAdv)) {
            return xAdv;
        }
    }

    return 0;
}

keel::i32 FontFace::kerning_units(keel::u32 leftGlyph, keel::u32 rightGlyph) const noexcept {
    if (!m_valid || leftGlyph == 0u || rightGlyph == 0u) {
        return 0;
    }

    for (keel::usize i = 0u; i < m_gposKernLookups.size(); ++i) {
        const keel::i32 adj = lookup_gpos_kern_subtable(m_gposKernLookups[i], leftGlyph, rightGlyph);
        if (adj != 0) {
            return adj;
        }
    }

    if (m_kernOffset != 0u && m_kernLength >= 4u) {
        const keel::usize base = keel::as<keel::usize>(m_kernOffset);
        keel::u16 version = 0u, nTables = 0u;
        if (!read_u16(base, version) || !read_u16(base + 2u, nTables)) return 0;
        if (version != 0u) return 0;

        keel::usize subOff = base + 4u;
        const keel::usize kernEnd = base + keel::as<keel::usize>(m_kernLength);
        const keel::u32 needle = (leftGlyph << 16u) | (rightGlyph & 0xFFFFu);

        for (keel::u16 t = 0u; t < nTables && subOff + 6u <= kernEnd; ++t) {
            keel::u16 subLen = 0u, cov = 0u;
            if (!read_u16(subOff + 2u, subLen) || !read_u16(subOff + 4u, cov) || subLen < 6u) break;
            const keel::u16 fmt = keel::as<keel::u16>(cov >> 8u);
            const bool horizontal = (cov & 1u) != 0u;
            const bool crossStream = (cov & 4u) != 0u;
            if (fmt == 0u && horizontal && !crossStream && subOff + 14u <= kernEnd) {
                keel::u16 nPairs = 0u;
                if (read_u16(subOff + 6u, nPairs) && nPairs > 0u) {
                    const keel::usize pairsStart = subOff + 14u;
                    keel::u32 lo = 0u, hi = nPairs;
                    while (lo < hi) {
                        const keel::u32 mid = lo + ((hi - lo) >> 1u);
                        keel::u32 key = 0u;
                        if (!read_u32(pairsStart + keel::as<keel::usize>(mid) * 6u, key)) break;
                        if (needle < key) hi = mid;
                        else if (needle > key) lo = mid + 1u;
                        else {
                            keel::i16 val = 0;
                            if (read_i16(pairsStart + keel::as<keel::usize>(mid) * 6u + 4u, val)) {
                                return val;
                            }
                            return 0;
                        }
                    }
                }
            }
            subOff += subLen;
        }
    }

    return 0;
}

// ==============================================================================
// TrueType `loca` + `glyf` Outline Extraction (Simple + Composite + `gvar`)
// ==============================================================================

bool FontFace::get_glyph_byte_range(keel::u32 glyphIndex, keel::u32& outOff, keel::u32& outLen) const noexcept {
    if (glyphIndex >= m_numGlyphs || m_locaOffset == 0u || m_glyfOffset == 0u) {
        return false;
    }
    keel::u32 g0 = 0u, g1 = 0u;
    if (m_indexToLocFormat == 0) {
        keel::u16 w0 = 0u, w1 = 0u;
        const keel::usize pos = keel::as<keel::usize>(m_locaOffset) + keel::as<keel::usize>(glyphIndex) * 2u;
        if (!read_u16(pos, w0) || !read_u16(pos + 2u, w1)) return false;
        g0 = keel::as<keel::u32>(w0) * 2u;
        g1 = keel::as<keel::u32>(w1) * 2u;
    } else {
        const keel::usize pos = keel::as<keel::usize>(m_locaOffset) + keel::as<keel::usize>(glyphIndex) * 4u;
        if (!read_u32(pos, g0) || !read_u32(pos + 4u, g1)) return false;
    }
    if (g1 < g0 || g1 > m_glyfLength) {
        return false;
    }
    outOff = m_glyfOffset + g0;
    outLen = g1 - g0;
    return true;
}

bool FontFace::extract_ttf_glyph_outline(
    keel::u32 glyphIndex,
    keel::f32 m00,
    keel::f32 m01,
    keel::f32 m10,
    keel::f32 m11,
    keel::f32 tx,
    keel::f32 ty,
    const Transform2D& xf,
    keel::f32 flatness,
    keel::u32 depth,
    OutlineBuilder& out,
    RasterScratch& scratch,
    keel::Array<keel::f32>* outPointsX,
    keel::Array<keel::f32>* outPointsY
) const {
    if (depth > 16u) return false;
    keel::u32 gOff = 0u, gLen = 0u;
    if (!get_glyph_byte_range(glyphIndex, gOff, gLen)) return false;
    if (gLen == 0u) return true;
    if (gLen < 10u) return false;

    keel::i16 numContours = 0;
    if (!read_i16(gOff, numContours)) return false;

    if (numContours > 0) {
        const keel::usize endPtsOff = keel::as<keel::usize>(gOff) + 10u;
        keel::u16 lastEndPt = 0u;
        if (!read_u16(endPtsOff + keel::as<keel::usize>(numContours - 1) * 2u, lastEndPt)) return false;
        const keel::u32 numPts = keel::as<keel::u32>(lastEndPt) + 1u;
        if (numPts == 0u || numPts > 32768u) return false;

        scratch.endPts.resize(keel::as<keel::usize>(numContours));
        for (keel::i16 c = 0; c < numContours; ++c) {
            if (!read_u16(endPtsOff + keel::as<keel::usize>(c) * 2u, scratch.endPts[keel::as<keel::usize>(c)])) return false;
        }

        keel::usize cursor = endPtsOff + keel::as<keel::usize>(numContours) * 2u;
        keel::u16 instrLen = 0u;
        if (!read_u16(cursor, instrLen)) return false;
        cursor += 2u + keel::as<keel::usize>(instrLen);

        scratch.flags.resize(numPts);
        keel::u32 p = 0u;
        while (p < numPts) {
            keel::u8 fl = 0u;
            if (!read_u8(cursor++, fl)) return false;
            scratch.flags[p++] = fl;
            if ((fl & 0x08u) != 0u) {
                keel::u8 rep = 0u;
                if (!read_u8(cursor++, rep)) return false;
                for (keel::u32 r = 0u; r < rep && p < numPts; ++r) {
                    scratch.flags[p++] = fl;
                }
            }
        }

        scratch.rawX.resize(numPts);
        scratch.rawY.resize(numPts);

        keel::i32 curX = 0;
        for (keel::u32 i = 0u; i < numPts; ++i) {
            const keel::u8 fl = scratch.flags[i];
            if ((fl & 0x02u) != 0u) {
                keel::u8 dx = 0u;
                if (!read_u8(cursor++, dx)) return false;
                curX += ((fl & 0x10u) != 0u) ? keel::as<keel::i32>(dx) : -keel::as<keel::i32>(dx);
            } else if ((fl & 0x10u) == 0u) {
                keel::i16 dx = 0;
                if (!read_i16(cursor, dx)) return false;
                cursor += 2u;
                curX += dx;
            }
            scratch.rawX[i] = curX;
        }

        keel::i32 curY = 0;
        for (keel::u32 i = 0u; i < numPts; ++i) {
            const keel::u8 fl = scratch.flags[i];
            if ((fl & 0x04u) != 0u) {
                keel::u8 dy = 0u;
                if (!read_u8(cursor++, dy)) return false;
                curY += ((fl & 0x20u) != 0u) ? keel::as<keel::i32>(dy) : -keel::as<keel::i32>(dy);
            } else if ((fl & 0x20u) == 0u) {
                keel::i16 dy = 0;
                if (!read_i16(cursor, dy)) return false;
                cursor += 2u;
                curY += dy;
            }
            scratch.rawY[i] = curY;
        }

        const bool applyVar = m_hasActiveVariation && (m_gvarOffset != 0u);
        if (applyVar) {
            apply_gvar_deltas(glyphIndex, scratch.rawX, scratch.rawY, scratch.endPts, numPts, scratch);
        }

        scratch.ptsX.resize(numPts);
        scratch.ptsY.resize(numPts);
        for (keel::u32 i = 0u; i < numPts; ++i) {
            const keel::f32 ux = keel::as<keel::f32>(scratch.rawX[i]) + (applyVar ? scratch.varDeltaX[i] : 0.0f);
            const keel::f32 uy = keel::as<keel::f32>(scratch.rawY[i]) + (applyVar ? scratch.varDeltaY[i] : 0.0f);
            const keel::f32 fx = m00 * ux + m01 * uy + tx;
            const keel::f32 fy = m10 * ux + m11 * uy + ty;
            if (outPointsX && outPointsY) {
                outPointsX->push_back(fx);
                outPointsY->push_back(fy);
            }
            const keel::f32 pyUp = xf.transform_y_up(fy);
            scratch.ptsX[i] = xf.transform_x(fx, pyUp);
            scratch.ptsY[i] = -pyUp;
        }

        if (xf.hint.snapStems && depth == 0u) {
            apply_horizontal_stem_snapping(scratch.ptsX, scratch.ptsY, scratch.flags, scratch.endPts, xf.hint);
        }

        keel::u32 startIdx = 0u;
        for (keel::i16 c = 0; c < numContours; ++c) {
            const keel::u32 endIdx = scratch.endPts[keel::as<keel::usize>(c)];
            if (endIdx >= numPts || endIdx < startIdx) break;
            const keel::u32 count = endIdx - startIdx + 1u;
            if (count < 2u) {
                startIdx = endIdx + 1u;
                continue;
            }

            auto isOn = [&](keel::u32 idx) noexcept -> bool {
                return (scratch.flags[startIdx + (idx % count)] & 0x01u) != 0u;
            };
            auto px = [&](keel::u32 idx) noexcept -> keel::f32 {
                return scratch.ptsX[startIdx + (idx % count)];
            };
            auto py = [&](keel::u32 idx) noexcept -> keel::f32 {
                return scratch.ptsY[startIdx + (idx % count)];
            };

            keel::f32 sx = 0.0f, sy = 0.0f;
            keel::u32 firstLoopIdx = 0u;
            if (isOn(0u)) {
                sx = px(0u);
                sy = py(0u);
                firstLoopIdx = 1u;
            } else if (isOn(count - 1u)) {
                sx = px(count - 1u);
                sy = py(count - 1u);
                firstLoopIdx = 0u;
            } else {
                sx = 0.5f * (px(count - 1u) + px(0u));
                sy = 0.5f * (py(count - 1u) + py(0u));
                firstLoopIdx = 0u;
            }

            out.move_to(sx, sy);

            bool haveOff = false;
            keel::f32 offX = 0.0f, offY = 0.0f;
            const keel::u32 totalSteps = isOn(0u) ? (count - 1u) : (isOn(count - 1u) ? (count - 1u) : count);

            for (keel::u32 step = 0u; step < totalSteps; ++step) {
                const keel::u32 idx = firstLoopIdx + step;
                const keel::f32 cx = px(idx);
                const keel::f32 cy = py(idx);
                if (isOn(idx)) {
                    if (haveOff) {
                        out.quad_to(offX, offY, cx, cy, flatness);
                        haveOff = false;
                    } else {
                        out.line_to(cx, cy);
                    }
                } else {
                    if (haveOff) {
                        const keel::f32 mx = 0.5f * (offX + cx);
                        const keel::f32 my = 0.5f * (offY + cy);
                        out.quad_to(offX, offY, mx, my, flatness);
                    }
                    offX = cx;
                    offY = cy;
                    haveOff = true;
                }
            }

            if (haveOff) {
                out.quad_to(offX, offY, sx, sy, flatness);
            }
            out.close_contour();
            startIdx = endIdx + 1u;
        }
        return true;
    }

    if (numContours < 0) {
        keel::usize cursor = keel::as<keel::usize>(gOff) + 10u;
        keel::Array<keel::f32> parentPtsX;
        keel::Array<keel::f32> parentPtsY;

        constexpr keel::u16 kArg1And2AreWords = 0x0001u;
        constexpr keel::u16 kArgsAreXYValues = 0x0002u;
        constexpr keel::u16 kWeHaveAScale = 0x0008u;
        constexpr keel::u16 kMoreComponents = 0x0020u;
        constexpr keel::u16 kWeHaveAnXAndYScale = 0x0040u;
        constexpr keel::u16 kWeHaveATwoByTwo = 0x0080u;

        // First count components to support composite `gvar` component offset deltas when active
        struct CompDesc {
            keel::u16 flags{0u};
            keel::u16 gid{0u};
            keel::i32 arg1{0};
            keel::i32 arg2{0};
            keel::f32 cm00{1.0f}, cm01{0.0f}, cm10{0.0f}, cm11{1.0f};
        };
        keel::SmallArray<CompDesc, 8> comps;
        bool more = true;
        while (more && comps.size() < 64u) {
            CompDesc cd{};
            if (!read_u16(cursor, cd.flags) || !read_u16(cursor + 2u, cd.gid)) return false;
            cursor += 4u;

            if ((cd.flags & kArg1And2AreWords) != 0u) {
                if ((cd.flags & kArgsAreXYValues) != 0u) {
                    keel::i16 a1 = 0, a2 = 0;
                    if (!read_i16(cursor, a1) || !read_i16(cursor + 2u, a2)) return false;
                    cd.arg1 = a1; cd.arg2 = a2;
                } else {
                    keel::u16 a1 = 0u, a2 = 0u;
                    if (!read_u16(cursor, a1) || !read_u16(cursor + 2u, a2)) return false;
                    cd.arg1 = a1; cd.arg2 = a2;
                }
                cursor += 4u;
            } else {
                if ((cd.flags & kArgsAreXYValues) != 0u) {
                    keel::u8 b1 = 0u, b2 = 0u;
                    if (!read_u8(cursor, b1) || !read_u8(cursor + 1u, b2)) return false;
                    cd.arg1 = keel::as<keel::i8>(b1);
                    cd.arg2 = keel::as<keel::i8>(b2);
                } else {
                    keel::u8 b1 = 0u, b2 = 0u;
                    if (!read_u8(cursor, b1) || !read_u8(cursor + 1u, b2)) return false;
                    cd.arg1 = b1; cd.arg2 = b2;
                }
                cursor += 2u;
            }

            if ((cd.flags & kWeHaveAScale) != 0u) {
                keel::i16 s = 0;
                if (!read_i16(cursor, s)) return false;
                cursor += 2u;
                cd.cm00 = cd.cm11 = keel::as<keel::f32>(s) / 16384.0f;
            } else if ((cd.flags & kWeHaveAnXAndYScale) != 0u) {
                keel::i16 sx = 0, sy = 0;
                if (!read_i16(cursor, sx) || !read_i16(cursor + 2u, sy)) return false;
                cursor += 4u;
                cd.cm00 = keel::as<keel::f32>(sx) / 16384.0f;
                cd.cm11 = keel::as<keel::f32>(sy) / 16384.0f;
            } else if ((cd.flags & kWeHaveATwoByTwo) != 0u) {
                keel::i16 a = 0, b = 0, c = 0, d = 0;
                if (!read_i16(cursor, a) || !read_i16(cursor + 2u, b) ||
                    !read_i16(cursor + 4u, c) || !read_i16(cursor + 6u, d)) return false;
                cursor += 8u;
                cd.cm00 = keel::as<keel::f32>(a) / 16384.0f;
                cd.cm10 = keel::as<keel::f32>(b) / 16384.0f;
                cd.cm01 = keel::as<keel::f32>(c) / 16384.0f;
                cd.cm11 = keel::as<keel::f32>(d) / 16384.0f;
            }

            comps.push_back(cd);
            more = (cd.flags & kMoreComponents) != 0u;
        }

        // Composite gvar component offset deltas (if present)
        keel::SmallArray<keel::f32, 8> compDx;
        keel::SmallArray<keel::f32, 8> compDy;
        if (m_hasActiveVariation && m_gvarOffset != 0u && !comps.empty()) {
            keel::Array<keel::i32> zeroOrig(comps.size(), 0);
            keel::Array<keel::u16> emptyEndPts;
            apply_gvar_deltas(glyphIndex, zeroOrig, zeroOrig, emptyEndPts, keel::as<keel::u32>(comps.size()), scratch);
            for (keel::usize i = 0u; i < comps.size(); ++i) {
                compDx.push_back(scratch.varDeltaX[i]);
                compDy.push_back(scratch.varDeltaY[i]);
            }
        }

        for (keel::usize i = 0u; i < comps.size(); ++i) {
            const CompDesc& cd = comps[i];
            keel::f32 ctx = 0.0f, cty = 0.0f;
            if ((cd.flags & kArgsAreXYValues) != 0u) {
                ctx = keel::as<keel::f32>(cd.arg1) + ((i < compDx.size()) ? compDx[i] : 0.0f);
                cty = keel::as<keel::f32>(cd.arg2) + ((i < compDy.size()) ? compDy[i] : 0.0f);
            } else {
                keel::Array<keel::f32> compPtsX;
                keel::Array<keel::f32> compPtsY;
                OutlineBuilder dummy{};
                (void)extract_ttf_glyph_outline(cd.gid, cd.cm00, cd.cm01, cd.cm10, cd.cm11, 0.0f, 0.0f, xf, flatness, depth + 1u, dummy, scratch, &compPtsX, &compPtsY);
                if (cd.arg1 >= 0 && keel::as<keel::usize>(cd.arg1) < parentPtsX.size() &&
                    cd.arg2 >= 0 && keel::as<keel::usize>(cd.arg2) < compPtsX.size()) {
                    ctx = parentPtsX[keel::as<keel::usize>(cd.arg1)] - compPtsX[keel::as<keel::usize>(cd.arg2)];
                    cty = parentPtsY[keel::as<keel::usize>(cd.arg1)] - compPtsY[keel::as<keel::usize>(cd.arg2)];
                }
            }

            const keel::f32 nm00 = m00 * cd.cm00 + m01 * cd.cm10;
            const keel::f32 nm01 = m00 * cd.cm01 + m01 * cd.cm11;
            const keel::f32 nm10 = m10 * cd.cm00 + m11 * cd.cm10;
            const keel::f32 nm11 = m10 * cd.cm01 + m11 * cd.cm11;
            const keel::f32 ntx = m00 * ctx + m01 * cty + tx;
            const keel::f32 nty = m10 * ctx + m11 * cty + ty;

            (void)extract_ttf_glyph_outline(
                cd.gid, nm00, nm01, nm10, nm11, ntx, nty, xf, flatness, depth + 1u, out, scratch, &parentPtsX, &parentPtsY
            );
        }

        if (outPointsX && outPointsY) {
            for (keel::usize i = 0u; i < parentPtsX.size(); ++i) {
                outPointsX->push_back(parentPtsX[i]);
                outPointsY->push_back(parentPtsY[i]);
            }
        }
        return true;
    }

    return true;
}

// ==============================================================================
// OpenType CFF v1 Parser & Type 2 Charstring Interpreter (.otf)
// ==============================================================================

bool FontFace::parse_cff_index(keel::usize off, CffIndex& out) const noexcept {
    out = CffIndex{};
    keel::u16 count = 0u;
    if (!read_u16(off, count)) return false;
    out.count = count;
    if (count == 0u) {
        out.endOffset = keel::as<keel::u32>(off + 2u);
        return true;
    }
    keel::u8 offSize = 0u;
    if (!read_u8(off + 2u, offSize) || offSize < 1u || offSize > 4u) return false;
    out.offSize = offSize;
    out.offsetsStart = keel::as<keel::u32>(off + 3u);
    out.dataStart = keel::as<keel::u32>(off + 3u + keel::as<keel::usize>(count + 1u) * keel::as<keel::usize>(offSize));

    const keel::usize lastOffPos = keel::as<keel::usize>(out.offsetsStart) + keel::as<keel::usize>(count) * keel::as<keel::usize>(offSize);
    if (lastOffPos + offSize > m_size) return false;
    keel::u32 lastVal = 0u;
    for (keel::u8 b = 0u; b < offSize; ++b) {
        keel::u8 byte = 0u;
        if (!read_u8(lastOffPos + b, byte)) return false;
        lastVal = (lastVal << 8u) | byte;
    }
    if (lastVal < 1u) return false;
    const keel::usize endOff = keel::as<keel::usize>(out.dataStart) + keel::as<keel::usize>(lastVal - 1u);
    if (endOff > m_size) return false;
    out.endOffset = keel::as<keel::u32>(endOff);
    return true;
}

bool FontFace::get_cff_index_item(
    const CffIndex& idx,
    keel::u32 i,
    keel::u32& outOff,
    keel::u32& outLen
) const noexcept {
    if (i >= idx.count || idx.offSize < 1u || idx.offSize > 4u) return false;
    auto readOff = [&](keel::u32 k, keel::u32& val) noexcept -> bool {
        const keel::usize pos = keel::as<keel::usize>(idx.offsetsStart) + keel::as<keel::usize>(k) * keel::as<keel::usize>(idx.offSize);
        val = 0u;
        for (keel::u8 b = 0u; b < idx.offSize; ++b) {
            keel::u8 byte = 0u;
            if (!read_u8(pos + b, byte)) return false;
            val = (val << 8u) | byte;
        }
        return val >= 1u;
    };
    keel::u32 o0 = 0u, o1 = 0u;
    if (!readOff(i, o0) || !readOff(i + 1u, o1) || o1 < o0) return false;
    outOff = idx.dataStart + (o0 - 1u);
    outLen = o1 - o0;
    return (keel::as<keel::usize>(outOff) + keel::as<keel::usize>(outLen) <= m_size);
}

bool FontFace::parse_cff_private_dict(keel::u32 privOff, keel::u32 privSize, CffFdInfo& outFd) const noexcept {
    outFd.privateOffset = privOff;
    outFd.privateSize = privSize;
    outFd.localSubrs = CffIndex{};
    if (privSize == 0u || keel::as<keel::usize>(privOff) + keel::as<keel::usize>(privSize) > m_size) {
        return true;
    }

    keel::usize p = keel::as<keel::usize>(privOff);
    const keel::usize end = p + keel::as<keel::usize>(privSize);
    keel::StaticArray<keel::i32, 48> stack{};
    keel::usize sp = 0u;

    while (p < end) {
        keel::u8 b0 = 0u;
        if (!read_u8(p++, b0)) break;
        if (b0 <= 21u) {
            keel::u32 op = b0;
            if (b0 == 12u) {
                keel::u8 b1 = 0u;
                if (!read_u8(p++, b1)) break;
                op = 0x0C00u | b1;
            }
            if (op == 19u && sp >= 1u) {
                const keel::i32 rel = stack[sp - 1u];
                if (rel > 0) {
                    (void)parse_cff_index(keel::as<keel::usize>(privOff) + keel::as<keel::usize>(rel), outFd.localSubrs);
                }
            }
            sp = 0u;
        } else if (b0 == 28u) {
            keel::i16 v = 0;
            if (!read_i16(p, v)) break;
            p += 2u;
            if (sp < stack.size()) stack[sp++] = v;
        } else if (b0 == 29u) {
            keel::i32 v = 0;
            if (!read_i32(p, v)) break;
            p += 4u;
            if (sp < stack.size()) stack[sp++] = v;
        } else if (b0 == 30u) {
            while (p < end) {
                keel::u8 nb = 0u;
                if (!read_u8(p++, nb)) break;
                if ((nb >> 4u) == 0x0Fu || (nb & 0x0Fu) == 0x0Fu) break;
            }
            if (sp < stack.size()) stack[sp++] = 0;
        } else if (b0 >= 32u && b0 <= 246u) {
            if (sp < stack.size()) stack[sp++] = keel::as<keel::i32>(b0) - 139;
        } else if (b0 >= 247u && b0 <= 250u) {
            keel::u8 b1 = 0u;
            if (!read_u8(p++, b1)) break;
            if (sp < stack.size()) stack[sp++] = (keel::as<keel::i32>(b0) - 247) * 256 + keel::as<keel::i32>(b1) + 108;
        } else if (b0 >= 251u && b0 <= 254u) {
            keel::u8 b1 = 0u;
            if (!read_u8(p++, b1)) break;
            if (sp < stack.size()) stack[sp++] = -(keel::as<keel::i32>(b0) - 251) * 256 - keel::as<keel::i32>(b1) - 108;
        }
    }
    return true;
}

bool FontFace::parse_cff(keel::u32 offset, keel::u32 length) noexcept {
    if (length < 4u) return false;
    m_cffOffset = offset;
    m_cffLength = length;

    keel::u8 major = 0u, hdrSize = 0u;
    if (!read_u8(offset, major) || major != 1u || !read_u8(keel::as<keel::usize>(offset) + 2u, hdrSize) || hdrSize < 4u) {
        return false;
    }

    CffIndex nameIdx{}, topDictIdx{}, stringIdx{};
    if (!parse_cff_index(keel::as<keel::usize>(offset) + hdrSize, nameIdx)) return false;
    if (!parse_cff_index(nameIdx.endOffset, topDictIdx) || topDictIdx.count == 0u) return false;
    if (!parse_cff_index(topDictIdx.endOffset, stringIdx)) return false;
    if (!parse_cff_index(stringIdx.endOffset, m_cffGlobalSubrs)) return false;

    keel::u32 topOff = 0u, topLen = 0u;
    if (!get_cff_index_item(topDictIdx, 0u, topOff, topLen)) return false;

    keel::u32 charStringsRel = 0u;
    keel::u32 privSize = 0u, privRel = 0u;
    keel::u32 fdArrayRel = 0u, fdSelectRel = 0u;

    keel::usize p = topOff;
    const keel::usize end = keel::as<keel::usize>(topOff) + keel::as<keel::usize>(topLen);
    keel::StaticArray<keel::i32, 48> stack{};
    keel::usize sp = 0u;

    while (p < end) {
        keel::u8 b0 = 0u;
        if (!read_u8(p++, b0)) break;
        if (b0 <= 21u) {
            keel::u32 op = b0;
            if (b0 == 12u) {
                keel::u8 b1 = 0u;
                if (!read_u8(p++, b1)) break;
                op = 0x0C00u | b1;
            }
            if (op == 17u && sp >= 1u) {
                charStringsRel = keel::as<keel::u32>(stack[sp - 1u]);
            } else if (op == 18u && sp >= 2u) {
                privSize = keel::as<keel::u32>(stack[sp - 2u]);
                privRel = keel::as<keel::u32>(stack[sp - 1u]);
            } else if (op == 0x0C24u && sp >= 1u) {
                fdArrayRel = keel::as<keel::u32>(stack[sp - 1u]);
            } else if (op == 0x0C25u && sp >= 1u) {
                fdSelectRel = keel::as<keel::u32>(stack[sp - 1u]);
            }
            sp = 0u;
        } else if (b0 == 28u) {
            keel::i16 v = 0;
            if (!read_i16(p, v)) break;
            p += 2u;
            if (sp < stack.size()) stack[sp++] = v;
        } else if (b0 == 29u) {
            keel::i32 v = 0;
            if (!read_i32(p, v)) break;
            p += 4u;
            if (sp < stack.size()) stack[sp++] = v;
        } else if (b0 == 30u) {
            while (p < end) {
                keel::u8 nb = 0u;
                if (!read_u8(p++, nb)) break;
                if ((nb >> 4u) == 0x0Fu || (nb & 0x0Fu) == 0x0Fu) break;
            }
            if (sp < stack.size()) stack[sp++] = 0;
        } else if (b0 >= 32u && b0 <= 246u) {
            if (sp < stack.size()) stack[sp++] = keel::as<keel::i32>(b0) - 139;
        } else if (b0 >= 247u && b0 <= 250u) {
            keel::u8 b1 = 0u;
            if (!read_u8(p++, b1)) break;
            if (sp < stack.size()) stack[sp++] = (keel::as<keel::i32>(b0) - 247) * 256 + keel::as<keel::i32>(b1) + 108;
        } else if (b0 >= 251u && b0 <= 254u) {
            keel::u8 b1 = 0u;
            if (!read_u8(p++, b1)) break;
            if (sp < stack.size()) stack[sp++] = -(keel::as<keel::i32>(b0) - 251) * 256 - keel::as<keel::i32>(b1) - 108;
        }
    }

    if (charStringsRel == 0u) return false;
    if (!parse_cff_index(keel::as<keel::usize>(offset) + charStringsRel, m_cffCharStrings) || m_cffCharStrings.count == 0u) {
        return false;
    }

    if (privSize > 0u && privRel > 0u) {
        (void)parse_cff_private_dict(offset + privRel, privSize, m_cffDefaultFd);
    }

    if (fdArrayRel > 0u && fdSelectRel > 0u) {
        m_cffFdSelectOffset = offset + fdSelectRel;
        CffIndex fdIdx{};
        if (parse_cff_index(keel::as<keel::usize>(offset) + fdArrayRel, fdIdx)) {
            m_cffFdArray.resize(fdIdx.count);
            for (keel::u32 i = 0u; i < fdIdx.count; ++i) {
                keel::u32 fOff = 0u, fLen = 0u;
                if (!get_cff_index_item(fdIdx, i, fOff, fLen)) continue;
                keel::usize fp = fOff;
                const keel::usize fend = keel::as<keel::usize>(fOff) + keel::as<keel::usize>(fLen);
                keel::usize fsp = 0u;
                keel::u32 pSz = 0u, pRel = 0u;
                while (fp < fend) {
                    keel::u8 b0 = 0u;
                    if (!read_u8(fp++, b0)) break;
                    if (b0 <= 21u) {
                        keel::u32 op = b0;
                        if (b0 == 12u) {
                            keel::u8 b1 = 0u;
                            if (!read_u8(fp++, b1)) break;
                            op = 0x0C00u | b1;
                        }
                        if (op == 18u && fsp >= 2u) {
                            pSz = keel::as<keel::u32>(stack[fsp - 2u]);
                            pRel = keel::as<keel::u32>(stack[fsp - 1u]);
                        }
                        fsp = 0u;
                    } else if (b0 == 28u) {
                        keel::i16 v = 0; if (!read_i16(fp, v)) break; fp += 2u;
                        if (fsp < stack.size()) stack[fsp++] = v;
                    } else if (b0 == 29u) {
                        keel::i32 v = 0; if (!read_i32(fp, v)) break; fp += 4u;
                        if (fsp < stack.size()) stack[fsp++] = v;
                    } else if (b0 == 30u) {
                        while (fp < fend) {
                            keel::u8 nb = 0u; if (!read_u8(fp++, nb)) break;
                            if ((nb >> 4u) == 0x0Fu || (nb & 0x0Fu) == 0x0Fu) break;
                        }
                        if (fsp < stack.size()) stack[fsp++] = 0;
                    } else if (b0 >= 32u && b0 <= 246u) {
                        if (fsp < stack.size()) stack[fsp++] = keel::as<keel::i32>(b0) - 139;
                    } else if (b0 >= 247u && b0 <= 250u) {
                        keel::u8 b1 = 0u; if (!read_u8(fp++, b1)) break;
                        if (fsp < stack.size()) stack[fsp++] = (keel::as<keel::i32>(b0) - 247) * 256 + keel::as<keel::i32>(b1) + 108;
                    } else if (b0 >= 251u && b0 <= 254u) {
                        keel::u8 b1 = 0u; if (!read_u8(fp++, b1)) break;
                        if (fsp < stack.size()) stack[fsp++] = -(keel::as<keel::i32>(b0) - 251) * 256 - keel::as<keel::i32>(b1) - 108;
                    }
                }
                if (pSz > 0u && pRel > 0u) {
                    (void)parse_cff_private_dict(offset + pRel, pSz, m_cffFdArray[i]);
                }
            }
        }
    }

    return true;
}

bool FontFace::extract_cff_glyph_outline(
    keel::u32 glyphIndex,
    const Transform2D& xf,
    keel::f32 flatness,
    OutlineBuilder& out
) const {
    keel::u32 csOff = 0u, csLen = 0u;
    if (!get_cff_index_item(m_cffCharStrings, glyphIndex, csOff, csLen)) {
        return false;
    }

    const CffIndex* localSubrs = &m_cffDefaultFd.localSubrs;
    if (m_cffFdSelectOffset != 0u && !m_cffFdArray.empty()) {
        keel::u8 fmt = 0u;
        if (read_u8(m_cffFdSelectOffset, fmt)) {
            keel::u32 fdIdx = 0u;
            if (fmt == 0u) {
                keel::u8 v = 0u;
                if (read_u8(keel::as<keel::usize>(m_cffFdSelectOffset) + 1u + glyphIndex, v)) fdIdx = v;
            } else if (fmt == 3u) {
                keel::u16 nRanges = 0u;
                if (read_u16(keel::as<keel::usize>(m_cffFdSelectOffset) + 1u, nRanges)) {
                    for (keel::u16 r = 0u; r < nRanges; ++r) {
                        const keel::usize rOff = keel::as<keel::usize>(m_cffFdSelectOffset) + 3u + keel::as<keel::usize>(r) * 3u;
                        keel::u16 first = 0u, nextFirst = 0u;
                        keel::u8 fd = 0u;
                        if (!read_u16(rOff, first) || !read_u8(rOff + 2u, fd) || !read_u16(rOff + 3u, nextFirst)) break;
                        if (glyphIndex >= first && glyphIndex < nextFirst) {
                            fdIdx = fd;
                            break;
                        }
                    }
                }
            }
            if (fdIdx < m_cffFdArray.size()) {
                localSubrs = &m_cffFdArray[fdIdx].localSubrs;
            }
        }
    }

    struct CallFrame {
        keel::usize pos{0u};
        keel::usize end{0u};
    };

    keel::StaticArray<CallFrame, 12> callStack{};
    keel::usize callDepth = 0u;
    callStack[callDepth++] = CallFrame{csOff, keel::as<keel::usize>(csOff) + keel::as<keel::usize>(csLen)};

    keel::StaticArray<keel::f32, 64> stack{};
    keel::usize sp = 0u;
    bool hasWidth = false;
    keel::u32 stemCount = 0u;
    keel::f32 curX = 0.0f;
    keel::f32 curY = 0.0f;

    auto emitMove = [&](keel::f32 fx, keel::f32 fy) {
        const keel::f32 pyUp = xf.transform_y_up(fy);
        out.move_to(xf.transform_x(fx, pyUp), -pyUp);
    };
    auto emitLine = [&](keel::f32 fx, keel::f32 fy) {
        const keel::f32 pyUp = xf.transform_y_up(fy);
        out.line_to(xf.transform_x(fx, pyUp), -pyUp);
    };
    auto emitCubic = [&](keel::f32 x1, keel::f32 y1, keel::f32 x2, keel::f32 y2, keel::f32 x3, keel::f32 y3) {
        const keel::f32 py1 = xf.transform_y_up(y1);
        const keel::f32 py2 = xf.transform_y_up(y2);
        const keel::f32 py3 = xf.transform_y_up(y3);
        out.cubic_to(
            xf.transform_x(x1, py1), -py1,
            xf.transform_x(x2, py2), -py2,
            xf.transform_x(x3, py3), -py3,
            flatness
        );
    };

    auto dropLeadingWidth = [&]() {
        if (sp > 0u) {
            for (keel::usize i = 1u; i < sp; ++i) {
                stack[i - 1u] = stack[i];
            }
            --sp;
        }
        hasWidth = true;
    };

    keel::u32 safetySteps = 0u;
    while (callDepth > 0u && safetySteps < 65536u) {
        ++safetySteps;
        CallFrame& frame = callStack[callDepth - 1u];
        if (frame.pos >= frame.end) {
            --callDepth;
            continue;
        }

        keel::u8 b0 = 0u;
        if (!read_u8(frame.pos++, b0)) break;

        if (b0 == 28u) {
            keel::i16 v = 0;
            if (!read_i16(frame.pos, v)) break;
            frame.pos += 2u;
            if (sp < stack.size()) stack[sp++] = keel::as<keel::f32>(v);
            continue;
        }
        if (b0 >= 32u && b0 <= 246u) {
            if (sp < stack.size()) stack[sp++] = keel::as<keel::f32>(keel::as<keel::i32>(b0) - 139);
            continue;
        }
        if (b0 >= 247u && b0 <= 250u) {
            keel::u8 b1 = 0u;
            if (!read_u8(frame.pos++, b1)) break;
            if (sp < stack.size()) stack[sp++] = keel::as<keel::f32>((keel::as<keel::i32>(b0) - 247) * 256 + keel::as<keel::i32>(b1) + 108);
            continue;
        }
        if (b0 >= 251u && b0 <= 254u) {
            keel::u8 b1 = 0u;
            if (!read_u8(frame.pos++, b1)) break;
            if (sp < stack.size()) stack[sp++] = keel::as<keel::f32>(-(keel::as<keel::i32>(b0) - 251) * 256 - keel::as<keel::i32>(b1) - 108);
            continue;
        }
        if (b0 == 255u) {
            keel::i32 v = 0;
            if (!read_i32(frame.pos, v)) break;
            frame.pos += 4u;
            if (sp < stack.size()) stack[sp++] = keel::as<keel::f32>(v) / 65536.0f;
            continue;
        }

        if (b0 == 1u || b0 == 3u || b0 == 18u || b0 == 23u) {
            if (!hasWidth && (sp & 1u) != 0u) dropLeadingWidth();
            hasWidth = true;
            stemCount += keel::as<keel::u32>(sp / 2u);
            sp = 0u;
        } else if (b0 == 19u || b0 == 20u) {
            if (!hasWidth && (sp & 1u) != 0u) dropLeadingWidth();
            hasWidth = true;
            stemCount += keel::as<keel::u32>(sp / 2u);
            sp = 0u;
            frame.pos += (keel::as<keel::usize>(stemCount) + 7u) / 8u;
        } else if (b0 == 21u) {
            if (!hasWidth && sp > 2u) dropLeadingWidth();
            hasWidth = true;
            if (sp >= 2u) {
                curX += stack[0];
                curY += stack[1];
                emitMove(curX, curY);
            }
            sp = 0u;
        } else if (b0 == 22u) {
            if (!hasWidth && sp > 1u) dropLeadingWidth();
            hasWidth = true;
            if (sp >= 1u) {
                curX += stack[0];
                emitMove(curX, curY);
            }
            sp = 0u;
        } else if (b0 == 4u) {
            if (!hasWidth && sp > 1u) dropLeadingWidth();
            hasWidth = true;
            if (sp >= 1u) {
                curY += stack[0];
                emitMove(curX, curY);
            }
            sp = 0u;
        } else if (b0 == 5u) {
            for (keel::usize i = 0u; i + 1u < sp; i += 2u) {
                curX += stack[i];
                curY += stack[i + 1u];
                emitLine(curX, curY);
            }
            sp = 0u;
        } else if (b0 == 6u || b0 == 7u) {
            bool horiz = (b0 == 6u);
            for (keel::usize i = 0u; i < sp; ++i) {
                if (horiz) curX += stack[i];
                else curY += stack[i];
                emitLine(curX, curY);
                horiz = !horiz;
            }
            sp = 0u;
        } else if (b0 == 8u) {
            for (keel::usize i = 0u; i + 5u < sp; i += 6u) {
                const keel::f32 x1 = curX + stack[i];
                const keel::f32 y1 = curY + stack[i + 1u];
                const keel::f32 x2 = x1 + stack[i + 2u];
                const keel::f32 y2 = y1 + stack[i + 3u];
                const keel::f32 x3 = x2 + stack[i + 4u];
                const keel::f32 y3 = y2 + stack[i + 5u];
                emitCubic(x1, y1, x2, y2, x3, y3);
                curX = x3; curY = y3;
            }
            sp = 0u;
        } else if (b0 == 24u) {
            keel::usize i = 0u;
            for (; i + 7u < sp; i += 6u) {
                const keel::f32 x1 = curX + stack[i];
                const keel::f32 y1 = curY + stack[i + 1u];
                const keel::f32 x2 = x1 + stack[i + 2u];
                const keel::f32 y2 = y1 + stack[i + 3u];
                const keel::f32 x3 = x2 + stack[i + 4u];
                const keel::f32 y3 = y2 + stack[i + 5u];
                emitCubic(x1, y1, x2, y2, x3, y3);
                curX = x3; curY = y3;
            }
            if (i + 1u < sp) {
                curX += stack[i];
                curY += stack[i + 1u];
                emitLine(curX, curY);
            }
            sp = 0u;
        } else if (b0 == 25u) {
            keel::usize i = 0u;
            for (; i + 7u < sp; i += 2u) {
                curX += stack[i];
                curY += stack[i + 1u];
                emitLine(curX, curY);
            }
            if (i + 5u < sp) {
                const keel::f32 x1 = curX + stack[i];
                const keel::f32 y1 = curY + stack[i + 1u];
                const keel::f32 x2 = x1 + stack[i + 2u];
                const keel::f32 y2 = y1 + stack[i + 3u];
                const keel::f32 x3 = x2 + stack[i + 4u];
                const keel::f32 y3 = y2 + stack[i + 5u];
                emitCubic(x1, y1, x2, y2, x3, y3);
                curX = x3; curY = y3;
            }
            sp = 0u;
        } else if (b0 == 26u) {
            keel::usize i = 0u;
            keel::f32 dx1 = 0.0f;
            if ((sp & 1u) != 0u && sp >= 1u) dx1 = stack[i++];
            for (; i + 3u < sp; i += 4u) {
                const keel::f32 x1 = curX + dx1;
                const keel::f32 y1 = curY + stack[i];
                const keel::f32 x2 = x1 + stack[i + 1u];
                const keel::f32 y2 = y1 + stack[i + 2u];
                const keel::f32 x3 = x2;
                const keel::f32 y3 = y2 + stack[i + 3u];
                emitCubic(x1, y1, x2, y2, x3, y3);
                curX = x3; curY = y3;
                dx1 = 0.0f;
            }
            sp = 0u;
        } else if (b0 == 27u) {
            keel::usize i = 0u;
            keel::f32 dy1 = 0.0f;
            if ((sp & 1u) != 0u && sp >= 1u) dy1 = stack[i++];
            for (; i + 3u < sp; i += 4u) {
                const keel::f32 x1 = curX + stack[i];
                const keel::f32 y1 = curY + dy1;
                const keel::f32 x2 = x1 + stack[i + 1u];
                const keel::f32 y2 = y1 + stack[i + 2u];
                const keel::f32 x3 = x2 + stack[i + 3u];
                const keel::f32 y3 = y2;
                emitCubic(x1, y1, x2, y2, x3, y3);
                curX = x3; curY = y3;
                dy1 = 0.0f;
            }
            sp = 0u;
        } else if (b0 == 30u || b0 == 31u) {
            bool vertFirst = (b0 == 30u);
            keel::usize i = 0u;
            while (i + 3u < sp) {
                const keel::usize rem = sp - i;
                const keel::f32 extra = (rem == 5u) ? stack[i + 4u] : 0.0f;
                keel::f32 x1 = curX, y1 = curY, x2 = 0.0f, y2 = 0.0f, x3 = 0.0f, y3 = 0.0f;
                if (vertFirst) {
                    y1 += stack[i];
                    x2 = x1 + stack[i + 1u];
                    y2 = y1 + stack[i + 2u];
                    x3 = x2 + stack[i + 3u];
                    y3 = y2 + extra;
                } else {
                    x1 += stack[i];
                    x2 = x1 + stack[i + 1u];
                    y2 = y1 + stack[i + 2u];
                    x3 = x2 + extra;
                    y3 = y2 + stack[i + 3u];
                }
                emitCubic(x1, y1, x2, y2, x3, y3);
                curX = x3; curY = y3;
                i += (rem == 5u) ? 5u : 4u;
                vertFirst = !vertFirst;
            }
            sp = 0u;
        } else if (b0 == 10u || b0 == 29u) {
            if (sp >= 1u && callDepth < callStack.size()) {
                const CffIndex& tbl = (b0 == 10u) ? *localSubrs : m_cffGlobalSubrs;
                const keel::i32 idx = keel::round_i32(stack[--sp]) + cff_subr_bias(tbl.count);
                if (idx >= 0 && keel::as<keel::u32>(idx) < tbl.count) {
                    keel::u32 sOff = 0u, sLen = 0u;
                    if (get_cff_index_item(tbl, keel::as<keel::u32>(idx), sOff, sLen)) {
                        callStack[callDepth++] = CallFrame{sOff, keel::as<keel::usize>(sOff) + keel::as<keel::usize>(sLen)};
                    }
                }
            }
        } else if (b0 == 11u) {
            if (callDepth > 0u) --callDepth;
        } else if (b0 == 14u) {
            out.close_contour();
            break;
        } else if (b0 == 12u) {
            keel::u8 b1 = 0u;
            if (!read_u8(frame.pos++, b1)) break;
            if (b1 == 34u && sp >= 7u) {
                const keel::f32 x1 = curX + stack[0], y1 = curY;
                const keel::f32 x2 = x1 + stack[1], y2 = y1 + stack[2];
                const keel::f32 x3 = x2 + stack[3], y3 = y2;
                const keel::f32 x4 = x3 + stack[4], y4 = y3;
                const keel::f32 x5 = x4 + stack[5], y5 = curY;
                const keel::f32 x6 = x5 + stack[6], y6 = curY;
                emitCubic(x1, y1, x2, y2, x3, y3);
                emitCubic(x4, y4, x5, y5, x6, y6);
                curX = x6; curY = y6;
                sp = 0u;
            } else if (b1 == 35u && sp >= 13u) {
                const keel::f32 x1 = curX + stack[0], y1 = curY + stack[1];
                const keel::f32 x2 = x1 + stack[2], y2 = y1 + stack[3];
                const keel::f32 x3 = x2 + stack[4], y3 = y2 + stack[5];
                const keel::f32 x4 = x3 + stack[6], y4 = y3 + stack[7];
                const keel::f32 x5 = x4 + stack[8], y5 = y4 + stack[9];
                const keel::f32 x6 = x5 + stack[10], y6 = y5 + stack[11];
                emitCubic(x1, y1, x2, y2, x3, y3);
                emitCubic(x4, y4, x5, y5, x6, y6);
                curX = x6; curY = y6;
                sp = 0u;
            } else if (b1 == 36u && sp >= 9u) {
                const keel::f32 x1 = curX + stack[0], y1 = curY + stack[1];
                const keel::f32 x2 = x1 + stack[2], y2 = y1 + stack[3];
                const keel::f32 x3 = x2 + stack[4], y3 = y2;
                const keel::f32 x4 = x3 + stack[5], y4 = y3;
                const keel::f32 x5 = x4 + stack[6], y5 = y4 + stack[7];
                const keel::f32 x6 = x5 + stack[8], y6 = curY;
                emitCubic(x1, y1, x2, y2, x3, y3);
                emitCubic(x4, y4, x5, y5, x6, y6);
                curX = x6; curY = y6;
                sp = 0u;
            } else if (b1 == 37u && sp >= 11u) {
                const keel::f32 x1 = curX + stack[0], y1 = curY + stack[1];
                const keel::f32 x2 = x1 + stack[2], y2 = y1 + stack[3];
                const keel::f32 x3 = x2 + stack[4], y3 = y2 + stack[5];
                const keel::f32 x4 = x3 + stack[6], y4 = y3 + stack[7];
                const keel::f32 x5 = x4 + stack[8], y5 = y4 + stack[9];
                const keel::f32 dx = x5 - curX;
                const keel::f32 dy = y5 - curY;
                const keel::f32 x6 = (keel::abs(dx) > keel::abs(dy)) ? (x5 + stack[10]) : curX;
                const keel::f32 y6 = (keel::abs(dx) > keel::abs(dy)) ? curY : (y5 + stack[10]);
                emitCubic(x1, y1, x2, y2, x3, y3);
                emitCubic(x4, y4, x5, y5, x6, y6);
                curX = x6; curY = y6;
                sp = 0u;
            } else {
                sp = 0u;
            }
        } else {
            sp = 0u;
        }
    }

    out.close_contour();
    return true;
}

void FontFace::compute_glyph_units_bbox(keel::u32 glyphIndex, keel::i16& outYMin, keel::i16& outYMax) const noexcept {
    outYMin = 0;
    outYMax = 0;
    if (!m_isCff) {
        keel::u32 gOff = 0u, gLen = 0u;
        if (get_glyph_byte_range(glyphIndex, gOff, gLen) && gLen >= 10u) {
            (void)read_i16(keel::as<keel::usize>(gOff) + 4u, outYMin);
            (void)read_i16(keel::as<keel::usize>(gOff) + 8u, outYMax);
        }
        return;
    }

    Transform2D xf{};
    xf.scaleX = 1.0f;
    xf.hint.enabled = false;
    xf.hint.scale = 1.0f;
    keel::Array<RasterScratch::Segment> segs;
    OutlineBuilder ob{};
    ob.segments = &segs;
    if (extract_cff_glyph_outline(glyphIndex, xf, 2.0f, ob) && !segs.empty()) {
        keel::f32 minY = 1.0e9f;
        keel::f32 maxY = -1.0e9f;
        for (keel::usize i = 0u; i < segs.size(); ++i) {
            const keel::f32 y0 = -segs[i].y0;
            const keel::f32 y1 = -segs[i].y1;
            minY = keel::min(minY, keel::min(y0, y1));
            maxY = keel::max(maxY, keel::max(y0, y1));
        }
        outYMin = keel::as<keel::i16>(keel::round_i32(minY));
        outYMax = keel::as<keel::i16>(keel::round_i32(maxY));
    }
}

FontFace::Transform2D FontFace::make_transform(
    keel::f32 fontSizePx,
    const RasterOptions& opts
) const noexcept {
    const keel::f32 scale = fontSizePx / keel::as<keel::f32>(m_unitsPerEm);
    const keel::f32 xMul = opts.subpixel ? 3.0f : 1.0f;
    const keel::f32 phasePx = (opts.hinting != Hinting::Normal)
        ? (keel::as<keel::f32>(opts.subpixelPhase & 0x03u) * 0.25f)
        : 0.0f;

    Transform2D xf{};
    xf.scaleX = scale * xMul;
    xf.italicShear = opts.syntheticItalic ? (0.18f * xMul) : 0.0f;
    xf.shiftX = phasePx * xMul;
    xf.hint.scale = scale;
    xf.hint.enabled = (opts.hinting != Hinting::None) && (m_xHeight > 0) && (m_capHeight > m_xHeight);
    xf.hint.snapStems = xf.hint.enabled && (fontSizePx <= 32.0f);
    if (xf.hint.enabled) {
        xf.hint.descenderUnits = keel::as<keel::f32>(m_descenderBlue);
        xf.hint.xHeightUnits = keel::as<keel::f32>(m_xHeight);
        xf.hint.capHeightUnits = keel::as<keel::f32>(m_capHeight);
        xf.hint.descenderPx = (m_descenderBlue < 0) ? -keel::max(1.0f, keel::round(-xf.hint.descenderUnits * scale)) : 0.0f;
        xf.hint.xHeightPx = keel::max(1.0f, keel::round(xf.hint.xHeightUnits * scale));
        xf.hint.capHeightPx = keel::max(xf.hint.xHeightPx + 1.0f, keel::round(xf.hint.capHeightUnits * scale));
        xf.hint.overshootClampUnits = (fontSizePx <= 28.0f) ? (0.42f / scale) : 0.0f;
    }
    return xf;
}

// ==============================================================================
// Vector Outline Extraction API
// ==============================================================================

bool FontFace::extract_outline(
    keel::u32 glyphIndex,
    keel::f32 fontSizePx,
    const RasterOptions& opts,
    const OutlineSink& sink,
    RasterScratch* scratch
) const {
    if (!m_valid || fontSizePx <= 0.0f) {
        return false;
    }

    RasterScratch localScratch;
    RasterScratch& sc = scratch ? *scratch : localScratch;

    RasterOptions vecOpts = opts;
    vecOpts.subpixel = false; // Vector outlines are always emitted in 1x pixel space
    const Transform2D xf = make_transform(fontSizePx, vecOpts);

    OutlineBuilder ob{};
    ob.sink = &sink;
    return m_isCff
        ? extract_cff_glyph_outline(glyphIndex, xf, 0.14f, ob)
        : extract_ttf_glyph_outline(glyphIndex, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, xf, 0.14f, 0u, ob, sc);
}

// ==============================================================================
// Top-Level Glyph Rasterization (Grayscale & 5-Tap LCD Subpixel, Zero-Alloc Hot Path)
// ==============================================================================

bool FontFace::rasterize(
    keel::u32 glyphIndex,
    keel::f32 fontSizePx,
    const RasterOptions& opts,
    GlyphBitmap& out,
    RasterScratch* scratch
) const {
    out = GlyphBitmap{};
    out.isSubpixel = opts.subpixel;
    out.empty = true;

    if (!m_valid || fontSizePx <= 0.0f) {
        return false;
    }

    RasterScratch localScratch;
    RasterScratch& sc = scratch ? *scratch : localScratch;
    sc.segments.clear();

    const keel::f32 scale = fontSizePx / keel::as<keel::f32>(m_unitsPerEm);
    keel::f32 advPx = advance_width_px(glyphIndex, fontSizePx, opts.hinting, opts.syntheticBold);

    const keel::f32 xMul = opts.subpixel ? 3.0f : 1.0f;
    const Transform2D xf = make_transform(fontSizePx, opts);

    OutlineBuilder ob{};
    ob.segments = &sc.segments;
    const keel::f32 flatness = opts.subpixel ? 0.12f : 0.14f;
    const bool ok = m_isCff
        ? extract_cff_glyph_outline(glyphIndex, xf, flatness, ob)
        : extract_ttf_glyph_outline(glyphIndex, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, xf, flatness, 0u, ob, sc);

    // If variable TTF has no `HVAR`, fold phantom point advance delta from `gvar` into `advPx`
    if (ok && !m_isCff && m_hasActiveVariation && m_hvarIvsOffset == 0u && m_gvarOffset != 0u && sc.varDeltaX.size() >= 2u) {
        const keel::usize n = sc.varDeltaX.size();
        const keel::f32 phantomAdvDelta = sc.varDeltaX[n - 3u] - sc.varDeltaX[n - 4u];
        if (keel::abs(phantomAdvDelta) > 1.0e-4f) {
            const keel::f32 baseAdvUnits = keel::as<keel::f32>(advance_width_units(glyphIndex)) + phantomAdvDelta;
            advPx = keel::max(0.0f, baseAdvUnits * scale);
            if (opts.hinting == Hinting::Normal) advPx = keel::round(advPx);
            if (opts.syntheticBold) advPx += 0.35f;
        }
    }
    out.advancePx = advPx;

    if (!ok || sc.segments.empty()) {
        return true;
    }

    keel::f32 minX = 1.0e9f, maxX = -1.0e9f;
    keel::f32 minY = 1.0e9f, maxY = -1.0e9f;
    for (keel::usize i = 0u; i < sc.segments.size(); ++i) {
        const auto& s = sc.segments[i];
        minX = keel::min(minX, keel::min(s.x0, s.x1));
        maxX = keel::max(maxX, keel::max(s.x0, s.x1));
        minY = keel::min(minY, keel::min(s.y0, s.y1));
        maxY = keel::max(maxY, keel::max(s.y0, s.y1));
    }

    if (maxX <= minX || maxY <= minY) {
        return true;
    }

    const keel::f32 boldRadiusPx = opts.syntheticBold ? keel::clamp(fontSizePx / 22.0f, 0.45f, 2.2f) : 0.0f;
    const keel::f32 boldPadRight = boldRadiusPx * xMul;
    const keel::f32 boldPadTop = opts.syntheticBold ? (boldRadiusPx * 0.35f) : 0.0f;
    const keel::f32 lcdPad = opts.subpixel ? 2.0f : 0.0f;

    keel::i32 minPixX = 0, maxPixX = 0;
    if (opts.subpixel) {
        minPixX = keel::floor_i32((minX - lcdPad) / 3.0f);
        maxPixX = keel::ceil_i32((maxX + boldPadRight + lcdPad) / 3.0f);
    } else {
        minPixX = keel::floor_i32(minX);
        maxPixX = keel::ceil_i32(maxX + boldPadRight);
    }
    const keel::i32 minPixY = keel::floor_i32(minY - boldPadTop);
    const keel::i32 maxPixY = keel::ceil_i32(maxY);

    const keel::i32 pixelW = maxPixX - minPixX;
    const keel::i32 pixelH = maxPixY - minPixY;
    if (pixelW <= 0 || pixelH <= 0 || pixelW > 1024 || pixelH > 1024) {
        return true;
    }

    const keel::u32 bufW = keel::as<keel::u32>(pixelW) * (opts.subpixel ? 3u : 1u);
    const keel::u32 bufH = keel::as<keel::u32>(pixelH);
    const keel::u32 deltaStride = bufW + 1u;

    const keel::usize deltaCount = keel::as<keel::usize>(deltaStride) * keel::as<keel::usize>(bufH);
    sc.deltaBuf.assign(deltaCount, 0.0f);
    const keel::f32 originX = keel::as<keel::f32>(minPixX) * xMul;
    const keel::f32 originY = keel::as<keel::f32>(minPixY);

    for (keel::usize i = 0u; i < sc.segments.size(); ++i) {
        const auto& s = sc.segments[i];
        rasterize_edge(
            sc.deltaBuf.data(),
            bufW,
            bufH,
            deltaStride,
            s.x0 - originX,
            s.y0 - originY,
            s.x1 - originX,
            s.y1 - originY
        );
    }

    const keel::usize covCount = keel::as<keel::usize>(bufW) * keel::as<keel::usize>(bufH);
    sc.coverageBuf.resize(covCount);
    for (keel::u32 y = 0u; y < bufH; ++y) {
        const keel::f32* dRow = sc.deltaBuf.data() + keel::as<keel::usize>(y) * keel::as<keel::usize>(deltaStride);
        keel::f32* cRow = sc.coverageBuf.data() + keel::as<keel::usize>(y) * keel::as<keel::usize>(bufW);
        keel::f32 acc = 0.0f;
        for (keel::u32 x = 0u; x < bufW; ++x) {
            acc += dRow[x];
            cRow[x] = keel::clamp(keel::abs(acc), 0.0f, 1.0f);
        }
    }

    if (opts.syntheticBold) {
        apply_synthetic_bold_dilation(sc.coverageBuf, sc.boldRowScratch, bufW, bufH, boldRadiusPx * xMul, boldRadiusPx * 0.35f);
    }

    const keel::u32 rowBytes = opts.subpixel ? (keel::as<keel::u32>(pixelW) * 3u) : keel::as<keel::u32>(pixelW);
    out.width = keel::as<keel::u16>(pixelW);
    out.height = keel::as<keel::u16>(pixelH);
    out.strideBytes = keel::as<keel::u16>(rowBytes);
    out.bearingX = keel::as<keel::i16>(minPixX);
    out.bearingY = keel::as<keel::i16>(-minPixY);
    out.isSubpixel = opts.subpixel;
    out.empty = false;
    out.pixels.resize(keel::as<keel::usize>(rowBytes) * keel::as<keel::usize>(bufH));

    if (opts.subpixel) {
        static constexpr keel::f32 kW0 = 16.0f / 256.0f;
        static constexpr keel::f32 kW1 = 64.0f / 256.0f;
        static constexpr keel::f32 kW2 = 112.0f / 256.0f;

        for (keel::u32 y = 0u; y < bufH; ++y) {
            const keel::f32* src = sc.coverageBuf.data() + keel::as<keel::usize>(y) * keel::as<keel::usize>(bufW);
            keel::u8* dst = out.pixels.data() + keel::as<keel::usize>(y) * keel::as<keel::usize>(rowBytes);
            for (keel::u32 x = 0u; x < bufW; ++x) {
                const keel::f32 sM2 = (x >= 2u) ? src[x - 2u] : 0.0f;
                const keel::f32 sM1 = (x >= 1u) ? src[x - 1u] : 0.0f;
                const keel::f32 s0  = src[x];
                const keel::f32 sP1 = (x + 1u < bufW) ? src[x + 1u] : 0.0f;
                const keel::f32 sP2 = (x + 2u < bufW) ? src[x + 2u] : 0.0f;
                const keel::f32 filtered = kW0 * sM2 + kW1 * sM1 + kW2 * s0 + kW1 * sP1 + kW0 * sP2;
                dst[x] = keel::as<keel::u8>(keel::clamp(keel::round_i32(filtered * 255.0f), 0, 255));
            }
        }
    } else {
        for (keel::usize i = 0u; i < covCount; ++i) {
            out.pixels[i] = keel::as<keel::u8>(keel::clamp(keel::round_i32(sc.coverageBuf[i] * 255.0f), 0, 255));
        }
    }

    return true;
}

} // namespace keel::font
