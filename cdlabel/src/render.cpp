// Painting pipeline: cover placements (grid / spiral / ring / scatter), the
// background layers (backdrop, image, mosaic, panels, overlay band, waveform,
// hub) and the top-level drawLabel() layer stack.

#include "render.h"
#include "render_internal.h"

#include "covers.h"

#include <QCryptographicHash>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>

using namespace render;

namespace {

// Small deterministic RNG with Python-random-like helpers. Placement is
// seeded by the cover count so preview and export land identically and
// repaints don't reshuffle.
struct Rng {
    std::mt19937 gen;
    explicit Rng(quint32 seed) : gen(seed) {}
    double uniform(double a, double b)
    {
        std::uniform_real_distribution<double> d(a, b);
        return d(gen);
    }
    int randrange(int n)
    {
        std::uniform_int_distribution<int> d(0, qMax(0, n - 1));
        return d(gen);
    }
};

struct Placement {
    double x = 0, y = 0, angle = 0;
};

// ---- Cover assignment ------------------------------------------------------------

// Choose which cover fills each slot so copies of one cover stay far apart.
// Each slot takes the least-used cover and, among ties, the one whose nearest
// already-placed copy sits farthest away — so every distinct cover surfaces
// before any repeat and you never catch the same art twice in one glance.
QVector<int> assignSpread(const QVector<Placement> &placements, int ncov)
{
    QVector<int> counts(ncov, 0);
    QVector<QVector<QPointF>> positions(ncov);
    QVector<int> assign(placements.size(), 0);
    for (int i = 0; i < placements.size(); ++i) {
        const double gx = placements[i].x, gy = placements[i].y;
        int bestC = 0;
        std::tuple<int, double, int> bestKey;
        bool haveBest = false;
        for (int c = 0; c < ncov; ++c) {
            double nearest = std::numeric_limits<double>::infinity();
            for (const QPointF &pt : positions[c])
                nearest = qMin(nearest, (gx - pt.x()) * (gx - pt.x())
                                            + (gy - pt.y()) * (gy - pt.y()));
            // Least-used wins first; ties go to whoever is farthest from its
            // nearest existing copy.
            const std::tuple<int, double, int> key{counts[c], -nearest, c};
            if (!haveBest || key < bestKey) {
                bestKey = key;
                bestC = c;
                haveBest = true;
            }
        }
        assign[i] = bestC;
        counts[bestC]++;
        positions[bestC].append(QPointF(gx, gy));
    }
    return assign;
}

// Fraction of a tile centred at (gx, gy) that lands inside the printable
// annulus — i.e. how much of it will actually be seen after the ring clip.
double tileVisibility(double gx, double gy, double tile, double innerR,
                      double outerR)
{
    const double half = tile / 2.0;
    const int steps = 5;
    int inside = 0;
    for (int iy = 0; iy < steps; ++iy) {
        const double py = gy - half + (iy + 0.5) / steps * tile;
        for (int ix = 0; ix < steps; ++ix) {
            const double px = gx - half + (ix + 0.5) / steps * tile;
            const double d = std::hypot(px, py);
            if (innerR <= d && d <= outerR)
                ++inside;
        }
    }
    return double(inside) / (steps * steps);
}

// ---- Placements -----------------------------------------------------------------

// Neat axis-aligned tiles, edge to edge, filling the disc. Tiles the hub
// fully hides are dropped. `sequence` orders the slots in reading order and
// cycles the covers strictly; otherwise the most-visible slots pick first
// from the anti-clumping spread, so distinct art lands where it shows.
void gridPlacements(int ncov, double innerR, double outerR, double tile,
                    bool sequence, QVector<Placement> &placements,
                    QVector<int> &assign)
{
    const double step = tile;
    const double reach = outerR + tile * 0.35;
    const int n = int(std::ceil((2 * reach) / step));
    const double start = -((n - 1) * step) / 2.0;
    struct Scored {
        double x, y, vis;
    };
    QVector<Scored> scored;
    for (int iy = 0; iy < n; ++iy) {
        for (int ix = 0; ix < n; ++ix) {
            const double gx = start + ix * step;
            const double gy = start + iy * step;
            if (std::hypot(gx, gy) > reach)
                continue;
            const double vis = tileVisibility(gx, gy, tile, innerR, outerR);
            if (vis <= 0.0)
                continue;   // fully hidden by the hub
            scored.append({gx, gy, vis});
        }
    }
    if (sequence) {
        std::stable_sort(scored.begin(), scored.end(),
                         [](const Scored &a, const Scored &b) {
                             return a.y != b.y ? a.y < b.y : a.x < b.x;
                         });
        for (int i = 0; i < scored.size(); ++i) {
            placements.append({scored[i].x, scored[i].y, 0.0});
            assign.append(i % ncov);
        }
        return;
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const Scored &a, const Scored &b) {
                         return a.vis > b.vis;
                     });
    for (const Scored &s : scored)
        placements.append({s.x, s.y, 0.0});
    assign = assignSpread(placements, ncov);
}

