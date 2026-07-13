#include "programaudio.h"

#include "redbook.h"
#include "toolpaths.h"

#include <QFileInfo>
#include <QProcess>

using namespace redbook;

namespace programaudio {

QByteArray decode(const QString &sourcePath, QString *error)
{
    QProcess proc;
    proc.start(resolveMediaTool(QStringLiteral("ffmpeg")),
               {QStringLiteral("-y"), QStringLiteral("-v"),
                QStringLiteral("error"), QStringLiteral("-i"), sourcePath,
                QStringLiteral("-ar"), QStringLiteral("44100"),
                QStringLiteral("-ac"), QStringLiteral("2"),
                QStringLiteral("-f"), QStringLiteral("s16le"),
                QStringLiteral("-acodec"), QStringLiteral("pcm_s16le"),
                QStringLiteral("-")});
    proc.closeWriteChannel();
    if (!proc.waitForFinished(-1) || proc.exitStatus() != QProcess::NormalExit
        || proc.exitCode() != 0) {
        if (error) {
            QString err = QString::fromUtf8(proc.readAllStandardError());
            *error = err.trimmed().isEmpty() ? proc.errorString() : err;
        }
        return {};
    }
    if (error)
        error->clear();
    return proc.readAllStandardOutput();
}

QByteArray fitGap(QByteArray pcm, const Track &track, bool isLast,
                  qint64 gapFrames)
{
    // Align the decoded audio to a whole CD sector, then trim or fill its
    // baked-in trailing silence so the actual gap after this track equals the
    // inter-track gap (0 after the last).
    const qsizetype remainder = pcm.size() % BYTES_PER_FRAME;
    if (remainder)
        pcm.append(BYTES_PER_FRAME - remainder, '\0');

    const qint64 desiredGap = isLast ? 0 : gapFrames;
    const qint64 bakedFrames = secondsToFrames(track.bakedInGap);
    const qint64 delta = desiredGap - bakedFrames;
    if (delta < 0) {
        // Trim excess trailing silence (never past the start).
        const qint64 trim = qMin(-delta, qint64(pcm.size()) / BYTES_PER_FRAME);
        pcm.chop(trim * BYTES_PER_FRAME);
    } else if (delta > 0) {
        // Pad with silence up to the target gap.
        pcm.append(delta * BYTES_PER_FRAME, '\0');
    }
    return pcm;
}

} // namespace programaudio
