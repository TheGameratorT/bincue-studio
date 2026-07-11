#include "redbook.h"

#include <QRegularExpression>

#include <cmath>

namespace redbook {

qint64 secondsToFrames(double seconds)
{
    return qint64(std::llround(seconds * FRAME_RATE));
}

QString framesToTimestamp(qint64 frames)
{
    const qint64 minutes = frames / (FRAME_RATE * 60);
    const qint64 seconds = (frames / FRAME_RATE) % 60;
    const qint64 frs = frames % FRAME_RATE;
    return QStringLiteral("%1:%2:%3")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(frs, 2, 10, QLatin1Char('0'));
}

QString secondsToMmss(double seconds)
{
    const qint64 s = qint64(std::llround(seconds));
    return QStringLiteral("%1:%2")
        .arg(s / 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString normalizeIsrc(const QString &value)
{
    static const QRegularExpression junk(QStringLiteral("[\\s-]"));
    static const QRegularExpression valid(QStringLiteral("^[A-Z0-9]{12}$"));
    QString cleaned = value.toUpper();
    cleaned.remove(junk);
    return valid.match(cleaned).hasMatch() ? cleaned : QString();
}

QString normalizeCatalog(const QString &value)
{
    static const QRegularExpression junk(QStringLiteral("[\\s-]"));
    static const QRegularExpression upcA(QStringLiteral("^\\d{12}$"));
    static const QRegularExpression ean13(QStringLiteral("^\\d{13}$"));
    QString cleaned = value;
    cleaned.remove(junk);
    if (upcA.match(cleaned).hasMatch())
        cleaned.prepend(QLatin1Char('0'));
    return ean13.match(cleaned).hasMatch() ? cleaned : QString();
}

} // namespace redbook
