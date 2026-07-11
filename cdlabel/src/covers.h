// Cover-art extraction and processing.
//
// Embedded front covers are pulled from the source audio files' tags (via
// TagLib, when built with it) and de-duplicated — a normal album repeats one
// cover; a mixtape carries many distinct ones. A file with no embedded art
// falls back to a sidecar image in its folder (cover.jpg and friends).
//
// Desaturation/blur/tint are applied at draw time (and cached), so the
// sidebar's sliders can be tuned live without re-reading the source files.
#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

// The distinct covers for the given audio files, in first-seen order.
QList<QImage> extractCovers(const QStringList &paths);

// The single cover for one audio file (first embedded picture, else a sidecar
// image beside it), as ARGB32; a null image when the file carries no art. Used
// to keep each track associated with its own cover for the per-track panel.
QImage extractCover(const QString &path);

// Blend toward greyscale, keeping `keep` (0..1) of the original colour.
QImage desaturate(const QImage &img, double keep);

// True Gaussian blur of `radius` pixels (no-op when radius <= 0).
QImage blurImage(const QImage &img, double radius);

// Wash `color` at `strength` (0-255) alpha over the image, keeping its shape.
QImage tintImage(const QImage &img, const QColor &color, int strength);

// Desaturate + blur + tint, memoised on (image, knobs) so slider drags don't
// reprocess the whole pile every frame.
QImage processCover(const QImage &img, double keep, double blurPx,
                    const QString &tint, int tintStrength);

// Centre-crop to a square so a cover fills its tile without distortion.
QImage squareCrop(const QImage &img);