// Covers wound outward along an Archimedean spiral (r = a·θ). Adjacent turns
// sit ~one step apart and tiles ~one step along the arc, so `overlap`
// controls the winding density.
void spiralPlacements(Rng &rng, int ncov, double innerR, double outerR,
                      double tile, double overlap, double jitter,
                      bool sequence, QVector<Placement> &placements,
                      QVector<int> &assign)
{
    const double step = tile * qMax(0.15, overlap);
    const double a = step / (2 * M_PI);   // radius gained per radian
    double r = qMax(innerR, tile * 0.25);
    double theta = 0.0;
    while (r <= outerR + tile * 0.35) {
        placements.append({r * std::cos(theta), r * std::sin(theta),
                           rng.uniform(-jitter, jitter)});
        const double dtheta = step / qMax(r, 1e-3);
        theta += dtheta;
        r += a * dtheta;
    }
    if (sequence) {
        for (int i = 0; i < placements.size(); ++i)
            assign.append(i % ncov);
    } else {
        assign = assignSpread(placements, ncov);
    }
}

// Fixed, evenly-spaced slots for every cover around a ring of `radius`, each
// turned to face the disc centre. With `hasGap`, the covers occupy the arc
// around a gap of 2·gapDeg centred at centerDeg (the title's spot); without
// it they spread evenly around the full ring.
QVector<Placement> ringPlacements(int ncov, double radius, bool hasGap,
                                  double centerDeg, double gapDeg)
{
    const int n = qMax(1, ncov);
    QVector<Placement> out;
    for (int k = 0; k < n; ++k) {
        double phi;
        if (!hasGap) {
            phi = 90.0 - 360.0 * k / n;
        } else {
            const double span = 360.0 - 2.0 * gapDeg;
            const double frac = n == 1 ? 0.0 : double(k) / (n - 1);
            phi = (centerDeg - gapDeg) - span * frac;
        }
        const double pr = qDegreesToRadians(phi);
        out.append({radius * std::cos(pr), -radius * std::sin(pr),
                    90.0 - phi});   // top of the cover faces outward
    }
    return out;
}

// Bridson blue-noise sampling: evenly spread points at least `rmin` apart,
// kept only where `valid` holds. Deterministic for a given rng.
QVector<QPointF> poissonDisk(double rmin, double xlo, double xhi, double ylo,
                             double yhi,
                             const std::function<bool(double, double)> &valid,
                             Rng &rng, int k = 30)
{
    QVector<QPointF> pts;
    if (rmin <= 0)
        return pts;
    const double cs = rmin / std::sqrt(2.0);
    const int gw = qMax(1, int(std::ceil((xhi - xlo) / cs)));
    const int gh = qMax(1, int(std::ceil((yhi - ylo) / cs)));
    QVector<int> grid(gw * gh, -1);
    QVector<int> active;

    const auto gcell = [&](double x, double y) {
        return QPoint(qBound(0, int((x - xlo) / cs), gw - 1),
                      qBound(0, int((y - ylo) / cs), gh - 1));
    };
    const auto fits = [&](double x, double y) {
        if (x < xlo || x > xhi || y < ylo || y > yhi || !valid(x, y))
            return false;
        const QPoint c = gcell(x, y);
        for (int jj = qMax(0, c.y() - 2); jj < qMin(gh, c.y() + 3); ++jj)
            for (int ii = qMax(0, c.x() - 2); ii < qMin(gw, c.x() + 3); ++ii) {
                const int idx = grid[jj * gw + ii];
                if (idx >= 0) {
                    const QPointF &p = pts[idx];
                    if ((p.x() - x) * (p.x() - x) + (p.y() - y) * (p.y() - y)
                        < rmin * rmin)
                        return false;
                }
            }
        return true;
    };
    const auto add = [&](double x, double y) {
        const QPoint c = gcell(x, y);
        grid[c.y() * gw + c.x()] = pts.size();
        active.append(pts.size());
        pts.append(QPointF(x, y));
    };

    bool seeded = false;
    for (int i = 0; i < 4000 && !seeded; ++i) {
        const double x = rng.uniform(xlo, xhi), y = rng.uniform(ylo, yhi);
        if (fits(x, y)) {
            add(x, y);
            seeded = true;
        }
    }
    if (!seeded)
        return pts;
    while (!active.isEmpty()) {
        const int a = rng.randrange(active.size());
        const QPointF o = pts[active[a]];
        bool placed = false;
        for (int i = 0; i < k && !placed; ++i) {
            const double ang = rng.uniform(0, 2 * M_PI);
            const double rad = rng.uniform(rmin, 2 * rmin);
            const double x = o.x() + std::cos(ang) * rad;
            const double y = o.y() + std::sin(ang) * rad;
            if (fits(x, y)) {
                add(x, y);
                placed = true;
            }
        }
        if (!placed)
            active.removeAt(a);
    }
    return pts;
}

