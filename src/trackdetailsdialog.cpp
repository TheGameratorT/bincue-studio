#include "trackdetailsdialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

TrackDetailsDialog::TrackDetailsDialog(const Track &track, QWidget *parent)
    : QDialog(parent), m_track(track)
{
    const QString name =
        track.title.isEmpty()
            ? QFileInfo(track.sourcePath).completeBaseName()
            : track.title;
    setWindowTitle(name.isEmpty() ? tr("Track Details")
                                  : tr("Details — %1").arg(name));
    // The default hint packs the fields into a cramped column; give the value
    // editors room so titles and messages aren't squeezed.
    setMinimumWidth(460);

    auto *root = new QVBoxLayout(this);

    // CD-Text: the pack types most players either don't show or only surface in
    // a details view, so they live here instead of the table.
    auto *textBox = new QGroupBox(tr("CD-Text"));
    auto *textForm = new QFormLayout(textBox);
    m_songwriter = new QLineEdit(track.songwriter);
    m_composer = new QLineEdit(track.composer);
    m_arranger = new QLineEdit(track.arranger);
    m_message = new QLineEdit(track.message);
    m_message->setPlaceholderText(tr("Short note shown by some players"));
    textForm->addRow(tr("Songwriter:"), m_songwriter);
    textForm->addRow(tr("Composer:"), m_composer);
    textForm->addRow(tr("Arranger:"), m_arranger);
    textForm->addRow(tr("Message:"), m_message);
    root->addWidget(textBox);

    // Identifier.
    auto *idBox = new QGroupBox(tr("Identifier"));
    auto *idForm = new QFormLayout(idBox);
    m_isrc = new QLineEdit(track.isrc);
    m_isrc->setPlaceholderText(tr("CC-XXX-YY-NNNNN"));
    m_isrc->setToolTip(
        tr("12-character ISRC. Auto-filled from the source's tags when "
           "present; written to the disc only if valid."));
    idForm->addRow(tr("ISRC:"), m_isrc);
    root->addWidget(idBox);

    // Red Book subchannel flags. Defaults match the overwhelmingly common case,
    // so a stereo, copy-protected, non-emphasised track leaves them all off.
    auto *flagsBox = new QGroupBox(tr("Playback flags"));
    auto *flagsLayout = new QVBoxLayout(flagsBox);
    m_copyPermitted = new QCheckBox(tr("Digital copying permitted"));
    m_copyPermitted->setChecked(track.copyPermitted);
    m_copyPermitted->setToolTip(
        tr("Clears the copy-prohibit flag so compliant hardware may copy the "
           "track. Off by default (copying prohibited)."));
    m_preEmphasis = new QCheckBox(tr("Pre-emphasis (50/15 µs)"));
    m_preEmphasis->setChecked(track.preEmphasis);
    m_preEmphasis->setToolTip(
        tr("Marks the audio as recorded with pre-emphasis. Only enable this if "
           "the source really is pre-emphasised."));
    m_fourChannel = new QCheckBox(tr("Four-channel audio"));
    m_fourChannel->setChecked(track.fourChannel);
    m_fourChannel->setToolTip(
        tr("Marks the track as quadraphonic rather than stereo. Almost never "
           "used; leave off for ordinary stereo audio."));
    flagsLayout->addWidget(m_copyPermitted);
    flagsLayout->addWidget(m_preEmphasis);
    flagsLayout->addWidget(m_fourChannel);
    root->addWidget(flagsBox);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

Track TrackDetailsDialog::track() const
{
    Track t = m_track;
    t.songwriter = m_songwriter->text().trimmed();
    t.composer = m_composer->text().trimmed();
    t.arranger = m_arranger->text().trimmed();
    t.message = m_message->text().trimmed();
    t.isrc = m_isrc->text().trimmed();
    t.copyPermitted = m_copyPermitted->isChecked();
    t.preEmphasis = m_preEmphasis->isChecked();
    t.fourChannel = m_fourChannel->isChecked();
    return t;
}

bool TrackDetailsDialog::hasDetails(const Track &track)
{
    return !track.songwriter.isEmpty() || !track.composer.isEmpty()
        || !track.arranger.isEmpty() || !track.message.isEmpty()
        || !track.isrc.isEmpty() || track.copyPermitted || track.preEmphasis
        || track.fourChannel;
}
