// Text layout and painting: the title (arc / straight banner) and the track
// list (arc rings / two columns / bottom-band table), plus the text plates.
//
// Layout is split from painting: computeLayout() resolves every font size and
// position without touching the painter, so the cover layers can consult the
// text zones (for panels and scatter avoidance) before any text lands.

#include "render_internal.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPen>
#include <QRegularExpression>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace render {

namespace {

// ---- Outlined text -----------------------------------------------------------
//
// QPainter's drawText only fills glyphs, so an outline is drawn by turning the
// text into a QPainterPath and stroking that path in the outline colour before
// filling it. A pen straddles the path it strokes, so it is set to twice the
// wanted width and the fill is painted on top to restore the glyph body.

void strokeTextPath(QPainter &p, const QPainterPath &path, const QColor &fill,
                    const QColor &outlineColor, double outlineWidth)
{
    QPen pen(outlineColor);
    pen.setWidthF(outlineWidth * 2.0);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPath(path);
}

QPointF alignedBaseline(const QRectF &rect, const QFontMetricsF &fm,
                        Qt::Alignment flags, const QString &text)
{
    const double w = fm.horizontalAdvance(text);
    double x;
    if (flags & Qt::AlignRight)
        x = rect.right() - w;
    else if (flags & Qt::AlignHCenter)
        x = rect.center().x() - w / 2.0;
    else
        x = rect.left();
    double y;
    if (flags & Qt::AlignVCenter)
        y = rect.center().y() + (fm.ascent() - fm.descent()) / 2.0;
    else if (flags & Qt::AlignBottom)
        y = rect.bottom() - fm.descent();
    else
        y = rect.top() + fm.ascent();
    return {x, y};
}

void drawTextOutlined(QPainter &p, const QRectF &rect, Qt::Alignment flags,
                      const QString &text, const QColor &fill,
                      const QColor *outlineColor, double outlineWidth)
{
    if (outlineColor && outlineWidth > 0) {
        const QFontMetricsF fm(p.font());
        QPainterPath path;
        path.addText(alignedBaseline(rect, fm, flags, text), p.font(), text);
        strokeTextPath(p, path, fill, *outlineColor, outlineWidth);
    } else {
        p.setPen(QPen(fill));
        p.drawText(rect, int(flags), text);
    }
}

// ---- Text plates ---------------------------------------------------------------

// Rounded panel behind a straight run of text. `rect` is the tight text
// bounds, expanded by the pads; corner radius follows the plate height.
void drawRectPlate(QPainter &p, const QRectF &rect, const LabelConfig &cfg,
                   double padX, double padY, double ref)
{
    if (!cfg.plateEnabled || cfg.plateAlpha <= 0)
        return;
    const QRectF panel = rect.adjusted(-padX, -padY, padX, padY);
    const double radius = panel.height() * qBound(0.0, cfg.plateRadius, 0.5);
    QColor fill(cfg.plateColor);
    fill.setAlpha(qBound(0, cfg.plateAlpha, 255));
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (cfg.plateOutline)
        p.setPen(QPen(QColor(cfg.plateOutlineColor), qMax(1.0, ref * 0.004)));
    else
        p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(panel, radius, radius);
    p.restore();
}

// Arc-shaped pill behind one curved text run: the arc at the glyph band's
// centre radius, stroked with a wide pen and (usually) round caps.
void drawArcPlate(QPainter &p, double cx, double cy, double baselineR,
                  double centerDeg, double angWidth, const QFontMetricsF &fm,
                  const LabelConfig &cfg, double ref)
{
    if (!cfg.plateEnabled || cfg.plateAlpha <= 0 || angWidth <= 0)
        return;
    const double padX = fm.height() * cfg.platePad;
    const double padY = fm.height() * cfg.platePad * 0.45;
    const double rc = baselineR + (fm.ascent() - fm.descent()) / 2.0;
    if (rc <= 1.0)
        return;
    const double spanDeg = qRadiansToDegrees(angWidth)
                           + 2.0 * qRadiansToDegrees(padX / rc);
    const QRectF rect(cx - rc, cy - rc, 2 * rc, 2 * rc);
    QPainterPath arc;
    arc.arcMoveTo(rect, centerDeg - spanDeg / 2.0);
    arc.arcTo(rect, centerDeg - spanDeg / 2.0, spanDeg);
    QPainterPathStroker stroker;
    stroker.setWidth(fm.height() + 2.0 * padY);
    stroker.setCapStyle(cfg.plateRadius > 0.05 ? Qt::RoundCap : Qt::FlatCap);
    const QPainterPath pill = stroker.createStroke(arc);
    QColor fill(cfg.plateColor);
    fill.setAlpha(qBound(0, cfg.plateAlpha, 255));
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPath(pill);
    if (cfg.plateOutline) {
        p.setPen(QPen(QColor(cfg.plateOutlineColor), qMax(1.0, ref * 0.004)));
        p.setBrush(Qt::NoBrush);
        p.drawPath(pill);
    }
    p.restore();
}

// ---- Arc text --------------------------------------------------------------------

// Trim (with an ellipsis) only if the text would wrap past MAX_TEXT_ANGLE at
// this radius — long titles fit untrimmed because the arc is so long.
QString fitToAngle(const QFontMetricsF &fm, QString text, double radius)
{
    if (radius <= 0 || fm.horizontalAdvance(text) / radius <= MAX_TEXT_ANGLE)
        return text;
    const QString ell = QStringLiteral("…");
    QString trimmed = text;
    while (!trimmed.isEmpty()
           && fm.horizontalAdvance(trimmed + ell) / radius > MAX_TEXT_ANGLE)
        trimmed.chop(1);
    return trimmed.isEmpty() ? text.left(1) : trimmed + ell;
}

// Draw `text` curved along a circle of `radius` centred on (cx, cy), reading
// left-to-right and centred on `centerDeg` (90° = top of the disc). Characters
// sit upright with their baseline tangent to the circle. Returns the angular
// width used (radians).
double drawArcText(QPainter &p, double cx, double cy, double radius,
                   QString text, const QColor &color, bool underline,
                   double centerDeg, const QColor *outlineColor,
                   double outlineWidth)
{
    const QFontMetricsF fm(p.font());
    text = fitToAngle(fm, text, radius);
    const double angWidth = fm.horizontalAdvance(text) / radius;

    // Accumulate every glyph into one path (each placed by its own tangent
    // transform) so the whole line's outline can be stroked first and the
    // whole line filled on top — stroking glyph by glyph would let each
    // glyph's outline paint over the neighbour body beside it.
    const QFont font = p.font();
    QPainterPath line;
    double ang = qDegreesToRadians(centerDeg) + angWidth / 2.0;  // leftmost edge
    for (const QChar ch : text) {
        const double w = fm.horizontalAdvance(ch);
        const double a = w / radius;
        const double theta = ang - a / 2.0;          // centre of this glyph
        const double x = cx + radius * std::cos(theta);
        const double y = cy - radius * std::sin(theta);  // screen y grows down
        QPainterPath glyph;
        glyph.addText(QPointF(-w / 2.0, 0.0), font, QString(ch));
        QTransform xf;
        xf.translate(x, y);
        xf.rotate(90.0 - qRadiansToDegrees(theta));  // baseline tangent, upright
        line.addPath(xf.map(glyph));
        ang -= a;
    }

    if (outlineColor && outlineWidth > 0) {
        strokeTextPath(p, line, color, *outlineColor, outlineWidth);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(line);
    }

    if (underline) {
        const double thickness = qMax(1.5, fm.height() * 0.06);
        const double ru = radius - fm.descent() * 0.7 - thickness;
        const QRectF rect(cx - ru, cy - ru, ru * 2, ru * 2);
        QPen pen(color);
        pen.setWidthF(thickness);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const double startDeg = centerDeg - qRadiansToDegrees(angWidth) / 2.0;
        p.drawArc(rect, int(startDeg * 16),
                  int(qRadiansToDegrees(angWidth) * 16));
    }
    return angWidth;
}

// Flow `lines` around concentric `rings` (baseline radii, outer→inner). Each
// line consumes an arc proportional to its length; when a ring reaches
// `maxSpan` radians the next line wraps to the ring inside it. Returns false
// (leaving `out` partial) if the lines don't fit and overflow isn't allowed.
bool layoutRings(const QStringList &lines, const QVector<double> &rings,
                 const QFontMetricsF &fm, double gapPx, double maxSpan,
                 bool allowOverflow, QVector<QVector<ArcRingContent>> &out)
{
    out = QVector<QVector<ArcRingContent>>(rings.size());
    int idx = 0;
    double used = 0.0;
    for (const QString &line : lines) {
        const double adv = fm.horizontalAdvance(line);
        double r = rings[idx];
        double w = adv / r;
        double add = w + (out[idx].isEmpty() ? 0.0 : gapPx / r);
        if (used + add > maxSpan && !out[idx].isEmpty()) {
            if (idx + 1 < rings.size()) {
                ++idx;
                used = 0.0;
                r = rings[idx];
                w = adv / r;
                add = w;
            } else if (!allowOverflow) {
                return false;
            }
            // else: no rings left — keep piling onto the last one.
        }
        out[idx].append({line, w});
        used += add;
    }
    return true;
}

// ---- Chord helpers ---------------------------------------------------------------

double chordHalf(double r, double dy)
{
    dy = qMin(std::abs(dy), r);
    return std::sqrt(qMax(r * r - dy * dy, 0.0));
}

// ---- Title layout -----------------------------------------------------------------

TitleLayout computeTitleArc(const Geometry &g, const QVector<TitleLine> &lines,
                            const LabelConfig &cfg)
{
    TitleLayout t;
    t.arc = true;
    if (lines.isEmpty())
        return t;
    t.present = true;
    const double basePx = qMax(14.0, g.ref * 0.10 * cfg.titleSize);
    // +x rotates the run around the rim; +y (down) slides it toward the hub.
    t.centerDeg = 90.0 - cfg.titleOffsetX * 60.0;
    double topR = g.outerR * 0.99 - cfg.titleOffsetY * g.outerR;
    for (const TitleLine &line : lines) {
        const QFont f = makeFont(cfg.titleFont, basePx * line.mult,
                                 cfg.titleBold, cfg.titleItalic);
        const QFontMetricsF fm(f);
        const double lineR = qMax(1.0, topR - fm.ascent());
        const QString text = fitToAngle(fm, line.text, lineR);
        t.halfDeg = qMax(t.halfDeg, qRadiansToDegrees(
                             fm.horizontalAdvance(text) / lineR) / 2.0);
        t.arcLines.append({f, text, lineR});
        topR = lineR - fm.descent() - g.ref * 0.02;
    }
    t.bottomR = topR;
    // Zone: the annular band the title occupies.
    t.zone.addEllipse(QPointF(g.cx, g.cy), g.outerR, g.outerR);
    const double zin = qMax(0.0, t.bottomR);
    t.zone.addEllipse(QPointF(g.cx, g.cy), zin, zin);
    t.zoneIsBand = false;
    return t;
}

TitleLayout computeTitleStraight(const Geometry &g,
                                 const QVector<TitleLine> &lines,
                                 const LabelConfig &cfg)
{
    TitleLayout t;
    t.arc = false;
    if (lines.isEmpty())
        return t;
    t.present = true;
    const double pad = qBound(0.0, cfg.titlePad, 2.0);
    const double bandH = qBound(0.0, cfg.titleBand, 0.6) * 2.0 * g.outerR;
    const double offY = cfg.titleOffsetY * g.outerR;
    const double top = g.cy - g.outerR + g.ref * 0.03 * pad + offY;
    const double bot = g.cy - g.outerR + bandH - g.ref * 0.02 * pad + offY;
    t.stackCx = g.cx + cfg.titleOffsetX * g.outerR;
    if (bot <= top)
        return t;

    // Grow the base size until the stack no longer overflows the band height,
    // capped so short titles don't balloon.
    const int hi = int(qMax(10.0, g.ref * 0.11 * cfg.titleSize));
    QVector<QFont> fonts(lines.size());
    double totalH = 0.0;
    for (int px = hi; px >= 8; --px) {
        totalH = 0.0;
        for (int i = 0; i < lines.size(); ++i) {
            fonts[i] = makeFont(cfg.titleFont,
                                qMax(8, int(px * lines[i].mult)),
                                cfg.titleBold, cfg.titleItalic);
            totalH += QFontMetricsF(fonts[i]).height();
        }
        if (totalH <= bot - top)
            break;
    }

    double y = (top + bot) / 2.0 - totalH / 2.0;
    for (int i = 0; i < lines.size(); ++i) {
        QFont f = fonts[i];
        QFontMetricsF fm(f);
        // Widest the disc allows at this line, measured at the edge nearest
        // the rim so the line never spills past the circle.
        double lineH = fm.height();
        const double dy = std::abs(y + lineH / 2.0 - g.cy) + lineH / 2.0;
        double avail = 2.0 * chordHalf(g.outerR, dy) * 0.96;
        while (fm.horizontalAdvance(lines[i].text) > avail && f.pixelSize() > 8) {
            f.setPixelSize(f.pixelSize() - 1);
            fm = QFontMetricsF(f);
            lineH = fm.height();
        }
        t.stack.append({f, lines[i].text, y, avail});
        y += lineH;
    }
    // Zone: the top band the banner occupies. The bottom stays put (the tracks
    // sit below it); only the panel can grow. When the band is offset down it
    // leaves a gap above it, so `titleBandEdge` stretches the zone up to the
    // disc edge — full width from the canvas top. The ring clip in
    // paintCoverStage bounds it to the real background edge, which is the bleed
    // edge when a full bleed is on and the printable rim otherwise.
    const double zoneBot = g.cy - g.outerR + offY + bandH;
    if (cfg.titleBandEdge)
        t.zoneRect = QRectF(0.0, 0.0, 2.0 * g.cx, zoneBot);
    else
        t.zoneRect = QRectF(g.cx - g.outerR, g.cy - g.outerR + offY,
                            2.0 * g.outerR, bandH);
    t.zone.addRect(t.zoneRect);
    t.zoneIsBand = true;
    return t;
}

// ---- Arc tracks -------------------------------------------------------------------

TracksLayout computeTracksArc(const Geometry &g, const QStringList &entries,
                              const LabelConfig &cfg, const TitleLayout &title,
                              double ringBand)
{
    TracksLayout t;
    t.kind = TRACKS_ARC;

    // Top of the track band: below the title (whichever layout it uses), below
    // any ring-feature band, then the radial nudge.
    double tracksOuterR = g.outerR * 0.99;
    if (title.present) {
        if (title.arc) {
            tracksOuterR = title.bottomR - g.ref * 0.04;
        } else {
            const double below = g.cy - title.zoneRect.bottom();
            if (below > 0)
                tracksOuterR = qMin(tracksOuterR, below);
        }
    }
    if (ringBand > 0)
        tracksOuterR = qMin(tracksOuterR, g.outerR - (ringBand + g.ref * 0.02));
    tracksOuterR -= cfg.trackOffset * g.outerR;
    tracksOuterR = qBound(g.innerR + g.ref * 0.05, tracksOuterR,
                          g.outerR * 0.99);
    t.tracksOuterR = tracksOuterR;
    if (entries.isEmpty())
        return t;
    t.present = true;

    const auto buildRings = [&](const QFontMetricsF &fm) {
        QVector<double> rings;
        const double pitch = fm.height() * 1.08;
        double r = tracksOuterR - fm.ascent();
        while (r - fm.descent() >= g.innerR && rings.size() < 12) {
            rings.append(r);
            r -= pitch;
        }
        return rings;
    };

    // Pick the largest font that still lets every track fit in the ring band.
    const int maxPx = int(qMax(9.0, g.ref * 0.06 * cfg.trackSize));
    int minPx = int(qMax(7.0, g.ref * 0.022 * cfg.trackSize));
    minPx = qMin(minPx, maxPx);
    bool found = false;
    for (int px = maxPx; px >= minPx && !found; --px) {
        const QFont f = makeFont(cfg.trackFont, px, cfg.trackBold,
                                 cfg.trackItalic);
        const QFontMetricsF fm(f);
        const QVector<double> rings = buildRings(fm);
        if (rings.isEmpty())
            continue;
        QVector<QVector<ArcRingContent>> contents;
        if (layoutRings(entries, rings, fm, px * 0.9, MAX_TEXT_ANGLE, false,
                        contents)) {
            t.font = f;
            t.rings = rings;
            t.ringContents = contents;
            found = true;
        }
    }
    if (!found) {                        // too many tracks — shrink and pack
        const QFont f = makeFont(cfg.trackFont, minPx, cfg.trackBold,
                                 cfg.trackItalic);
        const QFontMetricsF fm(f);
        QVector<double> rings = buildRings(fm);
        if (rings.isEmpty())
            rings.append(qMax(tracksOuterR - fm.ascent(),
                              g.innerR + fm.ascent()));
        QVector<QVector<ArcRingContent>> contents;
        layoutRings(entries, rings, fm, minPx * 0.9, MAX_TEXT_ANGLE, true,
                    contents);
        t.font = f;
        t.rings = rings;
        t.ringContents = contents;
    }

    // Zone: the annular band the rings occupy.
    const QFontMetricsF fm(t.font);
    double innermost = tracksOuterR;
    for (double r : t.rings)
        innermost = qMin(innermost, r);
    const double zin = qBound(0.0, innermost - fm.height(), tracksOuterR);
    t.zone.addEllipse(QPointF(g.cx, g.cy), qMin(g.outerR, tracksOuterR),
                      qMin(g.outerR, tracksOuterR));
    t.zone.addEllipse(QPointF(g.cx, g.cy), zin, zin);
    t.zoneIsBand = false;
    return t;
}

// ---- Column tracks ------------------------------------------------------------------

TracksLayout computeTracksColumns(const Geometry &g,
                                  const QStringList &trackTitles,
                                  const LabelConfig &cfg)
{
    TracksLayout t;
    t.kind = TRACKS_COLUMNS;

    struct Entry {
        QString num, title;
    };
    QVector<Entry> entries;
    for (int i = 0; i < trackTitles.size(); ++i) {
        const QString title = trackTitles[i].trimmed();
        entries.append({cfg.trackNumbers ? QStringLiteral("%1.").arg(i + 1)
                                         : QString(),
                        title});
    }
    if (entries.isEmpty())
        return t;
    t.present = true;

    // Even counts split cleanly into two equal columns. An odd count would
    // leave the columns misaligned by half a pitch, so the final track becomes
    // a centred "finale" row below two equal columns.
    QVector<Entry> left, right;
    bool hasCenter = false;
    Entry center;
    if (entries.size() % 2 == 1 && entries.size() >= 3) {
        const int split = entries.size() / 2;
        left = entries.mid(0, split);
        right = entries.mid(split, split);
        center = entries[2 * split];
        hasCenter = true;
    } else {
        const int split = (entries.size() + 1) / 2;
        left = entries.mid(0, split);
        right = entries.mid(split);
    }

    const double outMargin = g.outerR * 0.02;  // small gap to the printable rim
    const double inMargin = outMargin;         // same gap on the inner ring side
    const double bandV = g.outerR * 0.62;      // rows may spread this far
    const double maxPitch = g.outerR * 0.13;   // but keep lines packed close

    const int rows = qMax(qMax(left.size(), right.size()), 1);
    const double basePitch = qMin((2.0 * bandV) / rows, maxPitch);
    // Farthest row centre from the disc middle, in pitch units — the finale
    // row, when present, sits one pitch past the columns.
    const double extent = (rows - 1) / 2.0 + (hasCenter ? 1.0 : 0.0);
    const double capPitch =
        extent > 0 ? (g.outerR * 0.80) / extent : maxPitch;
    const double pitch = qMin(basePitch * cfg.trackSpacing, capPitch);
    const double lineH = pitch;

    const auto rowY = [&](int count, int i) {
        return g.cy + (i - (count - 1) / 2.0) * pitch;
    };
    const auto innerHalf = [&](double dy) {
        return dy < g.innerR
                   ? std::sqrt(qMax(g.innerR * g.innerR - dy * dy, 0.0))
                   : 0.0;
    };
    const auto rowAvail = [&](double dy) {
        return qMax(chordHalf(g.outerR, dy) - innerHalf(dy) - outMargin
                        - inMargin,
                    1.0);
    };

    // Every entry gets the same block width — number column + gutter + a title
    // slot as wide as the longest title — so the underlines come out equal.
    struct Metrics {
        QFont font;
        double numW, gutter, titleSlot, block, height;
    };
    const auto metrics = [&](int px) {
        const QFont f = makeFont(cfg.trackFont, px, cfg.trackBold,
                                 cfg.trackItalic);
        const QFontMetricsF m(f);
        double numW = 0, titleSlot = 0;
        for (const Entry &e : entries) {
            numW = qMax(numW, m.horizontalAdvance(e.num));
            titleSlot = qMax(titleSlot, m.horizontalAdvance(e.title));
        }
        const double gutter = cfg.trackNumbers
                                  ? m.horizontalAdvance(QStringLiteral("0")) * 0.9
                                  : 0.0;
        return Metrics{f, numW, gutter, titleSlot,
                       numW + gutter + titleSlot, m.height()};
    };
    const auto platePads = [&](const Metrics &m) {
        // Vertical padding is kept smaller so tight rows don't collide.
        if (!cfg.plateEnabled)
            return QPointF(0.0, 0.0);
        return QPointF(m.height * cfg.platePad, m.height * cfg.platePad * 0.45);
    };
    // Widest block that fits at a row centred `centerDy` from the disc centre.
    // With a plate, the panel (text + padding) must sit wholly inside the safe
    // ring, so the outer edge is measured at the plate's far corner and the
    // inner edge at its near corner.
    const auto rowAvailFor = [&](const Metrics &m, double centerDy) {
        if (!cfg.plateEnabled)
            return rowAvail(centerDy);
        const QPointF pads = platePads(m);
        const double halfV = m.height / 2.0 + pads.y();
        const double far = centerDy + halfV;
        const double near = qMax(0.0, centerDy - halfV);
        return qMax(chordHalf(g.outerR, far) - innerHalf(near) - outMargin
                        - inMargin - 2.0 * pads.x(),
                    1.0);
    };
    // The finale row sits one pitch below the columns' bottom edge.
    const double centerDy = ((rows - 1) / 2.0 + 1.0) * pitch;
    const auto fits = [&](const Metrics &m) {
        for (const auto *col : {&left, &right}) {
            for (int i = 0; i < col->size(); ++i)
                if (m.block > rowAvailFor(m, std::abs(rowY(col->size(), i) - g.cy)))
                    return false;
        }
        if (hasCenter) {
            const QPointF pads = platePads(m);
            const double far = centerDy + m.height / 2.0 + pads.y();
            const double avail = 2.0 * chordHalf(g.outerR, far)
                                 - 2.0 * (outMargin + pads.x());
            if (m.block > avail)
                return false;
        }
        return true;
    };

    const int hi = int(qMax(7.0, qMin(g.ref * 0.055 * cfg.trackSize,
                                      lineH * 0.80)));
    Metrics chosen = metrics(hi);
    for (int px = hi; px >= 6; --px) {
        chosen = metrics(px);
        if (fits(chosen))
            break;
    }

    t.font = chosen.font;
    t.numW = chosen.numW;
    t.gutter = chosen.gutter;
    t.titleSlot = chosen.titleSlot;
    t.blockW = chosen.block;
    t.lineH = lineH;

    const QFontMetricsF fm(t.font);
    const double ulThick = qMax(1.0, g.ref * 0.006);
    // Drop from a row's centre down to its underline; the ring is curved, so
    // the underline's rim-hugging ends are measured at this height.
    const double ulDrop = (fm.ascent() - fm.descent()) / 2.0
                          + fm.descent() * 0.7 + ulThick;
    const QPointF pads = platePads(chosen);
    const double plateHalfV = fm.height() / 2.0 + pads.y();

    const auto blockOuterX = [&](double yCenter, double ulY, bool leftCol) {
        double reach;
        if (cfg.plateEnabled)
            reach = chordHalf(g.outerR, std::abs(yCenter - g.cy) + plateHalfV)
                    - outMargin - pads.x();
        else
            reach = chordHalf(g.outerR, std::abs(ulY - g.cy)) - outMargin;
        return leftCol ? g.cx - reach : g.cx + reach - t.blockW;
    };

    // Left column: number and underline hug the outer rim; the underline runs
    // in to the inner ring.
    for (int i = 0; i < left.size(); ++i) {
        const double y = rowY(left.size(), i);
        const double ulY = y + ulDrop;
        const double inner = innerHalf(std::abs(ulY - g.cy));
        ColumnRow row;
        row.num = left[i].num;
        row.title = left[i].title;
        row.yCenter = y;
        row.blockLeft = blockOuterX(y, ulY, true);
        row.ulX0 = g.cx - chordHalf(g.outerR, std::abs(ulY - g.cy)) + outMargin;
        row.ulX1 = g.cx - inner - inMargin;
        row.ulY = ulY;
        t.rows.append(row);
    }
    // Right column: mirrored.
    for (int i = 0; i < right.size(); ++i) {
        const double y = rowY(right.size(), i);
        const double ulY = y + ulDrop;
        const double inner = innerHalf(std::abs(ulY - g.cy));
        ColumnRow row;
        row.num = right[i].num;
        row.title = right[i].title;
        row.yCenter = y;
        row.blockLeft = blockOuterX(y, ulY, false);
        row.ulX0 = g.cx + inner + inMargin;
        row.ulX1 = g.cx + chordHalf(g.outerR, std::abs(ulY - g.cy)) - outMargin;
        row.ulY = ulY;
        t.rows.append(row);
    }
    // Finale row: the odd track, centred under both columns.
    if (hasCenter) {
        ColumnRow row;
        row.num = center.num;
        row.title = center.title;
        row.yCenter = g.cy + centerDy;
        row.blockLeft = g.cx - t.blockW / 2.0;
        row.ulX0 = row.blockLeft;
        row.ulX1 = row.blockLeft + t.blockW;
        row.ulY = row.yCenter + ulDrop;
        t.rows.append(row);
    }

    // Zone: the horizontal band the rows span.
    double top = g.cy, bottom = g.cy;
    for (const ColumnRow &row : t.rows) {
        top = qMin(top, row.yCenter - fm.height() / 2.0 - pads.y());
        bottom = qMax(bottom, row.ulY + g.ref * 0.01 + pads.y());
    }
    t.zoneRect = QRectF(g.cx - g.outerR, top, 2.0 * g.outerR, bottom - top);
    t.zone.addRect(t.zoneRect);
    t.zoneIsBand = true;
    return t;
}

// ---- Table tracks ------------------------------------------------------------------

TracksLayout computeTracksTable(const Geometry &g,
                                const QStringList &trackTitles,
                                const LabelConfig &cfg)
{
    TracksLayout t;
    t.kind = TRACKS_TABLE;

    QStringList entries;
    if (cfg.trackNumbers) {
        // Right-align the number to the widest track's digit count so the
        // dots and titles line up in a column.
        const int numW = QString::number(trackTitles.size()).size();
        for (int i = 0; i < trackTitles.size(); ++i) {
            const QString title = trackTitles[i].trimmed();
            if (!title.isEmpty())
                entries.append(QStringLiteral("%1.  %2")
                                   .arg(i + 1, numW)
                                   .arg(title));
        }
    } else {
        for (const QString &title : trackTitles)
            if (!title.trimmed().isEmpty())
                entries.append(title.trimmed());
    }

    const double pad = qBound(0.0, cfg.trackPad, 2.0);
    const double bandH = qBound(0.0, cfg.trackBand, 0.6) * 2.0 * g.outerR;
    const double midBot = g.cy + g.outerR - bandH;
    const double bandTop = midBot + g.ref * 0.02 * pad;
    // Stop short of the disc's narrow tip so the last rows keep usable width.
    const double tipCap = g.outerR * qMin(0.96, 0.93 + (1.0 - pad) * 0.03);
    const double bandBot = g.cy + qMin(g.outerR - g.ref * 0.03 * pad, tipCap);
    const double bandAvail = bandBot - bandTop;

    // Zone regardless of fitting (panels may still want the band). The tracks
    // band can't be offset, so its panel always runs down to the disc edge —
    // full width from the canvas bottom. The ring clip in paintCoverStage
    // bounds it to the real background edge (the bleed edge when a full bleed
    // is on, the printable rim otherwise), so the band never leaves a gap
    // along the bottom rim.
    t.zoneRect = QRectF(0.0, midBot, 2.0 * g.cx, 2.0 * g.cy - midBot);
    t.zone.addRect(t.zoneRect);
    t.zoneIsBand = true;

    if (entries.isEmpty() || bandAvail <= 0)
        return t;

    const double margin = g.ref * 0.02 * pad;
    const int n = entries.size();
    const int hi = int(qMax(9.0, g.ref * 0.06 * cfg.trackSize));

    struct Fit {
        int px = 0;
        QFont font;
        QVector<QStringList> columns;
        QVector<double> slotWidths;
        int rows = 0;
        double lineH = 0, gutter = 0, rowW = 0, y0 = 0;
    };
    // Largest font at which `ncols` columns fit the band's height and every
    // row stays inside the disc chord.
    const auto fit = [&](int ncols) {
        Fit out;
        const int rows = (n + ncols - 1) / ncols;
        QVector<QStringList> columns;
        for (int c = 0; c < ncols; ++c)
            columns.append(entries.mid(c * rows, rows));
        for (int px = hi; px >= 7; --px) {
            const QFont f = makeFont(cfg.trackFont, px, cfg.trackBold,
                                     cfg.trackItalic);
            const QFontMetricsF fm(f);
            const double lineH = fm.height() * 1.05;
            if (rows * lineH > bandAvail)
                continue;
            QVector<double> slotWidths;
            for (const QStringList &col : columns) {
                double w = 0;
                for (const QString &e : col)
                    w = qMax(w, fm.horizontalAdvance(e));
                slotWidths.append(w);
            }
            const double gutter = fm.height() * 0.7;
            const double platePad =
                cfg.plateEnabled ? fm.height() * cfg.platePad : 0.0;
            double rowW = gutter * (ncols - 1);
            for (double s : slotWidths)
                rowW += s;
            // Vertical slack: at full padding the block is centred; lowering
            // the pad slides it up toward the band top where the disc is
            // wider, so the bottom rows gain room.
            const double slack = qMax(0.0, bandAvail - rows * lineH);
            const double y0 = bandTop + slack * (pad * 0.5);
            bool ok = true;
            for (int i = 0; i < rows && ok; ++i)
                ok = rowW <= 2.0 * chordHalf(g.outerR,
                                             y0 + (i + 1) * lineH - g.cy)
                                 - 2.0 * (margin + platePad);
            if (ok) {
                out.px = px;
                out.font = f;
                out.columns = columns;
                out.slotWidths = slotWidths;
                out.rows = rows;
                out.lineH = lineH;
                out.gutter = gutter;
                out.rowW = rowW;
                out.y0 = y0;
                return out;
            }
        }
        return out;   // px == 0 → no fit
    };

    // Try one to five columns; keep whichever packs the listing at the
    // largest font (ties prefer fewer columns — a cleaner block).
    Fit best;
    for (int ncols = 1; ncols <= 5; ++ncols) {
        const Fit cand = fit(ncols);
        if (cand.px > best.px)
            best = cand;
    }
    if (best.px == 0)
        return t;

    t.present = true;
    t.font = best.font;
    t.columns = best.columns;
    t.slotWidths = best.slotWidths;
    t.tableRows = best.rows;
    t.tableLineH = best.lineH;
    t.tableGutter = best.gutter;
    t.tableRowW = best.rowW;
    t.tableY0 = best.y0;
    return t;
}

} // namespace