// Pick n of pts as spread out as possible (farthest-point sampling), starting
// from the top-left one so the choice is deterministic.
QVector<QPointF> evenSubset(const QVector<QPointF> &pts, int n)
{
    if (pts.size() <= n)
        return pts;
    int start = 0;
    for (int i = 1; i < pts.size(); ++i)
        if (pts[i].y() < pts[start].y()
            || (pts[i].y() == pts[start].y() && pts[i].x() < pts[start].x()))
            start = i;
    QVector<QPointF> sel{pts[start]};
    QVector<double> d2(pts.size());
    for (int i = 0; i < pts.size(); ++i)
        d2[i] = (pts[i].x() - pts[start].x()) * (pts[i].x() - pts[start].x())
                + (pts[i].y() - pts[start].y()) * (pts[i].y() - pts[start].y());
    d2[start] = -1.0;
    while (sel.size() < n) {
        int j = 0;
        for (int i = 1; i < pts.size(); ++i)
            if (d2[i] > d2[j])
                j = i;
        sel.append(pts[j]);
        d2[j] = -1.0;
        for (int i = 0; i < pts.size(); ++i) {
            if (d2[i] >= 0) {
                const double dd =
                    (pts[i].x() - pts[j].x()) * (pts[i].x() - pts[j].x())
                    + (pts[i].y() - pts[j].y()) * (pts[i].y() - pts[j].y());
                d2[i] = qMin(d2[i], dd);
            }
        }
    }
    return sel;
}

// Scatter `ncov` points evenly through the printable disc, clear of the hole,
// the rim and the given avoid-rects (centre-relative). Returns the points and
// the largest tile that keeps them from overlapping — the natural cover size.
QPair<QVector<QPointF>, double>
scatterPlacements(int ncov, double innerR, double outerR,
                  const QVector<QRectF> &avoid, quint32 seed)
{
    if (ncov <= 0)
        return {{}, 0.0};
    const auto regionFor = [&](double t) {
        const double half = t * 0.45;
        return [=](double x, double y) {
            const double r = std::hypot(x, y);
            if (r < innerR + half || r > outerR - half)
                return false;
            for (const QRectF &rect : avoid)
                if (rect.adjusted(-half, -half, half, half).contains(x, y))
                    return false;
            return true;
        };
    };
    // Binary-search the largest tile whose spacing still lands at least
    // `ncov` points, so the covers come out as big and evenly spread as they
    // can. The sampler is re-seeded identically each probe so the search (and
    // the final layout) are deterministic.
    double lo = outerR * 0.04, hi = 2.0 * outerR;
    QVector<QPointF> bestPts;
    double bestT = 0.0;
    for (int i = 0; i < 20; ++i) {
        const double t = (lo + hi) / 2.0;
        Rng rng(seed);
        const QVector<QPointF> pts = poissonDisk(
            t, -outerR, outerR, -outerR, outerR, regionFor(t), rng);
        if (pts.size() >= ncov) {
            bestPts = pts;
            bestT = t;
            lo = t;
        } else {
            hi = t;
        }
    }
    if (bestPts.isEmpty()) {
        Rng rng(seed);
        bestT = lo;
        bestPts = poissonDisk(lo, -outerR, outerR, -outerR, outerR,
                              regionFor(lo), rng);
        if (bestPts.isEmpty())
            return {{}, 0.0};
    }
    return {evenSubset(bestPts, ncov), bestT};
}

// ---- Cover card (frame & shadow) -----------------------------------------------

// Paint a soft drop shadow and a white photo-frame border behind one cover.
// The painter must already be translated (and rotated) to the tile's centre.
void drawCoverCard(QPainter &p, double tile)
{
    if (tile <= 0.0)
        return;
    const double frameM = tile * 0.045;   // white border width
    const double fh = tile / 2.0 + frameM;
    const double off = tile * 0.03;       // shadow offset (down-right)
    // Largest/faintest first, so darker cores land on top → a soft penumbra.
    const struct {
        double grow;
        int alpha;
    } lobes[] = {{0.06, 18}, {0.03, 30}, {0.0, 44}};
    for (const auto &l : lobes) {
        const double r = fh + tile * l.grow;
        p.fillRect(QRectF(-r + off, -r + off, r * 2, r * 2),
                   QColor(0, 0, 0, l.alpha));
    }
    p.fillRect(QRectF(-fh, -fh, fh * 2, fh * 2), QColor(255, 255, 255));
}

// ---- Decorative layers ----------------------------------------------------------

void drawBackdrop(QPainter &p, double cx, double cy, double innerR,
                  double outerR, const LabelConfig &cfg)
{
    p.save();
    p.setClipPath(printableRing(cx, cy, innerR, outerR));
    const QRectF rect(cx - outerR, cy - outerR, outerR * 2, outerR * 2);
    if (cfg.backdropGradient && cfg.backdropGradientRadial) {
        QRadialGradient grad(QPointF(cx, cy), qMax(1.0, outerR));
        grad.setColorAt(0.0, QColor(cfg.backdropColor));
        grad.setColorAt(1.0, QColor(cfg.backdropColor2));
        p.fillRect(rect, grad);
    } else if (cfg.backdropGradient) {
        QLinearGradient grad(cx, cy - outerR, cx, cy + outerR);
        grad.setColorAt(0.0, QColor(cfg.backdropColor));
        grad.setColorAt(1.0, QColor(cfg.backdropColor2));
        p.fillRect(rect, grad);
    } else {
        p.fillRect(rect, QColor(cfg.backdropColor));
    }
    p.restore();
}

