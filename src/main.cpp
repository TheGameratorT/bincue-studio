// BinCue Studio — a small K3b-style Audio CD project tool.
//
// Builds a single-file CD-DA image (BIN + CUE) from FLAC (or other libav-
// readable audio) tracks, with:
//
//   - CD-Text style metadata: disc Title/Performer/Genre/Year, and per-track
//     Title/Performer (editable — this is what gets "burned" into the cue,
//     independent of the source filenames or original tags).
//   - A global "Gap between tracks" (default 2s) sets the gap every track
//     ends up with, and a per-track "Baked-in Gap" says how much trailing
//     silence each source already has. On export each track's tail is trimmed
//     or filled to hit that gap (0 after the last track). Every track from
//     the second onward is then marked with that gap as its pregap
//     (INDEX 00 = INDEX 01 - gap) per the Red Book convention. Track 1 gets a
//     separate lead-in "Pre-gap" (PREGAP), which the Red Book fixes at 2s.
//   - A live capacity meter comparing total programme time against a 74 or
//     80 minute disc.
//   - Save/Open Project (JSON) — the same format the legacy Python app used.
//   - Tracks are only actually decoded at export time; nothing is transcoded
//     just to edit metadata or reorder.
//
// Audio is decoded with the linked libav* libraries (libavformat / libavcodec
// / libswresample). TagLib (optional, compile
// time) auto-fills per-track Title/Performer/Songwriter/ISRC from existing
// tags when files are added; album-wide fields are filled from a chosen
// track's tags with one button instead.
//
// The CD label editor is the sibling cdlabel executable, built by the same
// CMake project and launched as a separate process with the project handed
// over as JSON.

#include <QApplication>
#include <QIcon>

#include "mainwindow.h"
#include "platformstyle.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    applyPlatformStyle();  // Fusion on Windows 10 so dark mode is honoured.
    QApplication::setOrganizationName(QStringLiteral("BinCue Studio"));
    QApplication::setApplicationName(QStringLiteral("BinCue Studio"));
    QApplication::setApplicationVersion(QStringLiteral(BINCUE_VERSION));

    // The app icon: the installed hicolor theme copy when packaged, else the
    // same SVG embedded as a resource (dev builds run uninstalled).
    QApplication::setWindowIcon(
        QIcon::fromTheme(QStringLiteral("bincue-studio"),
                         QIcon(QStringLiteral(":/icons/bincue-studio.svg"))));

    MainWindow window;
    window.show();
    return app.exec();
}