// ---- Public (module-internal) API ----------------------------------------------------

QFont makeFont(const QString &family, double px, bool bold, bool italic)
{
    QFont f(family.isEmpty() ? DEFAULT_FONT : family);
    f.setPixelSize(int(qMax(1.0, px)));
    f.setBold(bold);
    f.setItalic(italic);
    return f;
}

QVector<TitleLine> resolveTitleLines(const QString &title,
                                     const LabelConfig &cfg)
{
    // The album title is only a default: it's used when the override is truly
    // empty (a fresh preset that never touched the field). Once the field holds
    // anything at all — even just a space or a blank line — it wins, so typing
    // whitespace is how you clear the title (it trims to no lines below).
    // Interior blank lines are kept as vertical spacers (each layout renders an
    // empty line as its own line height); only stray leading/trailing blanks
    // are dropped so a trailing newline doesn't skew the centring. A line
    // starting with "[1.5]" scales that line.
    static const QRegularExpression lineSizeRe(
        QStringLiteral(R"(^\s*\[\s*(\d*\.?\d+)\s*\]\s?(.*)$)"));
    const QString raw = cfg.titleOverride.isEmpty() ? title : cfg.titleOverride;
    QVector<TitleLine> lines;
    for (const QString &ln : raw.split(QLatin1Char('\n'))) {
        double mult = 1.0;
        QString text = ln.trimmed();
        const QRegularExpressionMatch m = lineSizeRe.match(ln);
        if (m.hasMatch()) {
            bool ok = false;
            const double v = m.captured(1).toDouble(&ok);
            mult = ok ? v : 1.0;
            text = m.captured(2).trimmed();
        }
        lines.append({text, qMax(0.1, mult)});
    }
    while (!lines.isEmpty() && lines.first().text.isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().text.isEmpty())
        lines.removeLast();
    return lines;
}