void drawHubFill(QPainter &p, double cx, double cy, double innerR,
                 const LabelConfig &cfg)
{
    p.save();
    p.setPen(Qt::NoPen);
    const QRectF rect(cx - innerR, cy - innerR, innerR * 2, innerR * 2);
    if (cfg.hubGradient && cfg.hubGradientRadial) {
        QRadialGradient grad(QPointF(cx, cy), qMax(1.0, innerR));
        grad.setColorAt(0.0, QColor(cfg.hubColor));
        grad.setColorAt(1.0, QColor(cfg.hubColor2));
        p.setBrush(grad);
    } else if (cfg.hubGradient) {
        QLinearGradient grad(cx, cy - innerR, cx, cy + innerR);
        grad.setColorAt(0.0, QColor(cfg.hubColor));
        grad.setColorAt(1.0, QColor(cfg.hubColor2));
        p.setBrush(grad);
    } else {
        p.setBrush(QColor(cfg.hubColor));
    }
    p.drawEllipse(rect);
    p.restore();
}

// Feathered translucent band over the background layers: a concentric ring
// following the disc, or a straight horizontal strip.
void drawOverlayBand(QPainter &p, double cx, double cy, double innerR,
                     double outerR, const LabelConfig &cfg)
{
    if (cfg.bandAlpha <= 0 || cfg.bandHeight <= 0)
        return;
    QColor core(cfg.bandColor);
    core.setAlpha(qBound(0, cfg.bandAlpha, 255));
    QColor edge(cfg.bandColor);
    edge.setAlpha(0);
    const double f = qBound(0.0, cfg.bandFeather, 0.5);
    p.save();
    p.setClipPath(printableRing(cx, cy, innerR, outerR));
    if (cfg.bandStyle == QStringLiteral("ring")) {
        // An annular band centred on the printable ring's mid-radius,
        // feathered along the radius.
        const double rmid = (innerR + outerR) / 2.0;
        const double half = outerR * qBound(0.0, cfg.bandHeight, 1.0);
        const double rIn = qMax(0.0, rmid - half);
        const double rOut = qMin(outerR, rmid + half);
        const QRectF rect(cx - rOut, cy - rOut, rOut * 2, rOut * 2);
        if (f <= 0.0 || rOut <= 0.0) {
            QPainterPath path;
            path.addEllipse(QPointF(cx, cy), rOut, rOut);
            path.addEllipse(QPointF(cx, cy), rIn, rIn);
            p.fillPath(path, core);
        } else {
            QRadialGradient grad(QPointF(cx, cy), rOut);
            const double t0 = rIn / rOut;
            const double fw = f * (1.0 - t0);
            grad.setColorAt(qBound(0.0, t0, 1.0), edge);
            grad.setColorAt(qBound(0.0, t0 + fw, 1.0), core);
            grad.setColorAt(qBound(0.0, 1.0 - fw, 1.0), core);
            grad.setColorAt(1.0, edge);
            p.fillRect(rect, grad);
        }
    } else {
        const double half = outerR * qBound(0.0, cfg.bandHeight, 1.0);
        const double top = cy - half, bot = cy + half;
        const QRectF rect(cx - outerR, top, outerR * 2, bot - top);
        if (f <= 0.0) {
            p.fillRect(rect, core);
        } else {
            QLinearGradient grad(cx, top, cx, bot);
            grad.setColorAt(0.0, edge);
            grad.setColorAt(f, core);
            grad.setColorAt(1.0 - f, core);
            grad.setColorAt(1.0, edge);
            p.fillRect(rect, grad);
        }
    }
    p.restore();
}

// Metallic ring hugging the centre hole: a conical light→base→dark sweep,
// repeated around the circle, fakes the sheen of chrome or foil.
void drawHubRing(QPainter &p, double cx, double cy, double holeR,
                 double innerR, const LabelConfig &cfg)
{
    const double gap = qMax(0.0, innerR - holeR);
    const double band = gap * qBound(0.0, cfg.hubRingWidth, 1.0);
    if (band <= 0.0)
        return;
    const double rOut = holeR + band;
    const QColor base(cfg.hubRingColor);
    const int spread = 100 + qBound(0, cfg.hubRingShine, 255) / 2;
    const QColor light = base.lighter(spread), dark = base.darker(spread);
    QConicalGradient grad(QPointF(cx, cy), 90.0);
    const struct {
        double t;
        const QColor &c;
    } stops[] = {{0.0, light}, {0.12, base}, {0.25, dark},  {0.38, base},
                 {0.5, light}, {0.62, base}, {0.75, dark},  {0.88, base},
                 {1.0, light}};
    for (const auto &s : stops)
        grad.setColorAt(s.t, s.c);
    QPainterPath path;
    path.addEllipse(QPointF(cx, cy), rOut, rOut);
    path.addEllipse(QPointF(cx, cy), holeR, holeR);
    p.save();
    p.setPen(Qt::NoPen);
    p.fillPath(path, QBrush(grad));
    p.restore();
}

