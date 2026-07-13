#include "labelproject.h"

#include "covers.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

QJsonObject labelProjectToJson(const LabelProject &proj)
{
    QJsonObject o;
    o.insert(LABEL_FORMAT_KEY, LABEL_PROJECT_FORMAT_ID);
    o.insert(QStringLiteral("title"), proj.title);

    QJsonArray tracks;
    for (const LabelTrack &t : proj.tracks) {
        QJsonObject to;
        to.insert(QStringLiteral("name"), t.name);
        // Forward slashes keep saved projects portable across OSes.
        to.insert(QStringLiteral("source_path"),
                  QDir::fromNativeSeparators(t.sourcePath));
        to.insert(QStringLiteral("display_name"), t.displayName);
        to.insert(QStringLiteral("show_name"), t.showName);
        to.insert(QStringLiteral("show_cover"), t.showCover);
        tracks.append(to);
    }
    o.insert(QStringLiteral("tracks"), tracks);
    o.insert(QStringLiteral("design"), proj.design.toJson());
    return o;
}

void extractProjectCovers(QList<LabelTrack> &tracks)
{
    for (LabelTrack &t : tracks)
        t.cover = t.sourcePath.isEmpty() ? QImage() : extractCover(t.sourcePath);
}

QStringList shownTrackTitles(const QList<LabelTrack> &tracks)
{
    QStringList titles;
    for (const LabelTrack &t : tracks)
        if (t.showName)
            titles.append(t.displayName);
    return titles;
}

QList<QImage> shownCovers(const QList<LabelTrack> &tracks)
{
    QList<QImage> covers;
    for (const LabelTrack &t : tracks) {
        if (!t.showCover || t.cover.isNull())
            continue;
        bool duplicate = false;
        for (const QImage &c : covers)
            if (c == t.cover) {   // pixel compare — track counts are small
                duplicate = true;
                break;
            }
        if (!duplicate)
            covers.append(t.cover);
    }
    return covers;
}

QList<LabelTrack> syncTracks(const QList<LabelTrack> &saved,
                             const QList<LabelTrack> &content)
{
    QHash<QString, LabelTrack> byName;
    for (const LabelTrack &t : saved)
        byName.insert(t.name, t);
    QList<LabelTrack> out;
    out.reserve(content.size());
    for (const LabelTrack &c : content) {
        LabelTrack t = c;   // name, source path and freshly extracted cover
        const auto it = byName.constFind(c.name);
        if (it != byName.constEnd()) {
            t.displayName = it->displayName;
            t.showName = it->showName;
            t.showCover = it->showCover;
        }
        out.append(t);
    }
    return out;
}

QString siblingArtPath(const QString &bincueProjectPath)
{
    if (bincueProjectPath.isEmpty())
        return {};
    QString p = bincueProjectPath;
    if (p.endsWith(QStringLiteral(".bincue.json"), Qt::CaseInsensitive))
        p.chop(QStringLiteral(".bincue.json").size());
    else if (p.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        p.chop(QStringLiteral(".json").size());
    return p + QStringLiteral(".cdlabel.json");
}

bool loadLabelProject(const QString &path, LabelProject &out, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = file.errorString();
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (!doc.isObject()) {
        error = err.errorString();
        return false;
    }
    const QJsonObject obj = doc.object();

    // Verify the format tag: same discipline as LabelConfig::formatCompatible,
    // but for the "BCSLPv<major>" project tag rather than the preset one.
    const QString tag = obj.value(LABEL_FORMAT_KEY).toString();
    bool tagOk = false;
    if (tag.startsWith(QStringLiteral("BCSLPv"))) {
        bool ok = false;
        const int major = tag.mid(6).toInt(&ok);
        tagOk = ok && major == LABEL_PROJECT_FORMAT_MAJOR;
    }
    if (!tagOk) {
        error = tag.isEmpty()
                    ? QStringLiteral("This file is not a cdlabel project "
                                     "(expected format \"%1\").")
                          .arg(LABEL_PROJECT_FORMAT_ID)
                    : QStringLiteral("\"%1\" is not a cdlabel project this "
                                     "version can open (expected \"%2\").")
                          .arg(tag, LABEL_PROJECT_FORMAT_ID);
        return false;
    }

    out.title = obj.value(QStringLiteral("title")).toString();
    out.tracks.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject to = v.toObject();
        LabelTrack t;
        t.name = to.value(QStringLiteral("name")).toString();
        t.sourcePath = to.value(QStringLiteral("source_path")).toString();
        t.displayName =
            to.value(QStringLiteral("display_name")).toString(t.name);
        if (t.displayName.isEmpty())
            t.displayName = t.name;
        t.showName = to.value(QStringLiteral("show_name")).toBool(true);
        t.showCover = to.value(QStringLiteral("show_cover")).toBool(true);
        out.tracks.append(t);
    }
    out.design = LabelConfig::fromJson(obj.value(QStringLiteral("design"))
                                           .toObject());
    extractProjectCovers(out.tracks);
    return true;
}
