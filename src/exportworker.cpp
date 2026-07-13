#include "exportworker.h"
#include "toolpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

using namespace redbook;

ExportWorker::ExportWorker(Params params, QObject *parent)
    : QThread(parent), m_params(std::move(params))
{
}

void ExportWorker::run()
{
    QTemporaryDir tmpDir(QDir::temp().filePath(QStringLiteral("bincue_XXXXXX")));
    if (!tmpDir.isValid()) {
        emit failed(tr("Could not create a temporary directory."));
        return;
    }

    const qint64 pregapFrames = secondsToFrames(m_params.pregapSeconds);
    const qint64 gapFrames = secondsToFrames(m_params.gapSeconds);

    QStringList cue;
    const QString catalog = normalizeCatalog(m_params.albumCatalog);
    if (!catalog.isEmpty()) {
        // Media Catalog Number must precede the first FILE/TRACK.
        cue << QStringLiteral("CATALOG %1").arg(catalog);
    }
    if (!m_params.albumPerformer.isEmpty())
        cue << QStringLiteral("PERFORMER \"%1\"").arg(m_params.albumPerformer);
    if (!m_params.albumTitle.isEmpty())
        cue << QStringLiteral("TITLE \"%1\"").arg(m_params.albumTitle);
    if (!m_params.albumSongwriter.isEmpty())
        cue << QStringLiteral("SONGWRITER \"%1\"").arg(m_params.albumSongwriter);
    if (!m_params.albumGenre.isEmpty())
        cue << QStringLiteral("REM GENRE \"%1\"").arg(m_params.albumGenre);
    if (!m_params.albumYear.isEmpty())
        cue << QStringLiteral("REM DATE \"%1\"").arg(m_params.albumYear);
    cue << QStringLiteral("FILE \"%1.bin\" BINARY").arg(m_params.baseName);

    const QString tmpBin = tmpDir.filePath(QStringLiteral("image.bin"));
    QFile binOut(tmpBin);
    if (!binOut.open(QIODevice::WriteOnly)) {
        emit failed(binOut.errorString());
        return;
    }

    // Per-track facts for the cdrdao TOC, captured as the image is built.
    QList<TocEntry> tocEntries;

    qint64 offsetFrames = 0;
    qint64 prevStart = -1;  // INDEX 01 frame of the previous track
    const int total = int(m_params.tracks.size());

    for (int i = 0; i < total; ++i) {
        const Track &track = m_params.tracks[i];
        const int trackNum = i + 1;
        const bool isLast = (trackNum == total);
        emit progress(trackNum, total,
                      tr("Encoding: %1")
                          .arg(QFileInfo(track.sourcePath).fileName()));

        QProcess proc;
        proc.start(resolveMediaTool(QStringLiteral("ffmpeg")),
                   {QStringLiteral("-y"), QStringLiteral("-v"),
                    QStringLiteral("error"), QStringLiteral("-i"),
                    track.sourcePath, QStringLiteral("-ar"),
                    QStringLiteral("44100"), QStringLiteral("-ac"),
                    QStringLiteral("2"), QStringLiteral("-f"),
                    QStringLiteral("s16le"), QStringLiteral("-acodec"),
                    QStringLiteral("pcm_s16le"), QStringLiteral("-")});
        proc.closeWriteChannel();
        if (!proc.waitForFinished(-1)
            || proc.exitStatus() != QProcess::NormalExit
            || proc.exitCode() != 0) {
            QString error = QString::fromUtf8(proc.readAllStandardError());
            if (error.trimmed().isEmpty())
                error = proc.errorString();
            emit failed(tr("ffmpeg failed:\n%1").arg(error));
            return;
        }
        QByteArray pcm = proc.readAllStandardOutput();

        // Align the decoded audio to a whole CD sector, then trim or fill its
        // baked-in trailing silence so the actual gap after this track equals
        // the inter-track gap (0 after the last).
        const qsizetype remainder = pcm.size() % BYTES_PER_FRAME;
        if (remainder)
            pcm.append(BYTES_PER_FRAME - remainder, '\0');

        const qint64 desiredGap = isLast ? 0 : gapFrames;
        const qint64 bakedFrames = secondsToFrames(track.bakedInGap);
        const qint64 delta = desiredGap - bakedFrames;
        if (delta < 0) {
            // Trim excess trailing silence (never past the start).
            const qint64 trim =
                qMin(-delta, qint64(pcm.size()) / BYTES_PER_FRAME);
            pcm.chop(trim * BYTES_PER_FRAME);
        } else if (delta > 0) {
            // Pad with silence up to the target gap.
            pcm.append(delta * BYTES_PER_FRAME, '\0');
        }

        cue << QStringLiteral("  TRACK %1 AUDIO")
                   .arg(trackNum, 2, 10, QLatin1Char('0'));
        cue << QStringLiteral("    TITLE \"%1\"").arg(track.title);
        if (!track.performer.isEmpty())
            cue << QStringLiteral("    PERFORMER \"%1\"").arg(track.performer);
        if (!track.songwriter.isEmpty())
            cue << QStringLiteral("    SONGWRITER \"%1\"")
                       .arg(track.songwriter);
        const QString isrc = normalizeIsrc(track.isrc);
        if (!isrc.isEmpty())
            cue << QStringLiteral("    ISRC %1").arg(isrc);

        if (trackNum == 1) {
            // Mandatory Red Book lead-in pregap, generated by the burner, so
            // it is not stored in the image.
            cue << QStringLiteral("    PREGAP %1")
                       .arg(framesToTimestamp(pregapFrames));
        } else {
            // The previous track's trailing silence is now exactly one
            // inter-track gap long; mark it as this track's pregap.
            const qint64 pregap = offsetFrames - gapFrames;
            if (prevStart >= 0 && pregap > prevStart)
                cue << QStringLiteral("    INDEX 00 %1")
                           .arg(framesToTimestamp(pregap));
        }

        cue << QStringLiteral("    INDEX 01 %1")
                   .arg(framesToTimestamp(offsetFrames));

        if (m_params.writeToc)
            tocEntries.append({offsetFrames, track.title, track.performer,
                               track.songwriter, isrc});

        prevStart = offsetFrames;

        if (binOut.write(pcm) != pcm.size()) {
            emit failed(binOut.errorString());
            return;
        }
        offsetFrames += qint64(pcm.size()) / BYTES_PER_FRAME;
    }
    binOut.close();

    const QDir outDir(m_params.outDir);
    const QString finalBin = outDir.filePath(m_params.baseName
                                             + QStringLiteral(".bin"));
    const QString finalCue = outDir.filePath(m_params.baseName
                                             + QStringLiteral(".cue"));
    if (QFile::exists(finalBin))
        QFile::remove(finalBin);
    // QFile::rename falls back to copy + delete across filesystems.
    if (!QFile::rename(tmpBin, finalBin)) {
        emit failed(tr("Could not move the image to %1").arg(finalBin));
        return;
    }
    QFile cueFile(finalCue);
    if (!cueFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(cueFile.errorString());
        return;
    }
    cueFile.write((cue.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
    cueFile.close();

    QString finalToc;
    if (m_params.writeToc) {
        finalToc = outDir.filePath(m_params.baseName + QStringLiteral(".toc"));
        const QString toc = buildToc(tocEntries, offsetFrames, pregapFrames,
                                     gapFrames, catalog, finalBin);
        QFile tocFile(finalToc);
        if (!tocFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit failed(tocFile.errorString());
            return;
        }
        tocFile.write(toc.toUtf8());
        tocFile.close();
    }

    emit finishedOk(finalBin, finalCue, finalToc);
}

// Assemble a cdrdao TOC describing the just-written image. cdrdao can't read an
// audio cue, so we emit the equivalent TOC ourselves from the same offsets the
// cue used. Each track's FILE region is contiguous in the image; a non-final
// track's trailing gap becomes the next track's pregap (START), and track 1's
// lead-in is a generated PREGAP that isn't stored in the image.
//
// The image is raw 16-bit little-endian PCM; cdrdao assumes big-endian for raw
// FILEs, so the burn must pass --swap. The FILE path is a bare filename (like the
// cue's) so the .bin/.cue/.toc stay portable when copied together; cdrdao resolves
// it against its working directory, so run cdrdao from the folder holding them.
QString ExportWorker::buildToc(const QList<TocEntry> &entries, qint64 totalFrames,
                               qint64 pregapFrames, qint64 gapFrames,
                               const QString &catalog, const QString &binPath)
{
    auto q = [](const QString &s) {
        QString e = s;
        e.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        e.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return QLatin1Char('"') + e + QLatin1Char('"');
    };

    const int n = int(entries.size());
    const QString binName = QFileInfo(binPath).fileName();

    // Where each track's data (its INDEX 00) begins in the image. Track 1 has no
    // stored pregap; later tracks start one gap before their INDEX 01, clamped
    // so a track shorter than the gap can't push a boundary backwards.
    QList<qint64> boundary(n, 0);
    for (int i = 1; i < n; ++i)
        boundary[i] = qMax(entries[i].startFrame - gapFrames, boundary[i - 1]);

    QStringList out;
    out << QStringLiteral("CD_DA");
    out << QString();
    if (!catalog.isEmpty())
        out << QStringLiteral("CATALOG %1").arg(q(catalog));

    // cdrdao's checkCdTextData() requires each CD-Text pack type to appear
    // exactly nofTracks+1 times (once per track AND once for the disc); a field
    // present on only some tracks makes the TOC "not suitable for this drive".
    // On top of that, once any CD-Text language block exists at all, TITLE and
    // PERFORMER are mandatory (a count of 0 is itself an error) while the rest
    // are optional. So: decide whether we're writing CD-Text, and if so always
    // emit TITLE and PERFORMER on the disc and every track, plus SONGWRITER only
    // when it's in play, filling every gap below with an empty string.
    bool anyTrackText = false, anyTrackSongwriter = false;
    for (const TocEntry &e : entries) {
        anyTrackText |= !e.title.isEmpty() || !e.performer.isEmpty();
        anyTrackSongwriter |= !e.songwriter.isEmpty();
    }
    const bool useSongwriter =
        !m_params.albumSongwriter.isEmpty() || anyTrackSongwriter;
    const bool haveCdText = !m_params.albumTitle.isEmpty()
        || !m_params.albumPerformer.isEmpty() || anyTrackText || useSongwriter;

    // Disc-wide CD-Text from the album fields, mirroring the cue's header.
    if (haveCdText) {
        out << QStringLiteral("CD_TEXT {");
        out << QStringLiteral("  LANGUAGE_MAP { 0 : EN }");
        out << QStringLiteral("  LANGUAGE 0 {");
        out << QStringLiteral("    TITLE %1").arg(q(m_params.albumTitle));
        out << QStringLiteral("    PERFORMER %1").arg(q(m_params.albumPerformer));
        if (useSongwriter)
            out << QStringLiteral("    SONGWRITER %1")
                       .arg(q(m_params.albumSongwriter));
        out << QStringLiteral("  }");
        out << QStringLiteral("}");
    }

    for (int i = 0; i < n; ++i) {
        const TocEntry &e = entries[i];
        const qint64 start = boundary[i];
        const qint64 end = (i + 1 < n) ? boundary[i + 1] : totalFrames;
        const qint64 len = end - start;
        const qint64 pregap = (i == 0) ? 0 : entries[i].startFrame - start;

        out << QString();
        out << QStringLiteral("// Track %1").arg(i + 1);
        out << QStringLiteral("TRACK AUDIO");

        // cdrdao's grammar fixes the order of a track's modifiers: ISRC first,
        // then CD_TEXT, and only after that the data statements (PREGAP / FILE /
        // START). Emitting CD_TEXT before ISRC is a syntax error.
        // Subchannel ISRC is genuinely per-track optional (no all-or-nothing
        // rule, unlike the CD-Text packs above), so emit whichever tracks have
        // one. It can't be blank-filled anyway: cdrdao validates it as an exact
        // 12-char code, so an empty ISRC would be rejected outright.
        if (!e.isrc.isEmpty())
            out << QStringLiteral("ISRC %1").arg(q(e.isrc));

        // The same field set must appear on every track. Performer and
        // songwriter fall back to the album value (a track is by the album
        // artist unless it says otherwise); a missing title falls back to an
        // empty string rather than the album title, which isn't a track name.
        if (haveCdText) {
            out << QStringLiteral("CD_TEXT {");
            out << QStringLiteral("  LANGUAGE 0 {");
            out << QStringLiteral("    TITLE %1").arg(q(e.title));
            out << QStringLiteral("    PERFORMER %1")
                       .arg(q(e.performer.isEmpty() ? m_params.albumPerformer
                                                    : e.performer));
            if (useSongwriter)
                out << QStringLiteral("    SONGWRITER %1")
                           .arg(q(e.songwriter.isEmpty() ? m_params.albumSongwriter
                                                         : e.songwriter));
            out << QStringLiteral("  }");
            out << QStringLiteral("}");
        }
        // (COMPOSER/ARRANGER/MESSAGE are never emitted, so they stay at count 0
        // and cdrdao's all-or-nothing check leaves them alone.)

        if (i == 0 && pregapFrames > 0)
            out << QStringLiteral("PREGAP %1").arg(framesToTimestamp(pregapFrames));

        out << QStringLiteral("FILE %1 %2 %3")
                   .arg(q(binName), framesToTimestamp(start),
                        framesToTimestamp(len));

        if (pregap > 0)
            out << QStringLiteral("START %1").arg(framesToTimestamp(pregap));
    }

    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}
