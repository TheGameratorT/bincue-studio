// Disc geometry presets.
//
// The renderer is fully parametric: the physical disc diameter, centre-hole
// diameter and the printable outer/inner margins all live in LabelConfig and
// can be typed in freely for any printer's tray spec. These presets just
// pre-fill those four numbers for common media/printer combinations.
//
// The Epson XP-630 numbers come from Epson's printable-area spec for its
// "Print on CD/DVD" workflow: a 120 mm disc, quality assured between 43 mm
// (inner) and 116 mm (outer) diameters, printable at reduced quality from
// 18 mm out to 119 mm.
#pragma once

#include <QString>
#include <QVector>

struct MediaPreset {
    QString name;
    double discMm;    // physical disc diameter (also the square canvas size)
    double holeMm;    // centre hole diameter
    double outerMm;   // printable outer diameter (artwork stays inside)
    double innerMm;   // printable inner diameter (artwork stays outside)
};

inline const QVector<MediaPreset> &mediaPresets()
{
    static const QVector<MediaPreset> presets = {
        {"120 mm CD/DVD — Epson XP-630 (safe area)", 120.0, 15.0, 116.0, 43.0},
        {"120 mm CD/DVD — Epson XP-630 (maximum area)", 120.0, 15.0, 119.0, 18.0},
        {"120 mm CD/DVD — hub-printable disc", 120.0, 15.0, 118.0, 23.0},
        {"80 mm mini CD", 80.0, 15.0, 77.0, 24.0},
        {"80 mm mini CD — hub-printable", 80.0, 15.0, 79.0, 20.0},
    };
    return presets;
}
