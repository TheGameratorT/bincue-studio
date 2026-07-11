// cdlabel — CD label editor for BinCue Studio.
//
// Standalone mode (default when run on its own): open the editor with a left
// content panel to type the title, track listing and drop in cover images:
//     cdlabel [--preset preset.json]
//
// Embedded mode: BinCue Studio launches the editor with a project, whose title,
// tracks and cover art fill the label (no content panel — the project is the
// source of truth):
//     cdlabel --project project.bincue.json [--preset preset.json]
//
// Headless mode: render a preset straight to an image (no window; used by
// scripts and by BinCue Studio's own preview tooling):
//     cdlabel --project project.bincue.json --preset preset.json \
//             --render out.png [--size 1400] [--matte]

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

#include <cmath>
#include <cstdio>

#include "covers.h"
#include "labelconfig.h"
#include "labeldialog.h"
#include "render.h"

namespace {

struct Project {
    QString title;
    QStringList trackTitles;
    QStringList coverSources;
};

bool loadProject(const QString &path, Project &out, QString &error)
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
    out.title = obj.value(QStringLiteral("album_title")).toString();
    if (out.title.trimmed().isEmpty())
        out.title = QStringLiteral("Audio CD");
    for (const QJsonValue &v :
         obj.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject track = v.toObject();
        out.trackTitles.append(
            track.value(QStringLiteral("title")).toString());
        // Only tracks left ticked in the project's "Art" column contribute
        // covers; every track still shows in the listing text.
        if (track.value(QStringLiteral("include_cover")).toBool(true))
            out.coverSources.append(
                track.value(QStringLiteral("source_path")).toString());
    }
    return true;
}

bool loadPreset(const QString &path, LabelConfig &out, QString &error)
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
    if (!LabelConfig::formatCompatible(doc.object(), error))
        return false;
    out = LabelConfig::fromJson(doc.object());
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    // Headless renders shouldn't need a display server.
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--render") == 0 && !qEnvironmentVariableIsSet(
                "QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("cdlabel"));
    QApplication::setApplicationVersion(QStringLiteral("2.0"));

    // The app icon: the installed hicolor theme copy when packaged, else the
    // same SVG embedded as a resource (dev builds run uninstalled).
    QApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral("cdlabel"),
                         QIcon(QStringLiteral(":/icons/cdlabel.svg"))));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("CD label editor for BinCue Studio"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption projectOpt(
        QStringLiteral("project"),
        QStringLiteral("BinCue Studio project JSON (album title, tracks, "
                       "cover sources)."),
        QStringLiteral("path"));
    const QCommandLineOption presetOpt(
        QStringLiteral("preset"),
        QStringLiteral("Label preset JSON to load."), QStringLiteral("path"));
    const QCommandLineOption renderOpt(
        QStringLiteral("render"),
        QStringLiteral("Render to this image file and exit (headless)."),
        QStringLiteral("out.png"));
    const QCommandLineOption sizeOpt(
        QStringLiteral("size"),
        QStringLiteral("Rendered image side in pixels (default: the disc "
                       "size at 600 DPI)."),
        QStringLiteral("px"));
    const QCommandLineOption matteOpt(
        QStringLiteral("matte"),
        QStringLiteral("Composite the render onto a grey disc so the "
                       "transparent label reads like print."));
    const QCommandLineOption nameOpt(
        QStringLiteral("name"),
        QStringLiteral("Default file name offered when saving the label."),
        QStringLiteral("name"));
    parser.addOptions(
        {projectOpt, presetOpt, renderOpt, sizeOpt, matteOpt, nameOpt});
    parser.process(app);

    Project project;
    QString error;
    // No project on the command line means the editor was launched on its own:
    // run standalone, with a content panel to enter the title/tracks/covers.
    const bool standalone = !parser.isSet(projectOpt);
    if (parser.isSet(projectOpt)) {
        if (!loadProject(parser.value(projectOpt), project, error)) {
            std::fprintf(stderr, "cdlabel: could not load project: %s\n",
                         qPrintable(error));
            return 2;
        }
    } else {
        project.title = QStringLiteral("Audio CD");
    }

    LabelConfig cfg;
    const bool hasPreset = parser.isSet(presetOpt);
    if (hasPreset && !loadPreset(parser.value(presetOpt), cfg, error)) {
        std::fprintf(stderr, "cdlabel: could not load preset: %s\n",
                     qPrintable(error));
        return 2;
    }

    const QList<QImage> covers = extractCovers(project.coverSources);

    if (parser.isSet(renderOpt)) {
        RenderInput input;
        input.title = project.title;
        input.trackTitles = project.trackTitles;
        input.covers = covers;
        if (cfg.bgImageEnabled && !cfg.bgImagePath.isEmpty())
            input.bgImage = QImage(cfg.bgImagePath);
        const int side =
            parser.isSet(sizeOpt)
                ? qMax(64, parser.value(sizeOpt).toInt())
                : int(std::lround(cfg.discMm / 25.4 * SAVE_DPI));
        std::fprintf(stderr,
                     "cdlabel: %d distinct covers from %d sources\n",
                     int(covers.size()), int(project.coverSources.size()));
        QImage img = renderLabelImage(input, cfg, side);
        if (parser.isSet(matteOpt)) {
            QImage canvas(side, side, QImage::Format_ARGB32);
            canvas.fill(Qt::transparent);
            QPainter p(&canvas);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#cfcfcf")));
            p.drawEllipse(QRectF(0, 0, side, side));
            p.drawImage(0, 0, img);
            p.end();
            img = canvas;
        }
        const QString out = parser.value(renderOpt);
        if (!img.save(out)) {
            std::fprintf(stderr, "cdlabel: could not write %s\n",
                         qPrintable(out));
            return 1;
        }
        std::fprintf(stderr, "cdlabel: wrote %s (%dx%d)\n", qPrintable(out),
                     side, side);
        return 0;
    }

    LabelDialog dialog(project.title, project.trackTitles, covers,
                       parser.isSet(nameOpt) ? parser.value(nameOpt)
                                             : project.title,
                       standalone);
    if (hasPreset)
        dialog.applyConfig(cfg);
    dialog.exec();
    return 0;
}
