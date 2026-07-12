#include "burndialog.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <hostkit/HostSession.h>
#include <hostkit/TerminalView.h>

#include "burncontroller.h"
#include "burnjob.h"

using namespace hostkit;

namespace {

QString humanSize(quint64 n)
{
    static const char *unit[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double v = double(n);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    return QStringLiteral("%1 %2").arg(v, 0, 'f', u ? 1 : 0).arg(unit[u]);
}

// The example device for the given host: a drive letter on local Windows, a
// device node on Linux (a null/remote session is Linux — SSH hosts always are).
QString deviceHint(const HostSession *session)
{
#ifdef Q_OS_WIN
    if (session && !session->isRemote())
        return QStringLiteral("D:");
#else
    Q_UNUSED(session);
#endif
    return QStringLiteral("/dev/sr0");
}

// Fill `list` with the host's optical drives only — burning to anything else
// would destroy data, so non-optical block devices are never offered. The first
// drive is preselected.
void populateDriveList(HostSession *session, QListWidget *list, QLabel *errorLabel)
{
    list->clear();
    QString err;
    const QVector<DriveInfo> drives = session->listDrives(&err);
    for (const DriveInfo &d : drives) {
        if (!d.isOptical())
            continue;
        // No size on a Windows drive with an empty tray — skip the column.
        QStringList cols{d.path};
        if (d.size > 0)
            cols << humanSize(d.size);
        cols << (d.model.isEmpty() ? QStringLiteral("(no model)") : d.model);
        auto *it = new QListWidgetItem(cols.join(QStringLiteral("   ")));
        it->setData(Qt::UserRole, d.path);
        list->addItem(it);
    }
    if (list->count() > 0)
        list->setCurrentRow(0);

    const bool showHint = !err.isEmpty() || list->count() == 0;
    errorLabel->setVisible(showHint);
    if (!err.isEmpty())
        errorLabel->setText(
            QStringLiteral("<i>Could not list drives: %1 — enter the device "
                           "(e.g. %2) manually.</i>")
                .arg(err.toHtmlEscaped(), deviceHint(session)));
    else if (list->count() == 0)
        errorLabel->setText(
            QStringLiteral("<i>No optical drive found — enter the device "
                           "(e.g. %1) manually if you know it.</i>")
                .arg(deviceHint(session)));
}

} // namespace

// --- BurnSetupDialog ---------------------------------------------------------

BurnSetupDialog::BurnSetupDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Burn to Disc"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/burn.svg")));
    auto *lay = new QVBoxLayout(this);

    auto *head = new QLabel(
        tr("<b>Burn this project to a CD.</b> Pick the machine the burner is on, "
           "the drive, and the write options. The image is built here; for a "
           "remote burner it is uploaded over SSH, then cdrdao writes the disc."),
        this);
    head->setWordWrap(true);
    lay->addWidget(head);

    auto *hostRow = new QHBoxLayout;
    hostRow->addWidget(new QLabel(tr("Burner is on:"), this));
    m_hostCombo = new QComboBox(this);
    m_hostCombo->addItem(tr("This computer"));
    m_hostCombo->addItem(tr("Remote host (SSH)"));
    hostRow->addWidget(m_hostCombo);
    m_destEdit = new QLineEdit(this);
    m_destEdit->setPlaceholderText(
        tr("user@host  (or a ~/.ssh/config alias)"));
    hostRow->addWidget(m_destEdit, 1);
    m_connectBtn = new QPushButton(tr("Connect"), this);
    m_connectBtn->setAutoDefault(false);
    hostRow->addWidget(m_connectBtn);
    lay->addLayout(hostRow);

    m_hostStatus = new QLabel(this);
    m_hostStatus->setWordWrap(true);
    m_hostStatus->setVisible(false);
    lay->addWidget(m_hostStatus);

    lay->addWidget(new QLabel(tr("Burner drive:"), this));
    m_driveList = new QListWidget(this);
    m_driveList->setMinimumHeight(110);
    lay->addWidget(m_driveList);
    m_driveError = new QLabel(this);
    m_driveError->setWordWrap(true);
    m_driveError->setVisible(false);
    lay->addWidget(m_driveError);

    auto *refreshBtn = new QPushButton(tr("Refresh drives"), this);
    refreshBtn->setAutoDefault(false);
    connect(refreshBtn, &QPushButton::clicked, this,
            &BurnSetupDialog::reloadDrives);
    lay->addWidget(refreshBtn, 0, Qt::AlignLeft);

    auto *form = new QFormLayout;
    m_deviceEdit = new QLineEdit(this);
    // Placeholder tracks the selected host (drive letter vs device node);
    // updatePreview() keeps it current.
    form->addRow(tr("Device:"), m_deviceEdit);

    m_speedSpin = new QSpinBox(this);
    m_speedSpin->setRange(0, 56);
    m_speedSpin->setSpecialValueText(tr("maximum"));
    m_speedSpin->setValue(0);
    m_speedSpin->setToolTip(
        tr("Write speed multiplier. \"maximum\" lets the drive/media decide; a "
           "lower speed can be more reliable on cheap discs."));
    form->addRow(tr("Speed:"), m_speedSpin);

    m_simulateCheck = new QCheckBox(
        tr("Simulate only (test run, laser off)"), this);
    m_simulateCheck->setToolTip(
        tr("Run cdrdao with --simulate: everything happens except the laser "
           "writing. Good for a first dry run."));
    form->addRow(QString(), m_simulateCheck);

    m_ejectCheck = new QCheckBox(tr("Eject the disc when done"), this);
    form->addRow(QString(), m_ejectCheck);

    m_extraEdit = new QLineEdit(this);
    m_extraEdit->setPlaceholderText(
        tr("extra cdrdao options (e.g. --driver generic-mmc-raw)"));
    form->addRow(tr("Extra options:"), m_extraEdit);
    lay->addLayout(form);

    lay->addWidget(new QLabel(tr("Command:"), this));
    m_preview = new QLabel(this);
    m_preview->setWordWrap(true);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    lay->addWidget(m_preview);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("Burn"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(m_buttons);

    connect(m_hostCombo, qOverload<int>(&QComboBox::activated), this,
            [this](int) { onHostModeChanged(); });
    connect(m_connectBtn, &QPushButton::clicked, this,
            &BurnSetupDialog::onConnectRemote);
    // Note: Enter is handled in keyPressEvent (connect / swallow), not via
    // returnPressed, so it can never fall through to the default button.
    connect(m_driveList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            m_deviceEdit->setText(
                m_driveList->item(row)->data(Qt::UserRole).toString());
    });
    connect(m_deviceEdit, &QLineEdit::textChanged, this,
            &BurnSetupDialog::updatePreview);
    connect(m_speedSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int) { updatePreview(); });
    connect(m_simulateCheck, &QCheckBox::toggled, this,
            [this](bool) { updatePreview(); });
    connect(m_ejectCheck, &QCheckBox::toggled, this,
            [this](bool) { updatePreview(); });
    connect(m_extraEdit, &QLineEdit::textChanged, this,
            &BurnSetupDialog::updatePreview);

    resize(680, 620);
    onHostModeChanged();
    updatePreview();
}

