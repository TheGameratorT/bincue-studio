#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <memory>

class BurnJob;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace hostkit {
class HostSession;
class TerminalView;
}

// "Burn to Disc…": pick the machine the burner is on (this computer or an SSH
// destination), the drive there, and cdrdao write options. Owns the connected
// HostSession until the caller takes it on Accepted. Mirrors the disc-damage-
// viewer new-rescue dialog so the two tools feel the same.
class BurnSetupDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BurnSetupDialog(QWidget *parent = nullptr);
    ~BurnSetupDialog() override;

    std::unique_ptr<hostkit::HostSession> takeSession();
    QString device() const;
    // cdrdao option tokens from the speed/simulate/eject/extra controls.
    QStringList userOptions() const;

protected:
    // Swallow Enter so it can never accept the dialog (start a burn); in remote
    // mode it connects instead. Accepting requires clicking "Burn".
    void keyPressEvent(QKeyEvent *event) override;

private:
    void onHostModeChanged();
    void onConnectRemote();
    void reloadDrives();
    void updatePreview();
    void updateOkButton();

    std::unique_ptr<hostkit::HostSession> m_session;
    QComboBox *m_hostCombo = nullptr;
    QLineEdit *m_destEdit = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QLabel *m_hostStatus = nullptr;
    QListWidget *m_driveList = nullptr;
    QLabel *m_driveError = nullptr;
    QLineEdit *m_deviceEdit = nullptr;
    QSpinBox *m_speedSpin = nullptr;
    QCheckBox *m_simulateCheck = nullptr;
    QCheckBox *m_ejectCheck = nullptr;
    QLineEdit *m_extraEdit = nullptr;
    QLabel *m_preview = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

// Live burn view: phase label, a progress bar (build/upload bytes, then written
// MB) and a terminal of cdrdao's output. Takes ownership of the BurnJob and
// runs it. Blocks closing while the burn is in flight; Stop aborts it.
class BurnProgressDialog : public QDialog
{
    Q_OBJECT
public:
    BurnProgressDialog(BurnJob *job, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    BurnJob *m_job = nullptr;
    hostkit::TerminalView *m_term = nullptr;
    QProgressBar *m_bar = nullptr;
    QLabel *m_phase = nullptr;
    QPushButton *m_stopBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
    bool m_running = true;
};
