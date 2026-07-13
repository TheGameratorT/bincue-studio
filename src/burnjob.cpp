#include "burnjob.h"

#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTemporaryDir>

#include <hostkit/HostSession.h>

#include "burncontroller.h"

BurnJob::BurnJob(ExportWorker::Params params,
                 std::unique_ptr<hostkit::HostSession> session, QString device,
                 QStringList userOptions, QObject *parent)
    : QObject(parent), m_params(std::move(params)),
      m_session(std::move(session)), m_device(std::move(device)),
      m_userOptions(std::move(userOptions))
{
}

BurnJob::~BurnJob()
{
    if (m_uploadThread) {
        m_uploadThread->wait();
    }
}

void BurnJob::start()
{
    m_localDir = std::make_unique<QTemporaryDir>();
    if (!m_localDir->isValid()) {
        finishWith(false, tr("Could not create a temporary directory."));
        return;
    }

    // Build the image + TOC into our temp dir under a fixed base name; the
    // disc's CD-Text comes from the album fields, not the file name.
    m_params.outDir = m_localDir->path();
    m_params.baseName = QStringLiteral("image");
    m_params.writeToc = true;

    emit phase(tr("Building disc image…"));
    m_export = new ExportWorker(m_params, this);
    connect(m_export, &ExportWorker::progress, this,
            [this](int cur, int total, const QString &msg) {
                emit stepProgress(cur, total, msg);
            });
    connect(m_export, &ExportWorker::finishedOk, this, &BurnJob::onImageBuilt);
    connect(m_export, &ExportWorker::failed, this, &BurnJob::onBuildFailed);
    connect(m_export, &QThread::finished, m_export, &QObject::deleteLater);
    m_export->start();
}

void BurnJob::onBuildFailed(const QString &message)
{
    m_export = nullptr;
    finishWith(false, tr("Building the image failed: %1").arg(message));
}

void BurnJob::onImageBuilt(const QString &binPath, const QString &cuePath,
                           const QString &tocPath)
{
    Q_UNUSED(cuePath);
    m_export = nullptr;
    m_localBin = binPath;
    m_localToc = tocPath;

    if (m_cancelled) {
        finishWith(false, tr("Burn cancelled."));
        return;
    }

    if (m_session->isRemote()) {
        // Upload blocks on the image bytes, so push it off the GUI thread.
        m_uploadThread = QThread::create([this] { prepareRemoteThenBurn(); });
        connect(m_uploadThread, &QThread::finished, m_uploadThread,
                &QObject::deleteLater);
        connect(m_uploadThread, &QObject::destroyed, this,
                [this] { m_uploadThread = nullptr; });
        m_uploadThread->start();
    } else {
        // Point the TOC's FILE at the absolute local BIN path: cdrdao's working
        // directory isn't ours, so the exported bare filename may not resolve.
        QString err;
        const QString burnToc =
            writeBurnToc(QFileInfo(m_localBin).absoluteFilePath(), &err);
        if (burnToc.isEmpty()) {
            finishWith(false, err);
            return;
        }
        startBurn(burnToc);
    }
}

// See the declaration: rewrite the exported TOC's bare BIN filename to an
// absolute `binTarget` and write burn.toc alongside it in the local temp dir.
QString BurnJob::writeBurnToc(const QString &binTarget, QString *error)
{
    QFile tf(m_localToc);
    if (!tf.open(QIODevice::ReadOnly)) {
        *error = tr("Could not read the TOC: %1").arg(tf.errorString());
        return {};
    }
    QString toc = QString::fromUtf8(tf.readAll());
    tf.close();

    // Match the quoted FILE token so a stray substring elsewhere can't be hit.
    const QString binToken =
        QLatin1Char('"') + QFileInfo(m_localBin).fileName() + QLatin1Char('"');
    const QString target = QLatin1Char('"') + binTarget + QLatin1Char('"');
    toc.replace(binToken, target);

    const QString burnToc = m_localDir->filePath(QStringLiteral("burn.toc"));
    QFile bt(burnToc);
    if (!bt.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = tr("Could not write the TOC: %1").arg(bt.errorString());
        return {};
    }
    bt.write(toc.toUtf8());
    bt.close();
    return burnToc;
}

