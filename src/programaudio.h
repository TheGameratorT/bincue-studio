#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

#include "track.h"

// Turning a source file into the exact PCM that lands on the disc. Shared by the
// exporter (which writes it to the .bin) and the preview player (which streams
// it to the audio device) so what you hear matches what gets burned.
namespace programaudio {

// Decode a source file to raw interleaved 16-bit little-endian / 44100 Hz /
// stereo PCM using the linked libav* libraries. On failure, returns an empty
// array and sets *error to libav's diagnostics. Thin convenience wrapper around
// AudioDecoder that reads to end of stream in one go.
QByteArray decode(const QString &sourcePath, QString *error);

// Read a source file's duration in seconds via libav (replacing an ffprobe
// call). Returns false and sets *error when the duration can't be determined.
bool probeDuration(const QString &sourcePath, double *outSeconds, QString *error);

// Decode a source file and report how many seconds of trailing near-silence it
// already carries — the value the per-track "Baked-in Gap" wants. Scans the
// decoded PCM backwards from the end, counting sample-frames that stay under a
// low amplitude threshold, and stops at the first audible one. Returns false and
// sets *error if the file can't be decoded.
bool measureTrailingSilence(const QString &sourcePath, double *outSeconds,
                            QString *error);

// Sector-align the decoded PCM, then trim or pad its trailing silence so the
// gap after the track equals the inter-track gap (0 after the last track).
// gapFrames is the inter-track gap in CD frames (redbook::secondsToFrames).
QByteArray fitGap(QByteArray pcm, const Track &track, bool isLast,
                  qint64 gapFrames);

// A source file opened once, decoding Red Book PCM (s16le / 44100 Hz / stereo)
// on demand. This lets the preview start playing without decoding the whole
// file up front and seek by jumping (av_seek_frame) instead of decode-and-drop
// from the start. The exporter and decode() drain it to EOF; only they need the
// whole track in memory.
class AudioDecoder {
public:
    AudioDecoder();
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder &) = delete;
    AudioDecoder &operator=(const AudioDecoder &) = delete;

    // Open the file and set up demux/decode/resample. False + *error on failure.
    bool open(const QString &sourcePath, QString *error);

    // Decode just enough to return up to maxFrames stereo sample-frames of PCM.
    // Returns an empty array only once the stream is exhausted; *error is set
    // (and the array empty) on a decode failure — check it to tell the two apart.
    QByteArray read(int maxFrames, QString *error);

    // Position the next read() so it begins at output sample-frame sampleFrame
    // (44100 Hz). Sample-accurate for native-rate sources (libav seeks land on a
    // packet boundary, so this seeks slightly before the target and decode-drops
    // the remainder). False + *error on failure.
    bool seek(qint64 sampleFrame, QString *error);

    // True once read() has returned everything the stream holds.
    bool atEnd() const;

private:
    struct State;
    std::unique_ptr<State> s;
};

// The streaming counterpart to fitGap(): sector-aligns a track and normalises
// its trailing gap while the PCM arrives in chunks, so the player never holds a
// whole track in memory. Feed decoded chunks to process() as they arrive, then
// call finish() once at end of track. The concatenation of every process()
// result followed by finish() is byte-for-byte identical to fitGap() run over
// the whole-track PCM — that identity is what keeps the preview matching the
// burned image.
//
// startByteOffset is the byte position (a whole sample-frame) within the track's
// decoded PCM that the first process() chunk begins at; pass 0 to stream a track
// from its start, or the mid-track offset a seek landed on so the sector
// alignment and trailing-gap math still reflect the full track length.
class GapProcessor {
public:
    GapProcessor(const Track &track, bool isLast, qint64 gapFrames,
                 qint64 startByteOffset = 0);

    // Pass a decoded chunk; returns the PCM safe to emit now. A tail equal to
    // the most the end might be trimmed is withheld until finish().
    QByteArray process(const QByteArray &chunk);

    // End of track: sector-align and trim/pad the withheld tail. Returns the
    // remaining PCM.
    QByteArray finish();

private:
    qint64 m_delta;          // desiredGap - bakedInGap, in CD frames
    qint64 m_startOffset;    // absolute byte offset of the first fed chunk
    qint64 m_fed = 0;        // bytes handed to process() so far
    qint64 m_hold;           // bytes withheld from the tail
    QByteArray m_buf;        // the withheld tail
};

} // namespace programaudio