// A smooth pseudo-waveform of n samples in [0, 1] that wraps seamlessly
// around the circle: periodic harmonics with amplitude falling off by
// frequency for an organic, audio-like profile.
QVector<double> waveformAmplitudes(int n, quint32 seed)
{
    Rng rng(seed);
    struct Harmonic {
        int k;
        double amp, phase;
    };
    QVector<Harmonic> harmonics;
    for (int k = 2; k < 18; ++k)
        harmonics.append({k, rng.uniform(0.3, 1.0) / std::pow(k, 0.75),
                          rng.uniform(0, 2 * M_PI)});
    QVector<double> raw(n);
    for (int i = 0; i < n; ++i) {
        double v = 0;
        for (const Harmonic &h : harmonics)
            v += h.amp * std::sin(h.k * (2 * M_PI * i / n) + h.phase);
        raw[i] = v;
    }
    const auto [mnIt, mxIt] = std::minmax_element(raw.begin(), raw.end());
    const double lo = *mnIt, hiV = *mxIt;
    if (hiV - lo < 1e-6)
        return QVector<double>(n, 0.5);
    for (double &v : raw)
        v = (v - lo) / (hiV - lo);
    return raw;
}

void drawWaveform(QPainter &p, double cx, double cy, double innerR,
                  double outerR, double centerR, double amp,
                  const LabelConfig &cfg, quint32 seed)
{
    const int bars = qMax(8, cfg.waveformBars);
    const int a = qBound(0, cfg.waveformAlpha, 255);
    if (a <= 0 || amp <= 0.0 || centerR <= 0.0)
        return;
    const QVector<double> amps = waveformAmplitudes(bars, seed);
    QColor col(cfg.waveformColor);
    col.setAlpha(a);
    QPen pen(col);
    pen.setWidthF(qMax(1.0, (2 * M_PI * centerR / bars) * 0.5));
    pen.setCapStyle(Qt::RoundCap);
    p.save();
    p.setClipPath(printableRing(cx, cy, innerR, outerR));
    p.setPen(pen);
    for (int i = 0; i < bars; ++i) {
        const double ang = 2 * M_PI * i / bars;
        const double half = amp * (0.15 + 0.85 * amps[i]);
        const double c = std::cos(ang), s = std::sin(ang);
        const double r0 = centerR - half, r1 = centerR + half;
        p.drawLine(QPointF(cx + c * r0, cy + s * r0),
                   QPointF(cx + c * r1, cy + s * r1));
    }
    p.restore();
}

// A single picture filling the printable disc as its background.
void drawImageBackground(QPainter &p, double cx, double cy, double innerR,
                         double outerR, const QImage &imgIn,
                         const LabelConfig &cfg)
{
    if (imgIn.isNull())
        return;
    const QImage img = processCover(imgIn, cfg.bgImageDesat, cfg.bgImageBlur,
                                    QStringLiteral("#000000"), 0);
    p.save();
    p.setClipPath(printableRing(cx, cy, innerR, outerR));
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const QRectF box(cx - outerR, cy - outerR, outerR * 2, outerR * 2);
    const double iw = img.width(), ih = img.height();
    QRectF target = box;
    if (cfg.bgImageFit != QStringLiteral("stretch") && iw > 0 && ih > 0) {
        // "cover" fills the square (largest scale); "contain" fits inside it.
        const double scale = cfg.bgImageFit == QStringLiteral("contain")
                                 ? qMin(box.width() / iw, box.height() / ih)
                                 : qMax(box.width() / iw, box.height() / ih);
        target = QRectF(cx - iw * scale / 2.0, cy - ih * scale / 2.0,
                        iw * scale, ih * scale);
    }
    p.drawImage(target, img);
    if (cfg.bgImageFade > 0)
        p.fillRect(box, QColor(255, 255, 255, qBound(0, cfg.bgImageFade, 255)));
    p.restore();
}

// ---- Cover background stage ------------------------------------------------------
//
// Renders the background mosaic (grid / spiral) onto its own oversized layer
// at the painter's device resolution, then stamps it down through the ring
// clip — blurred once as a whole, so the blur is uniform and softens the tile
// seams. The text panels re-use the same layer: a blurrier copy is drawn
// clipped to each panel zone, then the whole-disc fade washes everything
// evenly, then solid fills / tints / fades land on just the zones.

