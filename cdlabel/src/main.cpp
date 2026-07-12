// cdlabel — CD label editor for BinCue Studio.
//
// The editor always shows a left content panel: the album title (with a rich
// override), the track listing and, per track, whether its name and cover
// appear plus a rename. Its own project format (title + those per-track choices
// + the design) saves/opens through that panel.
//
// Standalone: open empty, or open a saved cdlabel project directly.
//     cdlabel [preset/project args below]
//     cdlabel --art-project album.cdlabel.json      # open a saved project
//
// Embedded: BinCue Studio launches the editor with the current project content
// (album title + tracks). If a cdlabel project is saved beside the BinCue
// project, it is loaded and its per-track choices are synced to the current
// tracks (added/removed tracks reconciled by name) — but the file on disk only
// changes when the user saves.
//     cdlabel --project project.bincue.json [--art-project album.cdlabel.json]
//
// Headless: render straight to an image (no window; scripts / preview tooling).
//     cdlabel [--project … | --art-project …] [--preset preset.json] \
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

#include "labelconfig.h"
#include "labeldialog.h"
#include "labelproject.h"
#include "platformstyle.h"
#include "render.h"

namespace {

// Read a BinCue Studio authoring project (album title + tracks) as label
// content: one LabelTrack per track, every name and cover shown by default.
// Which of them actually appear is chosen later in cdlabel's panel, so this no
// longer reads the retired "include_cover" flag.
bool loadBincueContent(const QString &path, LabelProject &out, QString &error)
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
    out.tracks.clear();
    for (const QJsonValue &v : obj.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject track = v.toObject();
        LabelTrack t;
        t.name = track.value(QStringLiteral("title")).toString();
        t.displayName = t.name;
        t.sourcePath = track.value(QStringLiteral("source_path")).toString();
        out.tracks.append(t);
    }
    extractProjectCovers(out.tracks);
    out.design = LabelConfig();
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
    applyPlatformStyle();  // Fusion on Windows 10 so dark mode is honoured.
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
    const QCommandLineOption artOpt(
        QStringLiteral("art-project"),
        QStringLiteral("cdlabel project JSON (title, per-track choices and "
                       "design); loaded if present, and the default Save "
                       "target."),
        QStringLiteral("path"));
    const QCommandLineOption presetOpt(
        QStringLiteral("preset"),
        QStringLiteral("Label preset JSON to load (design only)."),
        QStringLiteral("path"));
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
    parser.addOptions({projectOpt, artOpt, presetOpt, renderOpt, sizeOpt,
                       matteOpt, nameOpt});
    // A lone file argument opens a saved cdlabel project (desktop file-manager
    // association / drag-onto-icon), equivalent to --art-project with no
    // --project.
    parser.addPositionalArgument(
        QStringLiteral("project"),
        QStringLiteral("A cdlabel project (.cdlabel.json) to open."),
        QStringLiteral("[project]"));
    parser.process(app);

    QString error;

    // Where the cdlabel project lives (load source + Save target). From
    // --art-project, else a lone positional file.
    QString artPath = parser.value(artOpt);
    if (artPath.isEmpty() && !parser.positionalArguments().isEmpty())
        artPath = parser.positionalArguments().constFirst();

    // Load the saved cdlabel project, if the file exists.
    LabelProject saved;
    bool haveSaved = false;
    if (!artPath.isEmpty() && QFile::exists(artPath)) {
        if (loadLabelProject(artPath, saved, error))
            haveSaved = true;
        else
            std::fprintf(stderr, "cdlabel: could not open project %s: %s\n",
                         qPrintable(artPath), qPrintable(error));
    }

    // Assemble the working project: BinCue content (with the saved choices
    // synced onto it) when launched embedded, else the saved project on its
    // own, else an empty standalone start.
    LabelProject project;
    if (parser.isSet(projectOpt)) {
        LabelProject content;
        if (!loadBincueContent(parser.value(projectOpt), content, error)) {
            std::fprintf(stderr, "cdlabel: could not load project: %s\n",
                         qPrintable(error));
            return 2;
        }
        project.title = content.title;
        if (haveSaved) {
            project.tracks = syncTracks(saved.tracks, content.tracks);
            project.design = saved.design;
        } else {
            project.tracks = content.tracks;
        }
    } else if (haveSaved) {
        project = saved;   // open the saved project standalone
    } else {
        project.title = QStringLiteral("Audio CD");
    }

    // A preset on the command line overrides the design (design only).
    if (parser.isSet(presetOpt)) {
        LabelConfig presetCfg;
        if (!loadPreset(parser.value(presetOpt), presetCfg, error)) {
            std::fprintf(stderr, "cdlabel: could not load preset: %s\n",
                         qPrintable(error));
            return 2;
        }
        project.design = presetCfg;
    }

    if (parser.isSet(renderOpt)) {
        RenderInput input;
        input.title = project.title;
        input.trackTitles = shownTrackTitles(project.tracks);
        input.covers = shownCovers(project.tracks);
        const LabelConfig &cfg = project.design;
        if (cfg.bgImageEnabled && !cfg.bgImagePath.isEmpty())
            input.bgImage = QImage(cfg.bgImagePath);
        const int side =
            parser.isSet(sizeOpt)
                ? qMax(64, parser.value(sizeOpt).toInt())
                : int(std::lround(cfg.discMm / 25.4 * SAVE_DPI));
        std::fprintf(stderr, "cdlabel: %d distinct covers\n",
                     int(input.covers.size()));
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

    const QString defaultName = parser.isSet(nameOpt) ? parser.value(nameOpt)
                                                      : project.title;
    LabelDialog dialog(project, defaultName, artPath, parser.isSet(projectOpt));
    dialog.exec();
    return 0;
}
