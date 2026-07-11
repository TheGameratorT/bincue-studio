#pragma once

#include <hostkit/HostProcess.h>

// Drives cdrdao writing an audio disc on a local or remote host. The base
// class owns the process/ssh/pty lifecycle; this subclass only parses cdrdao's
// line output into a burn-progress figure and refines the summaries.
//
// Note: a graceful stop of a burn still counts as a "clean" finish in the base
// class, but aborting cdrdao mid-write ruins the disc — stoppedSummary() says
// so plainly rather than pretending the disc is usable.
class BurnController : public hostkit::HostProcess
{
    Q_OBJECT
public:
    explicit BurnController(QObject *parent = nullptr);

    struct Progress {
        bool valid = false;
        int wroteMb = 0;
        int totalMb = 0;
    };
    Progress progress() const { return m_progress; }

    // The full cdrdao argument list for a write, shared by the setup dialog's
    // preview and the burn job so what's shown is exactly what's run. The image
    // is raw little-endian PCM (--swap), no confirmation pause (-n), verbose so
    // progress lines appear (-v 2); userOptions are the dialog's speed/simulate/
    // eject/extra tokens; tocPath is placed last as cdrdao requires.
    static QStringList writeArgs(const QString &device,
                                 const QStringList &userOptions,
                                 const QString &tocPath);

signals:
    void progressChanged();

protected:
    void resetState() override;
    void handleLine(const QString &line) override;
    QString successSummary() const override;
    QString stoppedSummary() const override;

private:
    Progress m_progress;
    bool m_simulated = false;  // saw "Simulation" — refine the success wording
};
