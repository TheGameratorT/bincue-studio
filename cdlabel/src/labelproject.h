// A cdlabel project: the label's *content* (album title, the track list with
// each track's display name and whether its name/cover shows) paired with the
// *design* (a LabelConfig). It is cdlabel's own save format, distinct from both
// the design-only presets and the BinCue Studio authoring project that only
// seeds the content.
//
// Per-track choices are keyed by the original track `name`, so reopening a
// project (or re-seeding it from an updated BinCue Studio project) re-attaches
// each choice to the matching track by name. Cover images are not stored: they
// are re-extracted from `sourcePath` on load, so the file stays small and the
// covers track whatever the audio currently holds.
#pragma once

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>

#include "labelconfig.h"

struct LabelTrack {
    QString name;         // original track name — the match key
    QString sourcePath;   // audio file, for cover re-extraction ("" if manual)
    QString displayName;  // what actually prints (defaults to name)
    bool showName = true;
    bool showCover = true;
    QImage cover;         // runtime only: re-extracted, never serialised
};

struct LabelProject {
    QString title;   // album title (the fallback when no title override is set)
    QList<LabelTrack> tracks;
    LabelConfig design;
};

// Format tag stamped into saved cdlabel projects. "BCSLP" = BinCue Studio Label
// Project; the trailing "v<major>" is bumped only for a breaking change, same
// discipline as the preset format tag in labelconfig.h.
inline constexpr int LABEL_PROJECT_FORMAT_MAJOR = 1;
inline const QString LABEL_PROJECT_FORMAT_ID = QStringLiteral("BCSLPv1");

// Serialise the project (title + per-track choices + nested design). Cover
// images are omitted; only each track's source path is kept.
QJsonObject labelProjectToJson(const LabelProject &proj);

// Load a cdlabel project, rejecting a missing/foreign/other-major format tag
// with a human-readable reason in `error`. On success each track's cover is
// re-extracted from its source path.
bool loadLabelProject(const QString &path, LabelProject &out, QString &error);

// Fill each track's `cover` from its `sourcePath` (null when there is no art).
void extractProjectCovers(QList<LabelTrack> &tracks);

// The display names of the tracks whose name is shown, in listing order — what
// the renderer prints as the track list.
QStringList shownTrackTitles(const QList<LabelTrack> &tracks);

// The distinct covers of the tracks whose cover is shown, in track order — what
// the renderer feeds its cover layers. De-duplicated so an album that repeats
// one cover contributes it once, matching extractCovers().
QList<QImage> shownCovers(const QList<LabelTrack> &tracks);

// Reconcile a saved art-project track list against fresh `content` (e.g. the
// tracks of a BinCue Studio project the label was launched with): the result
// follows the content's order and covers, but every track matched by `name`
// keeps its saved display name and name/cover visibility. New content tracks
// are added with defaults; saved tracks absent from the content are dropped.
// This is the in-memory sync on open — nothing is written until the user saves.
QList<LabelTrack> syncTracks(const QList<LabelTrack> &saved,
                             const QList<LabelTrack> &content);

// The art project saved beside a BinCue Studio project: the same path with the
// ".bincue.json" (or ".json") suffix replaced by ".cdlabel.json". Empty in,
// empty out.
QString siblingArtPath(const QString &bincueProjectPath);
