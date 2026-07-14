#include "cdtextcompletion.h"

#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include "cdtextfields.h"

namespace {

QString tr(const char *s)
{
    return QCoreApplication::translate("CdText", s);
}

// Phase 1: the disc-level values cdrdao needs but that are blank. The user sets
// these on the Disc panel (which flags them with ⚠), not in a fill dialog, so a
// missing one is a hard stop with an explanation — never auto-filled.
bool blockOnMissingDiscInfo(const ExportWorker::Params &params, QWidget *parent)
{
    QStringList missing;
    for (int i = 0; i < cdtext::PackCount; ++i) {
        const cdtext::Field &f = cdtext::field(cdtext::Pack(i));
        if (cdtext::inUse(params, f) && !cdtext::discSet(params, f))
            missing << tr(f.label);
    }
    if (missing.isEmpty())
        return false;

    QMessageBox::warning(
        parent, tr("Disc information incomplete"),
        tr("CD-Text is the title, artist and other text a CD player shows for "
           "the disc and its tracks. Once a field is filled in on any track, "
           "the same field must also be set for the disc as a whole — and a "
           "disc with any CD-Text needs at least a Title and Performer.\n\n"
           "These disc fields are still blank:\n\n• %1\n\nFill them in on the "
           "Disc panel (the fields marked ⚠), then try again.")
            .arg(missing.join(QStringLiteral("\n• "))));
    return true;
}

}  // namespace

bool ensureCdTextComplete(ExportWorker::Params &params, QWidget *parent)
{
    // Phase 1 — a blank disc value the user must fix themselves stops us here.
    if (blockOnMissingDiscInfo(params, parent))
        return false;

    // Phase 2 — the disc values are all set; only some tracks are missing theirs.
    // Offer a per-track fill value (separate from the disc value, so the disc can
    // read "Various Artists" while each track reads "Unknown Artist").
    std::vector<const cdtext::Field *> todo;
    for (int i = 0; i < cdtext::PackCount; ++i) {
        const cdtext::Field &f = cdtext::field(cdtext::Pack(i));
        if (cdtext::inUse(params, f) && cdtext::discSet(params, f)
            && cdtext::anyTrackBlank(params, f))
            todo.push_back(&f);
    }
    if (todo.empty())
        return true;  // fully consistent — nothing to ask

    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Complete track CD-Text"));
    dlg.setMinimumWidth(500);
    auto *root = new QVBoxLayout(&dlg);

    auto *intro = new QLabel(
        tr("Some tracks are missing CD-Text that the disc has, and cdrdao needs "
           "each field on every track. Enter the value to put on the tracks that "
           "lack one — it can differ from the disc value shown."));
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto *form = new QFormLayout;
    std::vector<std::pair<const cdtext::Field *, QLineEdit *>> rows;
    for (const cdtext::Field *f : todo) {
        auto *edit = new QLineEdit(tr(f->unknown));
        auto *label = new QLabel(
            tr("%1  —  disc “%2”, blank on %3 of %4 tracks")
                .arg(tr(f->label), cdtext::discValue(params, *f))
                .arg(cdtext::blankTrackCount(params, *f))
                .arg(params.tracks.size()));
        form->addRow(label, edit);
        rows.emplace_back(f, edit);
    }
    root->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg,
                     &QDialog::reject);
    root->addWidget(buttons);

    // A blank would just recreate the gap cdrdao rejects, so Ok stays disabled
    // until every row has a value.
    auto *ok = buttons->button(QDialogButtonBox::Ok);
    auto refreshOk = [ok, rows] {
        bool allFilled = true;
        for (const auto &[f, edit] : rows)
            allFilled = allFilled && !edit->text().trimmed().isEmpty();
        ok->setEnabled(allFilled);
    };
    for (const auto &[f, edit] : rows)
        QObject::connect(edit, &QLineEdit::textChanged, &dlg, refreshOk);
    refreshOk();

    if (dlg.exec() != QDialog::Accepted)
        return false;

    for (const auto &[f, edit] : rows)
        cdtext::applyTrackFill(params, *f, edit->text().trimmed());
    return true;
}
