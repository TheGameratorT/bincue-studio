#include "track.h"

qint64 Track::frames() const
{
    return redbook::secondsToFrames(durationSeconds);
}

QJsonObject Track::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("source_path")] = sourcePath;
    o[QStringLiteral("title")] = title;
    o[QStringLiteral("performer")] = performer;
    o[QStringLiteral("songwriter")] = songwriter;
    o[QStringLiteral("isrc")] = isrc;
    o[QStringLiteral("duration_seconds")] = durationSeconds;
    o[QStringLiteral("baked_in_gap")] = bakedInGap;
    return o;
}

Track Track::fromJson(const QJsonObject &o)
{
    Track t;
    t.sourcePath = o.value(QStringLiteral("source_path")).toString();
    t.title = o.value(QStringLiteral("title")).toString();
    t.performer = o.value(QStringLiteral("performer")).toString();
    t.songwriter = o.value(QStringLiteral("songwriter")).toString();
    t.isrc = o.value(QStringLiteral("isrc")).toString();
    t.durationSeconds = o.value(QStringLiteral("duration_seconds")).toDouble();
    t.bakedInGap =
        o.value(QStringLiteral("baked_in_gap")).toDouble(t.bakedInGap);
    return t;
}

qint64 totalProgramFrames(const QList<Track> &tracks, double pregapSeconds,
                          double gapSeconds)
{
    const qint64 pregapFrames = redbook::secondsToFrames(pregapSeconds);
    const qint64 gapFrames = redbook::secondsToFrames(gapSeconds);
    qint64 total = tracks.isEmpty() ? 0 : pregapFrames;  // track 1 lead-in
    for (qsizetype i = 0; i < tracks.size(); ++i) {
        const Track &t = tracks[i];
        const bool isLast = (i == tracks.size() - 1);
        const qint64 bakedFrames = redbook::secondsToFrames(t.bakedInGap);
        const qint64 desiredGap = isLast ? 0 : gapFrames;
        // audio without its baked-in tail, then the normalised gap
        total += qMax<qint64>(0, t.frames() - bakedFrames) + desiredGap;
    }
    return total;
}