void BurnJob::prepareRemoteThenBurn()
{
    const QString dest = m_session->destination();
    emit phase(tr("Preparing %1…").arg(dest));

    // Locate cdrdao before spending time on the upload. Posix hosts resolve
    // the bare name on PATH; Windows hosts check the install dir the
    // installer records in BINCUE_STUDIO_HOME, then fall back to where.exe.
    QString toolErr;
    m_cdrdao = m_session->resolveRemoteTool(
        QStringLiteral("cdrdao"), {QStringLiteral("$env:BINCUE_STUDIO_HOME")},
        &toolErr);
    if (m_cdrdao.isEmpty()) {
        finishWith(false,
                   tr("cdrdao is not available on %1: %2").arg(dest, toolErr));
        return;
    }

    QString err;
    m_remoteDir = m_session->makeTempDir(QStringLiteral("bincue-burn"), &err);
    if (m_remoteDir.isEmpty()) {
        finishWith(false, tr("Could not create a working directory on %1: %2")
                              .arg(dest, err));
        return;
    }
    // A Windows host answers with a backslashed path; flip to forward slashes,
    // which Win32 and PowerShell accept everywhere while staying literal inside
    // the TOC's quoted FILE string (backslash is its escape character there).
    m_remoteDir.replace(QLatin1Char('\\'), QLatin1Char('/'));

    const QString remoteBin = m_remoteDir + QStringLiteral("/image.bin");
    const QString remoteToc = m_remoteDir + QStringLiteral("/burn.toc");

    // Point the TOC's FILE at the uploaded BIN's remote path.
    QString err2;
    const QString localBurnToc = writeBurnToc(remoteBin, &err2);
    if (localBurnToc.isEmpty()) {
        finishWith(false, err2);
        return;
    }

    emit phase(tr("Uploading image to %1…").arg(dest));
    if (!m_session->uploadFile(
            m_localBin, remoteBin,
            [this](qint64 sent, qint64 total) {
                emit stepProgress(sent, total, tr("Uploading image…"));
            },
            &err)) {
        finishWith(false, tr("Uploading the image to %1 failed: %2")
                              .arg(dest, err));
        return;
    }
    if (!m_session->uploadFile(localBurnToc, remoteToc, {}, &err)) {
        finishWith(false,
                   tr("Uploading the TOC to %1 failed: %2").arg(dest, err));
        return;
    }

    if (m_cancelled) {
        finishWith(false, tr("Burn cancelled."));
        return;
    }

    // Hand back to the GUI thread to launch cdrdao (it drives a QProcess).
    QMetaObject::invokeMethod(
        this, [this, remoteToc] { startBurn(remoteToc); }, Qt::QueuedConnection);
}

void BurnJob::startBurn(const QString &tocPath)
{
    if (m_cancelled) {
        finishWith(false, tr("Burn cancelled."));
        return;
    }

    emit phase(tr("Burning on %1…").arg(m_session->label()));
    m_burn = new BurnController(m_cdrdao, this);
    connect(m_burn, &BurnController::outputChunk, this,
            [this](const QByteArray &raw) { emit log(raw); });
    connect(m_burn, &BurnController::progressChanged, this, [this] {
        const BurnController::Progress p = m_burn->progress();
        if (p.valid)
            emit burnProgress(p.wroteMb, p.totalMb);
    });
    connect(m_burn, &BurnController::finished, this, &BurnJob::onBurnFinished);

    const QStringList args =
        BurnController::writeArgs(m_device, m_userOptions, tocPath);
    QString err;
    if (!m_burn->start(m_session.get(), {args}, &err)) {
        onBurnFinished(false, tr("Could not start cdrdao: %1").arg(err));
    }
}

void BurnJob::onBurnFinished(bool ok, const QString &summary)
{
    finishWith(ok, summary);
}

void BurnJob::stop()
{
    m_cancelled = true;
    if (!(m_burn && m_burn->isRunning()))
        return;
    // A burn can't be rescued: interrupting cdrdao mid-write ruins the disc
    // whether we ask nicely or not. On a Windows host the "graceful" path is a
    // ^C forwarded over ConPTY, which is unreliable against a process that
    // isn't reading its console (cdrdao) and can leave the user unable to stop
    // at all (see docs/remote-burning.md). Since the outcome is a dead disc
    // either way, skip the pleasantries and hard-kill the process tree.
    if (m_session && m_session->isRemote()
        && m_session->hostOs() == hostkit::HostOs::Windows)
        m_burn->forceKill();
    else
        m_burn->stopGracefully();
}

void BurnJob::finishWith(bool ok, const QString &summary)
{
    if (m_finished)
        return;
    m_finished = true;
    if (m_session && m_session->isRemote() && !m_remoteDir.isEmpty())
        m_session->removeTree(m_remoteDir);
    // The local temp dir is dropped when m_localDir is destroyed with the job.
    emit finished(ok, summary);
}
