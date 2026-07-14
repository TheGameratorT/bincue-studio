#pragma once

#include <QDialog>
#include <QString>

#include "exportworker.h"

class QLineEdit;
class QToolButton;

// Disc-wide editor for the CD-Text pack types that don't earn their own row in
// the main Disc panel: the less player-visible ones (composer, arranger,
// message) and the disc-only DISC_ID. Title, performer, songwriter, genre, year
// and catalog stay on the panel and aren't repeated here. It carries the same
// ⚠ badges as the panel — computed from the shared cdtext model against the
// current tracks — so a partially used pack looks the same wherever you see it.
// Construct it from the current metadata, exec() it, and read the fields back on
// Accept, mirroring TrackDetailsDialog for the per-track fields.
class DiscDetailsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DiscDetailsDialog(const ExportWorker::Params &metadata,
                               QWidget *parent = nullptr);

    QString composer() const;
    QString arranger() const;
    QString message() const;
    QString discId() const;

    // True if any of these disc fields carries a value, i.e. the panel button
    // should be badged to show there is something behind it.
    static bool hasDetails(const QString &composer, const QString &arranger,
                           const QString &message, const QString &discId);

private:
    // Re-evaluate the per-field badges against the values currently typed.
    void updateBadges();

    // Tracks plus the disc values, kept in sync with the editors so the badges
    // reflect what's typed right now.
    ExportWorker::Params m_metadata;
    QLineEdit *m_composer = nullptr;
    QLineEdit *m_arranger = nullptr;
    QLineEdit *m_message = nullptr;
    QLineEdit *m_discId = nullptr;
    QToolButton *m_composerErr = nullptr;
    QToolButton *m_arrangerErr = nullptr;
    QToolButton *m_messageErr = nullptr;
};
