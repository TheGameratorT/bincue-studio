// Shared internals between the layout/text module (rendertext.cpp) and the
// painting pipeline (render.cpp). Not part of the public API.
#pragma once

#include <QFont>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

#include "labelconfig.h"

class QPainter;

namespace render {

inline const QString DEFAULT_FONT = QStringLiteral("DejaVu Sans");
inline constexpr double MAX_TEXT_ANGLE = 6.0213859194;   // 345° in radians

struct Geometry {
    double cx = 0, cy = 0;
    double holeR = 0;    // physical centre hole radius
    double innerR = 0;   // printable inner radius (artwork stays outside)
    double outerR = 0;   // printable outer radius (artwork stays inside)
    double ref = 0;      // scale reference for fonts (== outerR)
};

// One resolved title line: text + its size multiplier.
struct TitleLine {
    QString text;
    double mult = 1.0;
};

// A laid-out arc title line (baseline radius + font).
struct ArcTitleLine {
    QFont font;
    QString text;
    double radius = 0;
};

// A laid-out straight (banner/stack) line: font + the top of its line box and
// the chord width available to it at that height.
struct StackLine {
    QFont font;
    QString text;
    double y = 0;
    double availW = 0;
};

// A fully positioned row of the two-column track layout.
struct ColumnRow {
    QString num, title;
    double blockLeft = 0, yCenter = 0;
    double ulX0 = 0, ulX1 = 0, ulY = 0;   // underline span at ulY
};

struct TitleLayout {
    bool present = false;
    bool arc = true;
    // Arc:
    QVector<ArcTitleLine> arcLines;
    double centerDeg = 90.0;   // angular centre of the run (90 = top)
    double halfDeg = 0.0;      // widest line's half-arc, degrees
    double bottomR = 0.0;      // radius just inside the last line
    // Straight:
    QVector<StackLine> stack;
    double stackCx = 0;        // horizontal centre of the banner stack
    // Zone the title occupies (disc coords, not yet clipped to the ring); a
    // band rect for the straight layout, an annulus for the arc.
    QPainterPath zone;
    QRectF zoneRect;           // meaningful when zoneIsBand
    bool zoneIsBand = false;
};

// One arc track entry: text + its angular width (radians) on its ring.
struct ArcRingContent {
    QString text;
    double angWidth = 0;
};

struct TracksLayout {
    bool present = false;
    QString kind;              // TRACKS_ARC | TRACKS_COLUMNS | TRACKS_TABLE
    QFont font;
    // Arc:
    QVector<double> rings;                          // baseline radii, outer→inner
    QVector<QVector<ArcRingContent>> ringContents;
    double tracksOuterR = 0;
    // Columns:
    QVector<ColumnRow> rows;
    double numW = 0, gutter = 0, titleSlot = 0, blockW = 0, lineH = 0;
    // Table:
    QVector<QStringList> columns;
    QVector<double> slotWidths;     // per-column text width
    int tableRows = 0;
    double tableY0 = 0, tableRowW = 0, tableGutter = 0, tableLineH = 0;
    // Zone (see TitleLayout).
    QPainterPath zone;
    QRectF zoneRect;
    bool zoneIsBand = false;
};

struct Layout {
    Geometry g;
    TitleLayout title;
    TracksLayout tracks;
    double ringBand = 0;       // ring-feature band depth (px); 0 when off
};

QFont makeFont(const QString &family, double px, bool bold, bool italic);
QVector<TitleLine> resolveTitleLines(const QString &title,
                                     const LabelConfig &cfg);
QPainterPath printableRing(double cx, double cy, double innerR, double outerR);

// Lay out title and tracks (fonts fitted, every position resolved) without
// painting anything, so the cover layers can use the text zones first.
Layout computeLayout(const Geometry &g, const QString &title,
                     const QStringList &trackTitles, const LabelConfig &cfg,
                     bool ringFeature);

void drawTitle(QPainter &p, const Layout &lay, const LabelConfig &cfg);
void drawTracks(QPainter &p, const Layout &lay, const LabelConfig &cfg);

} // namespace render