void paintCoverStage(QPainter &p, const Geometry &g, double bgInnerR,
                     double bgOuterR, const QList<QImage> &covers,
                     const LabelConfig &cfg, const Layout &lay)
{
    const QPainterPath ring =
        printableRing(g.cx, g.cy, bgInnerR, bgOuterR);
    QVector<QPainterPath> zones;
    if (cfg.panelTitle && lay.title.present && !lay.title.zone.isEmpty())
        zones.append(ring.intersected(lay.title.zone));
    if (cfg.panelTracks && !lay.tracks.zone.isEmpty())
        zones.append(ring.intersected(lay.tracks.zone));

    const bool mosaic = !covers.isEmpty() && cfg.coverBg != COVER_BG_NONE;
    const bool panelBlurWanted = mosaic && !zones.isEmpty()
                                 && cfg.panelMode == QStringLiteral("blur")
                                 && cfg.panelBlur > 0;

    if (mosaic) {
        QList<QImage> procd;
        for (const QImage &img : covers)
            procd.append(processCover(img, cfg.coverDesat, 0.0, cfg.coverTint,
                                      cfg.coverTintStrength));
        const int ncov = procd.size();
        const double tile = bgOuterR * qMax(0.2, cfg.coverScale);

        QVector<Placement> placements;
        QVector<int> assign;
        if (cfg.coverBg == COVER_BG_SPIRAL) {
            Rng rng(quint32(ncov * 7919 + 17));
            spiralPlacements(rng, ncov, bgInnerR, bgOuterR, tile,
                             cfg.coverOverlap, cfg.coverJitter,
                             cfg.coverSequence, placements, assign);
        } else {
            gridPlacements(ncov, bgInnerR, bgOuterR, tile, cfg.coverSequence,
                           placements, assign);
        }

        // Device scale, so the layer is rendered at output resolution and the
        // blur radius reads the same on the preview and a high-res export.
        const QTransform xf = p.transform();
        const double s = std::hypot(xf.m11(), xf.m21()) != 0.0
                             ? std::hypot(xf.m11(), xf.m21())
                             : 1.0;
        const double blurCanvas = qMax(0.0, cfg.coverBlur) * 2.0;
        const double panelBlurCanvas =
            panelBlurWanted ? (qMax(0.0, cfg.coverBlur) + cfg.panelBlur) * 2.0
                            : blurCanvas;
        const double layerR = bgOuterR + tile * 0.85
                              + qMax(blurCanvas, panelBlurCanvas) + 2.0;
        const int dev = qMax(1, int(std::ceil(2.0 * layerR * s)));
        QImage layer(dev, dev, QImage::Format_ARGB32);
        layer.fill(Qt::transparent);
        {
            QPainter lp(&layer);
            lp.setRenderHint(QPainter::SmoothPixmapTransform, true);
            lp.scale(dev / (2.0 * layerR), dev / (2.0 * layerR));
            for (int i = 0; i < placements.size(); ++i) {
                const Placement &pl = placements[i];
                lp.save();
                lp.translate(layerR + pl.x, layerR + pl.y);
                lp.rotate(pl.angle);
                if (cfg.coverFrame)
                    drawCoverCard(lp, tile);
                lp.drawImage(QRectF(-tile / 2, -tile / 2, tile, tile),
                             procd[assign[i]]);
                lp.restore();
            }
        }
        const QRectF dest(g.cx - layerR, g.cy - layerR, 2.0 * layerR,
                          2.0 * layerR);
        p.save();
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setClipPath(ring);
        p.drawImage(dest, blurCanvas > 0 ? blurImage(layer, blurCanvas * s)
                                         : layer);
        // Panel extra blur: a blurrier copy over just the zones, drawn before
        // the whole-disc fade so that wash still covers it evenly.
        if (panelBlurWanted) {
            const QImage blurred = blurImage(layer, panelBlurCanvas * s);
            for (const QPainterPath &zone : zones) {
                p.setClipPath(zone);
                p.drawImage(dest, blurred);
            }
        }
        // Whole-disc fade: washes the mosaic back so text stays readable.
        if (cfg.coverFade > 0) {
            QColor wash(cfg.coverFadeColor);
            wash.setAlpha(qBound(0, cfg.coverFade, 255));
            p.setClipPath(ring);
            p.fillRect(dest, wash);
        }
        p.restore();
    }

    // Solid panels and the zone tint/fade apply with or without a mosaic.
    if (zones.isEmpty())
        return;
    p.save();
    const QRectF discBox(g.cx - bgOuterR, g.cy - bgOuterR, 2.0 * bgOuterR,
                         2.0 * bgOuterR);
    for (const QPainterPath &zone : zones) {
        p.setClipPath(zone);
        if (cfg.panelMode == QStringLiteral("solid"))
            p.fillRect(discBox, QColor(cfg.panelColor));
        if (cfg.panelTintStrength > 0) {
            QColor tint(cfg.panelTint);
            tint.setAlpha(qBound(0, cfg.panelTintStrength, 255));
            p.fillRect(discBox, tint);
        }
        if (cfg.panelFade > 0) {
            QColor fade(cfg.coverFadeColor);
            fade.setAlpha(qBound(0, cfg.panelFade, 255));
            p.fillRect(discBox, fade);
        }
    }
    p.restore();
}

// ---- Feature covers ---------------------------------------------------------------

