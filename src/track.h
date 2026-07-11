#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

#include "redbook.h"

struct Track {
    QString sourcePath;
    QString title;
    QString performer;
    QString songwriter;
    QString isrc;  // International Standard Recording Code (12 chars)
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
// inter-track gap (0 after the last track), plus track 1's lead-in pre-gap.
qint64 totalProgramFrames(const QList<Track> &tracks, double pregapSeconds,
                          double gapSeconds);
