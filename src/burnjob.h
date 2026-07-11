#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include "exportworker.h"

class BurnController;
class QThread;
class QTemporaryDir;

namespace hostkit {
class HostSession;
}

// Runs one end-to-end burn: build the BIN + cdrdao TOC into a local temp dir,
// then (for a remote host) upload them and (for either) run cdrdao on the host.
// Owns the HostSession taken from the setup dialog and cleans up its temp dirs
// — local when it's destroyed, remote when the burn ends. All signals arrive on
// the thread that created the BurnJob (the GUI thread); the blocking upload runs
// on a private worker thread.
class BurnJob : public QObject
{
    Q_OBJECT
public:
    // `params` supplies tracks + metadata + gaps; the output location, base name
    // and TOC flag are set here. `userOptions` are the dialog's cdrdao tokens
    // (--speed, --simulate, --eject, extra).
    BurnJob(ExportWorker::Params params,
            std::unique_ptr<hostkit::HostSession> session, QString device,
            QStringList userOptions, QObject *parent = nullptr);
    ~BurnJob() override;

    void start();

public slots:
    // Cancel the burn: aborts cdrdao if it's writing (ruining the disc), or
    // skips the burn if the image is still being built/uploaded.
    void stop();

signals:
    void log(const QByteArray &raw);
    void phase(const QString &text);
    // Reused for both encoding and upload byte progress (message says which).
    void stepProgress(qint64 current, qint64 total, const QString &message);
    void burnProgress(int wroteMb, int totalMb);
    void finished(bool ok, const QString &summary);

private:
    void onImageBuilt(const QString &binPath, const QString &cuePath,
                      const QString &tocPath);
    void onBuildFailed(const QString &message);
    void prepareRemoteThenBurn();  // on a worker thread
    // The exported TOC uses a bare BIN filename (portable); cdrdao resolves it
    // against its working directory, which we don't control, so rewrite the FILE
    // token to an absolute `binTarget` and write burn.toc. Returns its path, or
    // empty on error (with *error set).
    QString writeBurnToc(const QString &binTarget, QString *error);
    void startBurn(const QString &tocPath);
    void onBurnFinished(bool ok, const QString &summary);
    // The single terminal exit: cleans up the remote temp dir and emits
    // finished() exactly once, however the burn ends. Safe from any thread.
    void finishWith(bool ok, const QString &summary);

    ExportWorker::Params m_params;
    std::unique_ptr<hostkit::HostSession> m_session;
    QString m_device;
    QStringList m_userOptions;

    std::unique_ptr<QTemporaryDir> m_localDir;
    QString m_localBin;
    QString m_localToc;
    QString m_remoteDir;

    ExportWorker *m_export = nullptr;
    BurnController *m_burn = nullptr;
    QThread *m_uploadThread = nullptr;
    bool m_cancelled = false;
    bool m_finished = false;
};