// Ring: one cover per fixed slot, centred in the outer band, each facing the
// disc centre, arcing around the title's wedge.
void drawRingCovers(QPainter &p, const Geometry &g, double bgInnerR,
                    double bgOuterR, const QList<QImage> &covers,
                    const LabelConfig &cfg, const Layout &lay)
{
    const double band = lay.ringBand;
    if (band <= 0 || covers.isEmpty())
        return;
    const double centerR = qMax(1.0, g.outerR - band / 2.0);
    const double tile = band * qBound(0.4, cfg.featureScale, 1.6);
    if (tile <= 0)
        return;

    bool hasGap = false;
    double centerDeg = 90.0, gapDeg = 0.0;
    // A cover's widest angular reach (its inner corners) is the minimum that
    // keeps the nearest covers just off the title; the slider adds extra.
    const double coverHalf = qRadiansToDegrees(
        std::atan2(tile / 2.0, qMax(1.0, centerR - tile / 2.0)));
    if (lay.title.present && lay.title.arc) {
        hasGap = true;
        centerDeg = lay.title.centerDeg;
        gapDeg = qMax(0.0, lay.title.halfDeg + coverHalf - 2.0)
                 + cfg.ringTitleGap;
    } else if (lay.title.present) {
        // Straight banner: covers must clear the band's bottom edge.
        hasGap = true;
        const double bandH = lay.title.zoneRect.bottom() - (g.cy - g.outerR);
        const double x = (g.outerR - bandH - tile / 2.0) / centerR;
        gapDeg = qRadiansToDegrees(std::acos(qBound(-1.0, x, 1.0)))
                 + cfg.ringTitleGap;
    }

    QList<QImage> cards;
    for (const QImage &img : covers)
        cards.append(processCover(squareCrop(img), 1.0, 0.0,
                                  QStringLiteral("#000000"), 0));

    p.save();
    p.setClipPath(printableRing(g.cx, g.cy, bgInnerR, bgOuterR));
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    Rng rng(quint32(covers.size() * 7919 + 17));
    const QVector<Placement> ringSlots =
        ringPlacements(covers.size(), centerR, hasGap, centerDeg, gapDeg);
    for (int i = 0; i < ringSlots.size() && i < cards.size(); ++i) {
        const double tilt = rng.uniform(-cfg.featureTilt, cfg.featureTilt);
        p.save();
        p.translate(g.cx + ringSlots[i].x, g.cy + ringSlots[i].y);
        p.rotate(ringSlots[i].angle + tilt);
        if (cfg.coverFrame)
            drawCoverCard(p, tile);
        p.drawImage(QRectF(-tile / 2, -tile / 2, tile, tile), cards[i]);
        p.restore();
    }
    p.restore();
}

// Scatter: covers spread evenly (blue-noise) through the printable disc,
// clear of the hole and — optionally — the band-shaped text zones.
void drawScatterCovers(QPainter &p, const Geometry &g, double bgInnerR,
                       double bgOuterR, const QList<QImage> &covers,
                       const LabelConfig &cfg, const Layout &lay)
{
    if (covers.isEmpty())
        return;
    QVector<QRectF> avoid;
    if (cfg.scatterAvoidText) {
        // Only band-shaped zones are avoided; ring-shaped ones (arc text)
        // would swallow the whole disc.
        if (lay.title.present && lay.title.zoneIsBand)
            avoid.append(lay.title.zoneRect.translated(-g.cx, -g.cy));
        if (lay.tracks.zoneIsBand && !lay.tracks.zoneRect.isNull())
            avoid.append(lay.tracks.zoneRect.translated(-g.cx, -g.cy));
    }
    const quint32 seed = quint32(covers.size() * 6151 + 29);
    const auto [pts, baseT] =
        scatterPlacements(covers.size(), qMax(bgInnerR, g.holeR), bgOuterR,
                          avoid, seed);
    if (pts.isEmpty() || baseT <= 0)
        return;
    const double tile = baseT * qBound(0.4, cfg.featureScale, 1.6);

    // Assign covers in reading order (rough rows, then left→right) so the eye
    // travels with the chosen order.
    QVector<int> order(pts.size());
    for (int i = 0; i < pts.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int rowA = int(std::lround(pts[a].y() / (baseT * 0.6)));
        const int rowB = int(std::lround(pts[b].y() / (baseT * 0.6)));
        return rowA != rowB ? rowA < rowB : pts[a].x() < pts[b].x();
    });

    QList<QImage> cards;
    for (const QImage &img : covers)
        cards.append(processCover(squareCrop(img), 1.0, 0.0,
                                  QStringLiteral("#000000"), 0));

    Rng rng(seed * 131 + 7);
    p.save();
    p.setClipPath(printableRing(g.cx, g.cy, bgInnerR, bgOuterR));
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (int k = 0; k < order.size() && k < cards.size(); ++k) {
        const QPointF &pt = pts[order[k]];
        p.save();
        p.translate(g.cx + pt.x(), g.cy + pt.y());
        p.rotate(rng.uniform(-cfg.featureTilt, cfg.featureTilt));
        if (cfg.coverFrame)
            drawCoverCard(p, tile);
        p.drawImage(QRectF(-tile / 2, -tile / 2, tile, tile), cards[k]);
        p.restore();
    }
    p.restore();
}

} // namespace

// ---- Public API -----------------------------------------------------------------------

QList<QImage> orderedCovers(const QList<QImage> &covers, const LabelConfig &cfg)
{
    if (covers.isEmpty())
        return {};
    if (cfg.coverOrder.isEmpty())
        return covers;
    QList<QImage> picked;
    for (int i : cfg.coverOrder)
        if (i >= 0 && i < covers.size())
            picked.append(covers[i]);
    return picked;
}

