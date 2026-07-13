#pragma once

#include <QDialog>

#include "track.h"

class QCheckBox;
class QLineEdit;

// Per-track editor for the CD-Text and subchannel fields that don't earn a
// column in the main table: the less player-visible CD-Text pack types
// (songwriter, composer, arranger, message), the ISRC, and the Red Book
// subchannel flags. Title and performer stay in the table and aren't repeated
// here. Construct it from a track, exec() it, and read back track() on Accept.
class TrackDetailsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit TrackDetailsDialog(const Track &track, QWidget *parent = nullptr);

    // The input track with the detail fields overwritten from the widgets;
    // fields this dialog doesn't touch (title, performer, path…) are preserved.
    Track track() const;

    // True if a track carries any field this dialog edits, i.e. anything beyond
    // the table columns. Used to badge the row's "Details…" button.
    static bool hasDetails(const Track &track);

private:
    Track m_track;
    QLineEdit *m_songwriter = nullptr;
    QLineEdit *m_composer = nullptr;
    QLineEdit *m_arranger = nullptr;
    QLineEdit *m_message = nullptr;
    QLineEdit *m_isrc = nullptr;
    QCheckBox *m_copyPermitted = nullptr;
    QCheckBox *m_preEmphasis = nullptr;
    QCheckBox *m_fourChannel = nullptr;
};
