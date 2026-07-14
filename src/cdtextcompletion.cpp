#include "cdtextcompletion.h"

#include <vector>

#include <QCheckBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
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

namespace {

// One field's controls: fill it with `edit`'s text, or (for a non-mandatory
// field) drop it when `drop` is checked. `drop` is null for a mandatory pack,
// which can never be dropped while any CD-Text remains.
struct Row {
    const cdtext::Field *field;
    QLineEdit *edit;
    QRadioButton *drop;

    bool dropping() const { return drop && drop->isChecked(); }
    // Satisfied means it won't recreate the gap cdrdao rejects: either dropped,
    // or filled with a non-empty value.
    bool satisfied() const { return dropping() || !edit->text().trimmed().isEmpty(); }
};

}  // namespace

CdTextChoice ensureCdTextComplete(ExportWorker::Params &params, QWidget *parent)
{
    // Phase 1 — a blank disc value the user must fix themselves stops us here.
    if (blockOnMissingDiscInfo(params, parent))
        return {};

    // Phase 2 — the disc values are all set; only some tracks are missing theirs.
    // Offer a per-track fill value (separate from the disc value, so the disc can
    // read "Various Artists" while each track reads "Unknown Artist"), or dropping
    // the whole field so it isn't burned at all.
    std::vector<const cdtext::Field *> todo;
    for (int i = 0; i < cdtext::PackCount; ++i) {
        const cdtext::Field &f = cdtext::field(cdtext::Pack(i));
        if (cdtext::inUse(params, f) && cdtext::discSet(params, f)
            && cdtext::anyTrackBlank(params, f))
            todo.push_back(&f);
    }
    if (todo.empty())
        return {true, false};  // fully consistent — nothing to ask

    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Complete track CD-Text"));
    dlg.setMinimumWidth(460);
    auto *root = new QVBoxLayout(&dlg);

    auto *intro = new QLabel(
        tr("These fields are filled in for the disc as a whole but are missing "
           "on some tracks, and a CD needs the same fields on every track. For "
           "each one, put a value on the tracks that lack it — or drop the field "
           "so it isn’t written to the CD at all."));
    intro->setWordWrap(true);
    root->addWidget(intro);

    const int total = int(params.tracks.size());
    std::vector<Row> rows;
    for (const cdtext::Field *f : todo) {
        auto *group = new QGroupBox(tr(f->label));
        auto *box = new QVBoxLayout(group);

        const int blank = cdtext::blankTrackCount(params, *f);
        const QString disc = cdtext::discValue(params, *f);
        auto *context = new QLabel(
            blank == 1
                ? tr("1 of %1 tracks has none of its own. The disc is “%2”.")
                      .arg(total)
                      .arg(disc)
                : tr("%1 of %2 tracks have none of their own. The disc is "
                     "“%3”.")
                      .arg(blank)
                      .arg(total)
                      .arg(disc));
        context->setWordWrap(true);
        box->addWidget(context);

        Row row{f, new QLineEdit(tr(f->unknown)), nullptr};

        // A mandatory pack (Title/Performer) can't be dropped while other CD-Text
        // exists, so it gets only the fill row; everything else offers the choice.
        if (f->mandatory) {
            auto *fill = new QHBoxLayout;
            fill->addWidget(new QLabel(tr("Put on those tracks:")));
            fill->addWidget(row.edit);
            box->addLayout(fill);
        } else {
            auto *fillBtn = new QRadioButton(tr("Put on those tracks:"));
            fillBtn->setChecked(true);
            row.drop = new QRadioButton(tr("Don’t burn this field"));

            auto *fill = new QHBoxLayout;
            fill->addWidget(fillBtn);
            fill->addWidget(row.edit);
            box->addLayout(fill);
            box->addWidget(row.drop);

            // Grey the value out while the field is being dropped — it's ignored.
            QObject::connect(row.drop, &QRadioButton::toggled, row.edit,
                             &QWidget::setDisabled);
        }

        root->addWidget(group);
        rows.push_back(row);
    }

    auto *applyChk = new QCheckBox(
        tr("Also save these changes to the project"));
    root->addWidget(applyChk);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                         | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Continue"));
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg,
                     &QDialog::reject);
    root->addWidget(buttons);

    // A blank fill would just recreate the gap cdrdao rejects, so Ok stays
    // disabled until every row is either dropped or filled.
    auto *ok = buttons->button(QDialogButtonBox::Ok);
    auto refreshOk = [ok, &rows] {
        bool ready = true;
        for (const Row &r : rows)
            ready = ready && r.satisfied();
        ok->setEnabled(ready);
    };
    for (const Row &r : rows) {
        QObject::connect(r.edit, &QLineEdit::textChanged, &dlg, refreshOk);
        if (r.drop)
            QObject::connect(r.drop, &QRadioButton::toggled, &dlg, refreshOk);
    }
    refreshOk();

    if (dlg.exec() != QDialog::Accepted)
        return {};

    for (const Row &r : rows) {
        if (r.dropping())
            cdtext::dropField(params, *r.field);
        else
            cdtext::applyTrackFill(params, *r.field, r.edit->text().trimmed());
    }
    return {true, applyChk->isChecked()};
}
