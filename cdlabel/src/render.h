// Label renderer: paints the full layer stack described by LabelConfig.
#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include "labelconfig.h"

class QPainter;

// Fixed internal layout resolution. The label is always laid out at this
// canvas size and the painter scaled to the target, so the font fitting and
// ring packing are identical between preview and export (just sharper).
inline constexpr double PREVIEW_CANVAS_PX = 1000.0;
inline constexpr int SAVE_DPI = 600;   // export resolution

struct RenderInput {
    QString title;
    QStringList trackTitles;
    QList<QImage> covers;   // full extracted list; cfg.coverOrder picks/orders
    QImage bgImage;         // custom background image (may be null)
};

// The covers actually used for drawing: the config's cover_order subset
// (invalid indices dropped), or all of them when the order is empty.
QList<QImage> orderedCovers(const QList<QImage> &covers, const LabelConfig &cfg);

// Paint a full label into a side×side box on `painter`. When `forPrint` is
// false (on-screen preview) the disc is filled white and faint alignment
// guides are drawn; when true (file export) the background stays transparent
// so only the wanted ink lands on the disc.
void drawLabel(QPainter &painter, double side, const RenderInput &in,
               const LabelConfig &cfg, bool forPrint);

// Render a print-ready label (transparent background) to a square image.
QImage renderLabelImage(const RenderInput &in, const LabelConfig &cfg,
                        int sidePx);
