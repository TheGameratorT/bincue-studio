#include "burncontroller.h"

#include <QRegularExpression>

BurnController::BurnController(QObject *parent)
    : hostkit::HostProcess(QStringLiteral("cdrdao"), parent)
{
}

QStringList BurnController::writeArgs(const QString &device,
                                     const QStringList &userOptions,
                                     const QString &tocPath)
{
    QStringList args;
    args << QStringLiteral("write");
    args << userOptions;
    args << QStringLiteral("--device") << device;
    args << QStringLiteral("--swap");     // image is little-endian; cdrdao raw wants big
    args << QStringLiteral("-n");         // don't wait for a keypress before writing
    args << QStringLiteral("-v") << QStringLiteral("2");  // emit progress lines
    args << tocPath;                      // must come last
    return args;
}

void BurnController::resetState()
{
    m_progress = Progress{};
    m_simulated = false;
    emit progressChanged();
}

void BurnController::handleLine(const QString &line)
{
    // cdrdao redraws a status line as it writes, e.g.
    //   "Wrote 12 of 45 MB (Buffer 100%)."
    static const QRegularExpression wrote(
        QStringLiteral("Wrote\\s+(\\d+)\\s+of\\s+(\\d+)\\s+MB"));
    const QRegularExpressionMatch m = wrote.match(line);
    if (m.hasMatch()) {
        m_progress.valid = true;
        m_progress.wroteMb = m.captured(1).toInt();
        m_progress.totalMb = m.captured(2).toInt();
        emit progressChanged();
        return;
    }
    if (line.contains(QStringLiteral("Simulation"), Qt::CaseInsensitive))
        m_simulated = true;
}

QString BurnController::successSummary() const
{
    return m_simulated
               ? QStringLiteral("Simulation finished — no disc was written.")
               : QStringLiteral("Disc written successfully.");
}

QString BurnController::stoppedSummary() const
{
    // A burn can't be resumed: interrupting cdrdao mid-write leaves the disc
    // partially written and unusable.
    return QStringLiteral(
        "Burn aborted — the disc is likely unusable (a burn can't be resumed).");
}
