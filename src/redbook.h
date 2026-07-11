#pragma once

#include <QString>

// Red Book (CD-DA) constants and the small conversions shared by the UI and
// the exporter.
namespace redbook {

constexpr int FRAME_RATE = 75;         // CD "frames" (sectors) per second
constexpr int BYTES_PER_FRAME = 2352;  // raw CD-DA audio sector size

constexpr double DEFAULT_PREGAP_SECONDS = 2.0;  // track 1 lead-in PREGAP
constexpr double REDBOOK_PREGAP_SECONDS = 2.0;  // the standard lead-in; anything else is off-spec
constexpr double DEFAULT_GAP_SECONDS = 2.0;     // gap every track ends up with (0 after the last)
constexpr double DEFAULT_BAKED_GAP_SECONDS = 2.0;  // trailing silence a fresh source is assumed to have

qint64 secondsToFrames(double seconds);
QString framesToTimestamp(qint64 frames);  // MM:SS:FF as the cue expects
QString secondsToMmss(double seconds);

// Return a bare 12-character ISRC (letters/digits, upper-cased) or "" if the
// input isn't a valid ISRC. The human-readable form is often written with
// hyphens (CC-XXX-YY-NNNNN); those and any spaces are stripped. Burners
// reject a malformed ISRC, so only one we can vouch for is emitted.
QString normalizeIsrc(const QString &value);

// Return a bare 13-digit Media Catalog Number (MCN) or "" if invalid. A
// 12-digit UPC-A is accepted and left-padded with a zero to the EAN-13 form
// the cue's CATALOG field expects.
QString normalizeCatalog(const QString &value);

} // namespace redbook