void drawLabel(QPainter &painter, double side, const RenderInput &in,
               const LabelConfig &cfg, bool forPrint)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // Sanitise the parametric geometry so a wild preset can't invert rings.
    const double discMm = qBound(40.0, cfg.discMm, 400.0);
    const double holeMm = qBound(4.0, cfg.holeMm, discMm * 0.5);
    const double innerMm = qBound(holeMm, cfg.printableInnerMm, discMm - 4.0);
    const double outerMm = qBound(innerMm + 4.0, cfg.printableOuterMm, discMm);

    Geometry g;
    g.cx = g.cy = side / 2.0;
    const double pxPerMm = side / discMm;
    g.holeR = (holeMm / 2.0) * pxPerMm;
    g.innerR = (innerMm / 2.0) * pxPerMm;
    g.outerR = (outerMm / 2.0) * pxPerMm;
    g.ref = g.outerR;

    const QList<QImage> covers =
        cfg.coversEnabled ? orderedCovers(in.covers, cfg) : QList<QImage>();
    const bool ringFeature =
        !covers.isEmpty() && cfg.coverFeature == COVER_FEAT_RING;

    const Layout lay =
        computeLayout(g, in.title, in.trackTitles, cfg, ringFeature);

    if (!forPrint) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(Qt::white));
        painter.drawEllipse(QRectF(0, 0, side, side));
    }

    // "background" hub mode lets the layers run in to the hole; a full bleed
    // lets them run out to the disc edge.
    const double bgInnerR =
        cfg.hubMode == QStringLiteral("background") ? 0.0 : g.innerR;
    const double bgOuterR = cfg.bleedEdge ? side / 2.0 : g.outerR;

    if (cfg.backdropEnabled)
        drawBackdrop(painter, g.cx, g.cy, bgInnerR, bgOuterR, cfg);

    if (cfg.bgImageEnabled && !in.bgImage.isNull())
        drawImageBackground(painter, g.cx, g.cy, bgInnerR, bgOuterR,
                            in.bgImage, cfg);

    paintCoverStage(painter, g, bgInnerR, bgOuterR, covers, cfg, lay);

    if (cfg.bandEnabled)
        drawOverlayBand(painter, g.cx, g.cy, bgInnerR, bgOuterR, cfg);

    if (cfg.waveformEnabled) {
        const QByteArray digest = QCryptographicHash::hash(
            in.trackTitles.join(QLatin1Char('\n')).toUtf8(),
            QCryptographicHash::Md5);
        const quint32 seed = quint32(digest.left(4).toHex().toUInt(nullptr, 16));
        drawWaveform(painter, g.cx, g.cy, bgInnerR, bgOuterR,
                     g.outerR * cfg.waveformRadius,
                     g.outerR * cfg.waveformAmplitude, cfg, seed);
    }

    if (cfg.hubMode == QStringLiteral("fill"))
        drawHubFill(painter, g.cx, g.cy, g.innerR, cfg);

    // Feature covers sit on top of every wash, but under the text.
    if (ringFeature)
        drawRingCovers(painter, g, bgInnerR, bgOuterR, covers, cfg, lay);
    else if (!covers.isEmpty() && cfg.coverFeature == COVER_FEAT_SCATTER)
        drawScatterCovers(painter, g, bgInnerR, bgOuterR, covers, cfg, lay);

    drawTitle(painter, lay, cfg);
    drawTracks(painter, lay, cfg);

    if (cfg.hubRingEnabled)
        drawHubRing(painter, g.cx, g.cy, g.holeR, g.innerR, cfg);

    // Punch the physical hole clear so no ink lands there.
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 255));
    painter.drawEllipse(
        QRectF(g.cx - g.holeR, g.cy - g.holeR, g.holeR * 2, g.holeR * 2));
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (!forPrint) {
        QPen pen(QColor(150, 150, 150), qMax(1.0, g.ref * 0.004));
        painter.setBrush(Qt::NoBrush);
        painter.setPen(pen);
        painter.drawEllipse(QRectF(0.5, 0.5, side - 1, side - 1));  // disc edge
        painter.drawEllipse(QRectF(g.cx - g.holeR, g.cy - g.holeR,
                                   g.holeR * 2, g.holeR * 2));
        pen.setStyle(Qt::DashLine);
        pen.setColor(QColor(200, 120, 120));
        painter.setPen(pen);
        painter.drawEllipse(QRectF(g.cx - g.innerR, g.cy - g.innerR,
                                   g.innerR * 2, g.innerR * 2));
        painter.drawEllipse(QRectF(g.cx - g.outerR, g.cy - g.outerR,
                                   g.outerR * 2, g.outerR * 2));
    }
}

QImage renderLabelImage(const RenderInput &in, const LabelConfig &cfg,
                        int sidePx)
{
    QImage image(sidePx, sidePx, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    // Lay the label out at the same fixed canvas the preview uses, then scale
    // up to the export resolution: the font-fit and ring packing depend on
    // the canvas size, so this keeps the export identical to the preview.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(sidePx / PREVIEW_CANVAS_PX, sidePx / PREVIEW_CANVAS_PX);
    drawLabel(painter, PREVIEW_CANVAS_PX, in, cfg, true);
    painter.end();
    return image;
}
