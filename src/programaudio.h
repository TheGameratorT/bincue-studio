#pragma once

#include <QByteArray>
#include <QString>

#include "track.h"

// Turning a source file into the exact PCM that lands on the disc. Shared by the
// exporter (which writes it to the .bin) and the preview player (which streams
// it to the audio device) so what you hear matches what gets burned.
namespace programaudio {

// Decode a source file to raw interleaved 16-bit little-endian / 44100 Hz /
// stereo PCM via the bundled ffmpeg. On failure, returns an empty array and
// sets *error to ffmpeg's diagnostics (or the process error).
QByteArray decode(const QString &sourcePath, QString *error);

// Sector-align the decoded PCM, then trim or pad its trailing silence so the
// gap after the track equals the inter-track gap (0 after the last track).
// gapFrames is the inter-track gap in CD frames (redbook::secondsToFrames).
QByteArray fitGap(QByteArray pcm, const Track &track, bool isLast,
                  qint64 gapFrames);

} // namespace programaudio