QPainterPath printableRing(double cx, double cy, double innerR, double outerR)
{
    QPainterPath path;
    path.addEllipse(QPointF(cx, cy), outerR, outerR);
    if (innerR > 0)
        path.addEllipse(QPointF(cx, cy), innerR, innerR);  // OddEven → hollow
    return path;
}

Layout computeLayout(const Geometry &g, const QString &title,
                     const QStringList &trackTitles, const LabelConfig &cfg,
                     bool ringFeature)
{
    Layout lay;
    lay.g = g;
    lay.ringBand = ringFeature
                       ? (g.outerR - g.innerR) * qBound(0.0, cfg.ringDepth, 0.9)
                       : 0.0;

    const QVector<TitleLine> lines = resolveTitleLines(title, cfg);
    lay.title = (cfg.titleLayout == TITLE_STRAIGHT)
                    ? computeTitleStraight(g, lines, cfg)
                    : computeTitleArc(g, lines, cfg);

    QStringList entries;
    if (cfg.trackLayout == TRACKS_ARC) {
        for (int i = 0; i < trackTitles.size(); ++i) {
            const QString text = trackTitles[i].trimmed();
            entries.append(cfg.trackNumbers
                               ? QStringLiteral("%1.  %2").arg(i + 1).arg(text)
                               : text);
        }
    }
    if (cfg.trackLayout == TRACKS_COLUMNS)
        lay.tracks = computeTracksColumns(g, trackTitles, cfg);
    else if (cfg.trackLayout == TRACKS_TABLE)
        lay.tracks = computeTracksTable(g, trackTitles, cfg);
    else
        lay.tracks = computeTracksArc(g, entries, cfg, lay.title, lay.ringBand);
    return lay;
}

