#include "track.h"

#include <QDir>

qint64 Track::frames() const
{
    return redbook::secondsToFrames(durationSeconds);
}

QJsonObject Track::toJson() const
{
    QJsonObject o;
    // Store paths with forward slashes so projects are portable across OSes
    // (a no-op on Unix; rewrites Windows backslashes).
    o[QStringLiteral("source_path")] = QDir::fromNativeSeparators(sourcePath);
    o[QStringLiteral("title")] = title;
    o[QStringLiteral("performer")] = performer;
    o[QStringLiteral("songwriter")] = songwriter;
    o[QStringLiteral("composer")] = composer;
    o[QStringLiteral("arranger")] = arranger;
    o[QStringLiteral("message")] = message;
    o[QStringLiteral("isrc")] = isrc;
    o[QStringLiteral("copy_permitted")] = copyPermitted;
    o[QStringLiteral("pre_emphasis")] = preEmphasis;
    o[QStringLiteral("four_channel")] = fourChannel;
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
    t.composer = o.value(QStringLiteral("composer")).toString();
    t.arranger = o.value(QStringLiteral("arranger")).toString();
    t.message = o.value(QStringLiteral("message")).toString();
    t.isrc = o.value(QStringLiteral("isrc")).toString();
    t.copyPermitted = o.value(QStringLiteral("copy_permitted")).toBool();
    t.preEmphasis = o.value(QStringLiteral("pre_emphasis")).toBool();
    t.fourChannel = o.value(QStringLiteral("four_channel")).toBool();
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
    qint64 total = tracks.isEmpty() ? 0 : pregapFrames;  // track 1 pre-gap
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
