#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include "redbook.h"

struct Track {
    QString sourcePath;
    QString title;
    QString performer;
    // Less-visible CD-Text pack types, edited in the per-track details dialog
    // rather than the main table (see TrackDetailsDialog).
    QString songwriter;
    QString composer;
    QString arranger;
    QString message;  // free-text CD-Text message shown by some players
    QString isrc;     // International Standard Recording Code (12 chars)
    // Red Book subchannel flags (all default to the common case). copyPermitted
    // clears the copy-prohibit bit; preEmphasis marks 50/15µs pre-emphasised
    // audio; fourChannel marks quadraphonic instead of stereo.
    bool copyPermitted = false;
    bool preEmphasis = false;
    bool fourChannel = false;
    double durationSeconds = 0.0;
    // How much trailing silence the source file already has, in seconds. The
    // exporter trims or fills this so the actual gap matches the disc gap.
    double bakedInGap = redbook::DEFAULT_BAKED_GAP_SECONDS;

    qint64 frames() const;

    // The JSON shape is shared with saved projects, the legacy Python app and
    // cdlabel's --project loader; keys stay snake_case.
    QJsonObject toJson() const;
    static Track fromJson(const QJsonObject &obj);
};

// Total CD frames after trimming/filling each track's baked-in gap to the
// inter-track gap (0 after the last track), plus track 1's pre-gap.
qint64 totalProgramFrames(const QList<Track> &tracks, double pregapSeconds,
                          double gapSeconds);