void drawTitle(QPainter &p, const Layout &lay, const LabelConfig &cfg)
{
    const TitleLayout &t = lay.title;
    if (!t.present)
        return;
    const Geometry &g = lay.g;
    const QColor color(cfg.titleColor);
    const QColor outlineColor(cfg.titleOutlineColor);
    const QColor *outline = cfg.titleOutline ? &outlineColor : nullptr;

    if (t.arc) {
        for (const ArcTitleLine &line : t.arcLines) {
            p.setFont(line.font);
            const QFontMetricsF fm(line.font);
            const double angW = fm.horizontalAdvance(line.text) / line.radius;
            drawArcPlate(p, g.cx, g.cy, line.radius, t.centerDeg, angW, fm,
                         cfg, g.ref);
            const double ow = outline
                                  ? line.font.pixelSize() * cfg.titleOutlineWidth
                                  : 0.0;
            drawArcText(p, g.cx, g.cy, line.radius, line.text, color,
                        cfg.titleUnderline, t.centerDeg, outline, ow);
        }
        return;
    }

    for (const StackLine &line : t.stack) {
        p.setFont(line.font);
        const QFontMetricsF fm(line.font);
        const double lineH = fm.height();
        const double textW = fm.horizontalAdvance(line.text);
        if (cfg.plateEnabled) {
            const double padX = lineH * cfg.platePad;
            const double padY = lineH * cfg.platePad * 0.5;
            drawRectPlate(p,
                          QRectF(t.stackCx - textW / 2.0, line.y, textW, lineH),
                          cfg, padX, padY, g.ref);
        }
        const double ow =
            outline ? line.font.pixelSize() * cfg.titleOutlineWidth : 0.0;
        drawTextOutlined(p,
                         QRectF(t.stackCx - line.availW / 2.0, line.y,
                                line.availW, lineH),
                         Qt::AlignCenter, line.text, color, outline, ow);
        if (cfg.titleUnderline) {
            const double uy = line.y + lineH - fm.descent() * 0.5;
            p.setPen(QPen(color, qMax(1.0, g.ref * 0.006)));
            p.drawLine(QPointF(t.stackCx - textW / 2.0, uy),
                       QPointF(t.stackCx + textW / 2.0, uy));
        }
    }
}

