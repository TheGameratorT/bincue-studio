#pragma once

#include <QThread>

#include "track.h"

// Builds the BIN + CUE pair off the UI thread: each track is decoded by
// ffmpeg to raw CD-DA PCM, sector-aligned, its baked-in trailing silence
// trimmed/filled to the inter-track gap, and appended to a single image whose
// cue offsets then match byte-for-byte.
class ExportWorker : public QThread
{
    Q_OBJECT
public:
    struct Params {
        QList<Track> tracks;
        QString albumTitle;
        QString albumPerformer;
        QString albumSongwriter;
        QString albumGenre;
        QString albumYear;
        QString albumCatalog;
        QString outDir;
        QString baseName;
        double pregapSeconds = redbook::DEFAULT_PREGAP_SECONDS;
        double gapSeconds = redbook::DEFAULT_GAP_SECONDS;
        // Also emit a cdrdao <baseName>.toc alongside the BIN + CUE. cdrdao
        // can't burn an audio cue directly, so the burn path asks for this;
        // a plain export leaves it off.
        bool writeToc = false;
    };

    explicit ExportWorker(Params params, QObject *parent = nullptr);

signals:
    void progress(int current, int total, const QString &message);
    // tocPath is empty unless Params::writeToc was set.
    void finishedOk(const QString &binPath, const QString &cuePath,
                    const QString &tocPath);
    void failed(const QString &message);

protected:
    void run() override;

private:
    // Per-track facts the cdrdao TOC needs, captured as the image is built so
    // the TOC's FILE regions line up byte-for-byte with the cue's offsets.
    struct TocEntry {
        qint64 startFrame = 0;  // INDEX 01 position of the track in the image
        QString title;
        QString performer;
        QString songwriter;
        QString composer;
        QString arranger;
        QString message;
        QString isrc;  // already normalized (or empty)
        bool copyPermitted = false;
        bool preEmphasis = false;
        bool fourChannel = false;
    };

    // Assemble a cdrdao TOC describing the just-written image (see the .cpp).
    QString buildToc(const QList<TocEntry> &entries, qint64 totalFrames,
                     qint64 pregapFrames, qint64 gapFrames,
                     const QString &catalog, const QString &binPath);

    Params m_params;
};
