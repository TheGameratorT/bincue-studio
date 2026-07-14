#include "discdetailsdialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QToolButton>
#include <QVBoxLayout>

#include "cdtextfields.h"

namespace {

// A red ⚠ that explains itself when pressed, matching the main-window panel
// badges so a partial pack looks the same wherever it shows up.
QToolButton *makeBadge(QWidget *parent)
{
    auto *btn = new QToolButton(parent);
    btn->setText(QStringLiteral("⚠"));
    btn->setAutoRaise(true);
    btn->setStyleSheet(QStringLiteral("color: #d13438; font-weight: bold;"));
    btn->setCursor(Qt::PointingHandCursor);
    btn->hide();
    QObject::connect(btn, &QToolButton::clicked, parent, [parent] {
        QMessageBox::warning(
            parent, QObject::tr("CD-Text field incomplete"),
            QObject::tr(
                "This pack type is used on some tracks but not the whole disc. "
                "cdrdao needs it on the disc and every track, or nowhere.\n\n"
                "Fill it in here and on the tracks that lack it — or you'll be "
                "prompted to complete it when you burn or export."));
    });
    return btn;
}

QLayout *badgedRow(QLineEdit *edit, QToolButton *badge)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(edit, 1);
    row->addWidget(badge);
    return row;
}

}  // namespace

DiscDetailsDialog::DiscDetailsDialog(const ExportWorker::Params &metadata,
                                     QWidget *parent)
    : QDialog(parent), m_metadata(metadata)
{
    setWindowTitle(tr("Disc Details"));
    // Match TrackDetailsDialog so the value editors aren't squeezed into a
    // cramped column.
    setMinimumWidth(460);

    auto *root = new QVBoxLayout(this);

    // CD-Text: the disc-wide pack types most players either don't show or only
    // surface in a details view, so they live here instead of the main panel.
    auto *textBox = new QGroupBox(tr("CD-Text"));
    auto *textForm = new QFormLayout(textBox);
    m_composer = new QLineEdit(metadata.albumComposer);
    m_arranger = new QLineEdit(metadata.albumArranger);
    m_message = new QLineEdit(metadata.albumMessage);
    m_message->setPlaceholderText(tr("Short note shown by some players"));
    m_composerErr = makeBadge(this);
    m_arrangerErr = makeBadge(this);
    m_messageErr = makeBadge(this);
    textForm->addRow(tr("Composer:"), badgedRow(m_composer, m_composerErr));
    textForm->addRow(tr("Arranger:"), badgedRow(m_arranger, m_arrangerErr));
    textForm->addRow(tr("Message:"), badgedRow(m_message, m_messageErr));
    root->addWidget(textBox);

    // Identifier. DISC_ID is disc-only and rarely populated, hence its home
    // here rather than on the panel. It can't be "partial", so it has no badge.
    auto *idBox = new QGroupBox(tr("Identifier"));
    auto *idForm = new QFormLayout(idBox);
    m_discId = new QLineEdit(metadata.albumDiscId);
    m_discId->setToolTip(
        tr("Optional disc identifier written as the CD-Text DISC_ID pack "
           "(e.g. a catalogue code). Disc-wide only."));
    idForm->addRow(tr("Disc ID:"), m_discId);
    root->addWidget(idBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    // Refresh the badges live as the disc values change, exactly like the panel.
    for (QLineEdit *e : {m_composer, m_arranger, m_message})
        connect(e, &QLineEdit::textChanged, this,
                &DiscDetailsDialog::updateBadges);
    updateBadges();
}

void DiscDetailsDialog::updateBadges()
{
    m_metadata.albumComposer = m_composer->text();
    m_metadata.albumArranger = m_arranger->text();
    m_metadata.albumMessage = m_message->text();
    using cdtext::Pack;
    m_composerErr->setVisible(
        cdtext::needsAttention(m_metadata, cdtext::field(Pack::Composer)));
    m_arrangerErr->setVisible(
        cdtext::needsAttention(m_metadata, cdtext::field(Pack::Arranger)));
    m_messageErr->setVisible(
        cdtext::needsAttention(m_metadata, cdtext::field(Pack::Message)));
}

QString DiscDetailsDialog::composer() const
{
    return m_composer->text().trimmed();
}

QString DiscDetailsDialog::arranger() const
{
    return m_arranger->text().trimmed();
}

QString DiscDetailsDialog::message() const
{
    return m_message->text().trimmed();
}

QString DiscDetailsDialog::discId() const
{
    return m_discId->text().trimmed();
}

bool DiscDetailsDialog::hasDetails(const QString &composer,
                                   const QString &arranger,
                                   const QString &message, const QString &discId)
{
    return !composer.trimmed().isEmpty() || !arranger.trimmed().isEmpty()
        || !message.trimmed().isEmpty() || !discId.trimmed().isEmpty();
}