void drawTracks(QPainter &p, const Layout &lay, const LabelConfig &cfg)
{
    const TracksLayout &t = lay.tracks;
    if (!t.present)
        return;
    const Geometry &g = lay.g;
    const QColor color(cfg.trackColor);
    const QColor outlineColor(cfg.trackOutlineColor);
    const QColor *outline = cfg.trackOutline ? &outlineColor : nullptr;
    p.setFont(t.font);
    const QFontMetricsF fm(t.font);
    const double trackPx = t.font.pixelSize();
    const double ow = outline ? trackPx * cfg.trackOutlineWidth : 0.0;

    if (t.kind == TRACKS_ARC) {
        for (int idx = 0; idx < t.ringContents.size(); ++idx) {
            const auto &contents = t.ringContents[idx];
            if (contents.isEmpty())
                continue;
            const double r = t.rings[idx];
            const int count = contents.size();
            double textW = 0.0;
            for (const ArcRingContent &c : contents)
                textW += c.angWidth;
            // Spread the ring's tracks evenly around the whole circle so
            // every gap (including the one across the bottom) is equal.
            const double gapAng =
                count > 1 ? (2.0 * M_PI - textW) / count : 0.0;
            const double total = textW + gapAng * (count - 1);
            double ang = 90.0 + qRadiansToDegrees(total / 2.0);
            for (int j = 0; j < count; ++j) {
                if (j > 0)
                    ang -= qRadiansToDegrees(gapAng);
                const double center =
                    ang - qRadiansToDegrees(contents[j].angWidth / 2.0);
                drawArcPlate(p, g.cx, g.cy, r, center, contents[j].angWidth,
                             fm, cfg, g.ref);
                drawArcText(p, g.cx, g.cy, r, contents[j].text, color,
                            cfg.trackUnderline, center, outline, ow);
                ang -= qRadiansToDegrees(contents[j].angWidth);
            }
        }
        return;
    }

    if (t.kind == TRACKS_COLUMNS) {
        const double ulThick = qMax(1.0, g.ref * 0.006);
        const double padX = fm.height() * cfg.platePad;
        const double padY = fm.height() * cfg.platePad * 0.45;
        for (const ColumnRow &row : t.rows) {
            const double top = row.yCenter - t.lineH / 2.0;
            if (cfg.plateEnabled) {
                const double th = fm.height();
                drawRectPlate(p,
                              QRectF(row.blockLeft, row.yCenter - th / 2.0,
                                     t.blockW, th),
                              cfg, padX, padY, g.ref);
            }
            drawTextOutlined(p, QRectF(row.blockLeft, top, t.numW, t.lineH),
                             Qt::AlignRight | Qt::AlignVCenter, row.num, color,
                             outline, ow);
            drawTextOutlined(p,
                             QRectF(row.blockLeft + t.numW + t.gutter, top,
                                    t.titleSlot + 2.0, t.lineH),
                             Qt::AlignLeft | Qt::AlignVCenter, row.title,
                             color, outline, ow);
            if (cfg.trackUnderline) {
                p.setPen(QPen(color, ulThick));
                p.drawLine(QPointF(row.ulX0, row.ulY),
                           QPointF(row.ulX1, row.ulY));
            }
        }
        return;
    }

    // Table.
    if (cfg.plateEnabled) {
        const double padX = fm.height() * cfg.platePad;
        const double padY = fm.height() * cfg.platePad * 0.5;
        drawRectPlate(p,
                      QRectF(g.cx - t.tableRowW / 2.0, t.tableY0, t.tableRowW,
                             t.tableRows * t.tableLineH),
                      cfg, padX, padY, g.ref);
    }
    for (int i = 0; i < t.tableRows; ++i) {
        const double yc = t.tableY0 + (i + 0.5) * t.tableLineH;
        double x = g.cx - t.tableRowW / 2.0;
        for (int c = 0; c < t.columns.size(); ++c) {
            if (i < t.columns[c].size()) {
                const QString &text = t.columns[c][i];
                drawTextOutlined(p,
                                 QRectF(x, yc - t.tableLineH / 2.0, t.slotWidths[c],
                                        t.tableLineH),
                                 Qt::AlignLeft | Qt::AlignVCenter, text, color,
                                 outline, ow);
                if (cfg.trackUnderline) {
                    const double uy = yc + fm.height() * 0.42;
                    p.setPen(QPen(color, qMax(1.0, g.ref * 0.005)));
                    p.drawLine(QPointF(x, uy),
                               QPointF(x + fm.horizontalAdvance(text), uy));
                }
            }
            x += t.slotWidths[c] + t.tableGutter;
        }
    }
}

} // namespace render