BurnSetupDialog::~BurnSetupDialog() = default;

std::unique_ptr<HostSession> BurnSetupDialog::takeSession()
{
    return std::move(m_session);
}

QString BurnSetupDialog::device() const
{
    return m_deviceEdit->text().trimmed();
}

QStringList BurnSetupDialog::userOptions() const
{
    QStringList opts;
    if (m_speedSpin->value() > 0)
        opts << QStringLiteral("--speed")
             << QString::number(m_speedSpin->value());
    if (m_simulateCheck->isChecked())
        opts << QStringLiteral("--simulate");
    if (m_ejectCheck->isChecked())
        opts << QStringLiteral("--eject");
    opts << m_extraEdit->text().trimmed().split(QLatin1Char(' '),
                                                Qt::SkipEmptyParts);
    return opts;
}

void BurnSetupDialog::onHostModeChanged()
{
    const bool remote = m_hostCombo->currentIndex() == 1;
    m_destEdit->setEnabled(remote);
    m_connectBtn->setEnabled(remote);
    m_driveError->setVisible(false);
    if (remote) {
        m_session.reset();
        m_driveList->clear();
        m_hostStatus->setText(
            tr("<i>Enter the SSH destination and press Connect to list its "
               "drives. Authentication uses your ssh keys/agent and "
               "~/.ssh/config (no password prompts).</i>"));
        m_hostStatus->setVisible(true);
    } else {
        m_hostStatus->setVisible(false);
        m_session = std::make_unique<HostSession>();
        reloadDrives();
    }
    updateOkButton();
    updatePreview();
}

void BurnSetupDialog::onConnectRemote()
{
    const QString dest = m_destEdit->text().trimmed();
    if (m_hostCombo->currentIndex() != 1 || dest.isEmpty())
        return;
    auto s = std::make_unique<HostSession>(dest);
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    m_hostStatus->setText(tr("Connecting to %1…").arg(dest));
    m_hostStatus->setVisible(true);
    QString err;
    const bool ok = s->connectToHost(&err);
    QGuiApplication::restoreOverrideCursor();
    if (!ok) {
        m_session.reset();
        m_driveList->clear();
        m_hostStatus->setText(
            tr("<span style='color:#d02f2f'>Connection to %1 failed:</span> %2")
                .arg(dest.toHtmlEscaped(), err.toHtmlEscaped()));
    } else {
        m_session = std::move(s);
        m_hostStatus->setText(tr("Connected to %1.").arg(dest));
        reloadDrives();
    }
    updateOkButton();
    updatePreview();
}

