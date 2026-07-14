#pragma once

#include <QDialog>
#include <QString>

#include "exportworker.h"

class QLineEdit;
class QToolButton;

// Disc-wide editor for every disc field except the two the main Disc panel
// keeps (title and performer): the songwriter, genre, year and catalog, plus
// the less player-visible CD-Text packs (composer, arranger, message) and the
// disc-only DISC_ID. It carries the same ⚠ badges as the panel — computed from
// the shared cdtext model against the current tracks — so a partially used pack
// looks the same wherever you see it. Construct it from the current metadata,
// exec() it, and read the fields back on Accept, mirroring TrackDetailsDialog
// for the per-track fields.
class DiscDetailsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DiscDetailsDialog(const ExportWorker::Params &metadata,
                               QWidget *parent = nullptr);

    QString songwriter() const;
    QString genre() const;
    QString year() const;
    QString catalog() const;
    QString composer() const;
    QString arranger() const;
    QString message() const;
    QString discId() const;

    // True if any disc-details field carries a value, i.e. the panel button
    // should be badged to show there is something behind it.
    static bool hasDetails(const ExportWorker::Params &metadata);

private:
    // Re-evaluate the per-field badges against the values currently typed.
    void updateBadges();

    // Tracks plus the disc values, kept in sync with the editors so the badges
    // reflect what's typed right now.
    ExportWorker::Params m_metadata;
    QLineEdit *m_songwriter = nullptr;
    QLineEdit *m_genre = nullptr;
    QLineEdit *m_year = nullptr;
    QLineEdit *m_catalog = nullptr;
    QLineEdit *m_composer = nullptr;
    QLineEdit *m_arranger = nullptr;
    QLineEdit *m_message = nullptr;
    QLineEdit *m_discId = nullptr;
    QToolButton *m_songwriterErr = nullptr;
    QToolButton *m_composerErr = nullptr;
    QToolButton *m_arrangerErr = nullptr;
    QToolButton *m_messageErr = nullptr;
};
