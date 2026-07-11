#pragma once

#include <QString>

// Tag import via TagLib (optional, HAVE_TAGLIB). Mirrors what the legacy
// Python app did with mutagen: per-track fields are filled when files are
// added; album-wide fields are only copied on explicit request.

struct TrackTags {
    QString title;
    QString performer;
    QString songwriter;
    QString isrc;
};

struct AlbumTags {
    QString title;
    QString performer;   // ALBUMARTIST
    QString songwriter;
    QString genre;
    QString year;
    QString catalog;
};

bool tagReaderAvailable();
TrackTags readTrackTags(const QString &path);
AlbumTags readAlbumTags(const QString &path);