void BurnSetupDialog::reloadDrives()
{
    if (m_session)
        populateDriveList(m_session.get(), m_driveList, m_driveError);
}

void BurnSetupDialog::updatePreview()
{
    const QString hint = deviceHint(m_session.get());
    m_deviceEdit->setPlaceholderText(hint);
    const QString dev = device().isEmpty() ? hint : device();
    const QStringList args = BurnController::writeArgs(
        dev, userOptions(), QStringLiteral("album.toc"));
    QString cmd = QStringLiteral("cdrdao ") + args.join(QLatin1Char(' '));
    if (m_session && m_session->isRemote())
        cmd += QStringLiteral("   [on %1]").arg(m_session->destination());
    m_preview->setText(cmd);
    updateOkButton();
}

void BurnSetupDialog::updateOkButton()
{
    m_buttons->button(QDialogButtonBox::Ok)
        ->setEnabled(m_session != nullptr && !device().isEmpty());
}

void BurnSetupDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        // QDialogButtonBox re-promotes the accept button to default when shown,
        // so simply un-defaulting "Burn" isn't enough — intercept Enter here.
        // In remote mode it connects; otherwise it does nothing. Burning is only
        // ever started by clicking the Burn button.
        if (m_hostCombo->currentIndex() == 1 && m_connectBtn->isEnabled())
            onConnectRemote();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

// --- BurnProgressDialog ------------------------------------------------------

BurnProgressDialog::BurnProgressDialog(BurnJob *job, QWidget *parent)
    : QDialog(parent), m_job(job)
{
    m_job->setParent(this);
    setWindowTitle(tr("Burning"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/burn.svg")));
    setModal(true);
    resize(720, 480);

    auto *lay = new QVBoxLayout(this);
    m_phase = new QLabel(tr("Starting…"), this);
    m_phase->setWordWrap(true);
    lay->addWidget(m_phase);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 0);  // busy until the first concrete progress arrives
    lay->addWidget(m_bar);

    m_term = new TerminalView(this);
    lay->addWidget(m_term, 1);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    m_stopBtn = new QPushButton(tr("Stop"), this);
    m_closeBtn = new QPushButton(tr("Close"), this);
    m_closeBtn->setEnabled(false);
    btnRow->addWidget(m_stopBtn);
    btnRow->addWidget(m_closeBtn);
    lay->addLayout(btnRow);

    connect(m_stopBtn, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(
                this, tr("Stop the burn?"),
                tr("Stopping now aborts cdrdao. If it is already writing, the "
                   "disc will be unusable. Stop anyway?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes) {
            m_stopBtn->setEnabled(false);
            m_job->stop();
        }
    });
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_job, &BurnJob::log, this,
            [this](const QByteArray &raw) { m_term->appendChunk(raw); });
    connect(m_job, &BurnJob::phase, this,
            [this](const QString &text) { m_phase->setText(text); });
    connect(m_job, &BurnJob::stepProgress, this,
            [this](qint64 cur, qint64 total, const QString &msg) {
                m_phase->setText(msg);
                if (total > 0) {
                    m_bar->setRange(0, 100);
                    m_bar->setValue(int(cur * 100 / total));
                } else {
                    m_bar->setRange(0, 0);
                }
            });
    connect(m_job, &BurnJob::burnProgress, this,
            [this](int wrote, int total) {
                if (total > 0) {
                    m_bar->setRange(0, total);
                    m_bar->setValue(wrote);
                }
            });
    connect(m_job, &BurnJob::finished, this,
            [this](bool ok, const QString &summary) {
                m_running = false;
                m_stopBtn->setEnabled(false);
                m_closeBtn->setEnabled(true);
                m_closeBtn->setDefault(true);
                if (ok) {
                    m_bar->setRange(0, 100);
                    m_bar->setValue(100);
                }
                m_phase->setText(
                    QStringLiteral("<b style='color:%1'>%2</b>")
                        .arg(ok ? QStringLiteral("#2e8b57")
                                : QStringLiteral("#d02f2f"),
                             summary.toHtmlEscaped()));
            });

    m_job->start();
}

void BurnProgressDialog::closeEvent(QCloseEvent *event)
{
    if (!m_running) {
        event->accept();
        return;
    }
    if (QMessageBox::question(
            this, tr("Stop the burn?"),
            tr("A burn is still in progress. Stopping now aborts cdrdao and may "
               "ruin the disc. Stop and close?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        == QMessageBox::Yes) {
        m_job->stop();
        event->ignore();  // wait for finished() before the dialog closes
    } else {
        event->ignore();
    }
}
