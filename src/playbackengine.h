#pragma once

#include <QList>
#include <QObject>
#include <QVector>

#include <memory>

#include "track.h"

class QTimer;

// Streams the assembled program (every track, sector-aligned, with the exact
// inter-track gaps) to the audio device via miniaudio, so you can hear the disc
// exactly as it will be burned — seamlessly, gaps and all. A background thread
// decodes tracks with ffmpeg and feeds a ring buffer the real-time audio
// callback drains, so playback across track boundaries is gapless.
//
// All positions in the public API are milliseconds within the whole program.
class PlaybackEngine : public QObject
{
    Q_OBJECT
public:
    enum class State { Stopped, Playing, Paused };

    explicit PlaybackEngine(QObject *parent = nullptr);
    ~PlaybackEngine() override;

    // Replace the program. Stops playback and resets the play position to the
    // start of track 0. Cheap to call; only the audio-relevant fields (source
    // path, baked-in gap) and the gap settings matter.
    void setProgram(const QList<Track> &tracks, double gapSeconds);

    // Where a fresh Play (or a Stop) rests, as a track index. No-op while
    // playing or paused.
    void setStartTrack(int index);

    // Jump to the start of a track: moves the resting point while stopped, or
    // seeks there (keeping play/pause) while active.
    void seekToTrack(int index);

    State state() const { return m_state; }
    bool hasProgram() const { return !m_tracks.isEmpty(); }
    int trackCount() const { return int(m_tracks.size()); }
    int currentTrack() const { return m_currentTrack; }

signals:
    void stateChanged(PlaybackEngine::State state);
    void positionChanged(qint64 positionMs, qint64 totalMs);
    void currentTrackChanged(int index); // -1 when there is no program
    void errorOccurred(const QString &message);

public slots:
    void play();
    void pause();
    void togglePlayPause();
    void stop();
    void seek(qint64 positionMs);

private:
    struct Impl;
    class DecodeThread;

    bool ensureDevice();
    void startDecoder(qint64 startFrame);
    void teardownDecoder();
    int trackAtFrame(qint64 frame) const;
    void poll();
    void setState(State state);

    // Program frame == one stereo sample frame at 44100 Hz.
    qint64 msToFrame(qint64 ms) const;
    qint64 frameToMs(qint64 frame) const;

    std::unique_ptr<Impl> d;
    DecodeThread *m_decoder = nullptr;
    QTimer *m_poll = nullptr;

    QList<Track> m_tracks;
    qint64 m_gapFrames = 0;             // inter-track gap, in CD frames
    QVector<qint64> m_boundaries;       // program sample-frame per track start
    qint64 m_totalFrames = 0;

    State m_state = State::Stopped;
    qint64 m_startFrame = 0;            // resting position for Play/Stop
    qint64 m_seekBase = 0;             // program frame the current decode began at
    int m_currentTrack = -1;
};
