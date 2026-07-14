#include "playbackengine.h"

#include "programaudio.h"
#include "redbook.h"

#include <QThread>
#include <QTimer>

#include <atomic>
#include <cmath>
#include <cstring>

#include <miniaudio.h>

using namespace redbook;

namespace {
constexpr int SAMPLE_RATE = 44100;
constexpr int CHANNELS = 2;
constexpr int BYTES_PER_SAMPLE_FRAME = CHANNELS * int(sizeof(qint16)); // 4
// One CD frame (sector) is 1/75 s of audio = 588 stereo sample frames.
constexpr qint64 SF_PER_CD_FRAME = SAMPLE_RATE / FRAME_RATE;           // 588
constexpr ma_uint32 RB_CAPACITY_FRAMES = SAMPLE_RATE * 4;             // ~4 s
constexpr ma_uint32 FEED_CHUNK_FRAMES = 4096;
} // namespace

// -- Ring buffer + device state, hidden from the header (no miniaudio there) ---
struct PlaybackEngine::Impl {
    ma_device device{};
    ma_pcm_rb rb{};
    bool deviceReady = false;
    bool rbReady = false;
    std::atomic<qint64> framesOut{0}; // stereo sample frames the device has played

    // Real-time audio thread: drain whatever the decoder has queued, pad the
    // rest with silence on underrun (or at the very end of the program).
    static void callback(ma_device *dev, void *out, const void *, ma_uint32 frameCount)
    {
        auto *self = static_cast<Impl *>(dev->pUserData);
        auto *dst = static_cast<qint16 *>(out);
        ma_uint32 got = 0;
        while (got < frameCount) {
            ma_uint32 n = frameCount - got;
            void *src = nullptr;
            if (ma_pcm_rb_acquire_read(&self->rb, &n, &src) != MA_SUCCESS || n == 0)
                break;
            std::memcpy(dst + qint64(got) * CHANNELS, src,
                        size_t(n) * BYTES_PER_SAMPLE_FRAME);
            ma_pcm_rb_commit_read(&self->rb, n);
            got += n;
        }
        if (got < frameCount)
            std::memset(dst + qint64(got) * CHANNELS, 0,
                        size_t(frameCount - got) * BYTES_PER_SAMPLE_FRAME);
        self->framesOut.fetch_add(got, std::memory_order_relaxed);
    }
};

// -- Background decoder: ffmpeg-decode each track, apply the gap, feed the rb ---
class PlaybackEngine::DecodeThread : public QThread
{
public:
    DecodeThread(QList<Track> tracks, qint64 gapFrames, int startTrack,
                 qint64 startByteOffset, ma_pcm_rb *rb)
        : m_tracks(std::move(tracks)), m_gapFrames(gapFrames),
          m_startTrack(startTrack), m_startByteOffset(startByteOffset), m_rb(rb)
    {
    }

    void abort() { m_abort.store(true); }
    bool failed() const { return m_failed.load(); }
    QString errorText() const { return m_errorText; }

protected:
    void run() override
    {
        const int n = int(m_tracks.size());
        for (int i = m_startTrack; i < n && !m_abort.load(); ++i) {
            programaudio::AudioDecoder dec;
            QString err;
            if (!dec.open(m_tracks[i].sourcePath, &err)) {
                fail(err);
                return;
            }

            // Where this track's audio begins. The first track resumes at the
            // mid-track offset a seek/start landed on (kept on a sample-frame
            // edge); later tracks stream from the top. The gap processor is told
            // this offset so its sector alignment reflects the full track length.
            qint64 startOffset =
                (i == m_startTrack)
                    ? qMax<qint64>(0, m_startByteOffset)
                    : 0;
            startOffset -= startOffset % BYTES_PER_SAMPLE_FRAME;
            if (startOffset > 0
                && !dec.seek(startOffset / BYTES_PER_SAMPLE_FRAME, &err)) {
                fail(err);
                return;
            }

            programaudio::GapProcessor gap(m_tracks[i], i == n - 1, m_gapFrames,
                                           startOffset);
            for (;;) {
                if (m_abort.load())
                    return;
                QByteArray chunk = dec.read(FEED_CHUNK_FRAMES, &err);
                if (!err.isEmpty()) {
                    fail(err);
                    return;
                }
                const bool eof = chunk.isEmpty();
                const QByteArray out = eof ? gap.finish() : gap.process(chunk);
                if (!feed(out))
                    return; // aborted mid-feed
                if (eof)
                    break;
            }
        }
    }

private:
    // Push a PCM chunk into the ring buffer, blocking while it's full (or
    // paused). Returns false if aborted mid-feed.
    bool feed(const QByteArray &pcm)
    {
        const char *data = pcm.constData();
        qsizetype pos = 0;
        while (pos < pcm.size()) {
            if (m_abort.load())
                return false;
            ma_uint32 want = ma_uint32(qMin<qsizetype>(
                (pcm.size() - pos) / BYTES_PER_SAMPLE_FRAME, FEED_CHUNK_FRAMES));
            ma_uint32 got = want;
            void *dst = nullptr;
            if (ma_pcm_rb_acquire_write(m_rb, &got, &dst) != MA_SUCCESS)
                return false;
            if (got == 0) { // buffer full (or paused): wait and retry
                msleep(4);
                continue;
            }
            std::memcpy(dst, data + pos, size_t(got) * BYTES_PER_SAMPLE_FRAME);
            ma_pcm_rb_commit_write(m_rb, got);
            pos += qsizetype(got) * BYTES_PER_SAMPLE_FRAME;
        }
        return true;
    }

    void fail(const QString &err)
    {
        m_errorText = err;
        m_failed.store(true);
    }

    QList<Track> m_tracks;
    qint64 m_gapFrames;
    int m_startTrack;
    qint64 m_startByteOffset;
    ma_pcm_rb *m_rb;
    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_failed{false};
    QString m_errorText;
};

// -----------------------------------------------------------------------------

PlaybackEngine::PlaybackEngine(QObject *parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    m_poll = new QTimer(this);
    m_poll->setInterval(100);
    connect(m_poll, &QTimer::timeout, this, &PlaybackEngine::poll);
}

PlaybackEngine::~PlaybackEngine()
{
    if (d->deviceReady)
        ma_device_stop(&d->device);
    teardownDecoder();
    if (d->deviceReady)
        ma_device_uninit(&d->device);
    if (d->rbReady)
        ma_pcm_rb_uninit(&d->rb);
}

qint64 PlaybackEngine::msToFrame(qint64 ms) const
{
    return ms * SAMPLE_RATE / 1000;
}

qint64 PlaybackEngine::frameToMs(qint64 frame) const
{
    return frame * 1000 / SAMPLE_RATE;
}

void PlaybackEngine::setProgram(const QList<Track> &tracks, double gapSeconds)
{
    stop();
    teardownDecoder();

    m_tracks = tracks;
    m_gapFrames = secondsToFrames(gapSeconds);

    // Predict each track's on-disc length from its metadata duration, mirroring
    // the exporter (sector-align the audio, then trim/pad to the target gap).
    // Used for the timeline and for mapping a seek position back to a track.
    m_boundaries.clear();
    m_boundaries.reserve(int(m_tracks.size()) + 1);
    qint64 acc = 0;
    const int n = int(m_tracks.size());
    for (int i = 0; i < n; ++i) {
        m_boundaries.append(acc);
        const qint64 audio =
            qint64(std::llround(m_tracks[i].durationSeconds * SAMPLE_RATE));
        const qint64 aligned =
            ((audio + SF_PER_CD_FRAME - 1) / SF_PER_CD_FRAME) * SF_PER_CD_FRAME;
        const qint64 desiredGap = (i == n - 1) ? 0 : m_gapFrames;
        const qint64 bakedFrames = secondsToFrames(m_tracks[i].bakedInGap);
        qint64 processed = aligned + (desiredGap - bakedFrames) * SF_PER_CD_FRAME;
        if (processed < 0)
            processed = 0;
        acc += processed;
    }
    m_boundaries.append(acc);
    m_totalFrames = acc;

    m_startFrame = 0;
    m_seekBase = 0;
    d->framesOut.store(0);
    m_currentTrack = m_tracks.isEmpty() ? -1 : 0;

    setState(State::Stopped);
    emit currentTrackChanged(m_currentTrack);
    emit positionChanged(0, frameToMs(m_totalFrames));
}

int PlaybackEngine::trackAtFrame(qint64 frame) const
{
    const int n = int(m_tracks.size());
    if (n == 0)
        return -1;
    int t = 0;
    for (int i = 0; i < n; ++i) {
        if (m_boundaries[i] <= frame)
            t = i;
        else
            break;
    }
    return t;
}

bool PlaybackEngine::ensureDevice()
{
    if (d->deviceReady)
        return true;

    if (!d->rbReady) {
        if (ma_pcm_rb_init(ma_format_s16, CHANNELS, RB_CAPACITY_FRAMES, nullptr,
                           nullptr, &d->rb)
            != MA_SUCCESS)
            return false;
        d->rbReady = true;
    }

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_s16;
    cfg.playback.channels = CHANNELS;
    cfg.sampleRate = SAMPLE_RATE;
    cfg.dataCallback = &Impl::callback;
    cfg.pUserData = d.get();
    if (ma_device_init(nullptr, &cfg, &d->device) != MA_SUCCESS)
        return false;
    d->deviceReady = true;
    return true;
}

void PlaybackEngine::startDecoder(qint64 startFrame)
{
    teardownDecoder();
    ma_pcm_rb_reset(&d->rb);
    d->framesOut.store(0);
    m_seekBase = startFrame;

    const int startTrack = qMax(0, trackAtFrame(startFrame));
    const qint64 offsetFrames = startFrame - m_boundaries[startTrack];
    const qint64 offsetBytes = offsetFrames * BYTES_PER_SAMPLE_FRAME;

    m_decoder = new DecodeThread(m_tracks, m_gapFrames, startTrack, offsetBytes,
                                 &d->rb);
    m_decoder->start();
}

void PlaybackEngine::teardownDecoder()
{
    if (!m_decoder)
        return;
    m_decoder->abort();
    m_decoder->wait();
    delete m_decoder;
    m_decoder = nullptr;
}

void PlaybackEngine::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    if (state == State::Playing)
        m_poll->start();
    else
        m_poll->stop();
    emit stateChanged(m_state);
}

void PlaybackEngine::play()
{
    if (m_tracks.isEmpty() || m_state == State::Playing)
        return;

    if (m_state == State::Paused) {
        if (ma_device_start(&d->device) != MA_SUCCESS) {
            emit errorOccurred(tr("Could not resume audio playback."));
            return;
        }
        setState(State::Playing);
        return;
    }

    if (!ensureDevice()) {
        emit errorOccurred(tr("No audio output device is available."));
        return;
    }
    startDecoder(m_startFrame);
    if (ma_device_start(&d->device) != MA_SUCCESS) {
        teardownDecoder();
        emit errorOccurred(tr("Could not start audio playback."));
        return;
    }
    setState(State::Playing);
}

void PlaybackEngine::pause()
{
    if (m_state != State::Playing)
        return;
    ma_device_stop(&d->device);
    setState(State::Paused);
}

void PlaybackEngine::togglePlayPause()
{
    if (m_state == State::Playing)
        pause();
    else
        play();
}

void PlaybackEngine::stop()
{
    const bool wasActive = (m_state != State::Stopped);
    if (d->deviceReady)
        ma_device_stop(&d->device);
    teardownDecoder();
    if (d->rbReady)
        ma_pcm_rb_reset(&d->rb);
    d->framesOut.store(0);
    m_seekBase = m_startFrame;

    const int t = trackAtFrame(m_startFrame);
    setState(State::Stopped);
    if (wasActive || m_currentTrack != t) {
        m_currentTrack = t;
        emit currentTrackChanged(m_currentTrack);
    }
    emit positionChanged(frameToMs(m_startFrame), frameToMs(m_totalFrames));
}

void PlaybackEngine::seek(qint64 positionMs)
{
    if (m_tracks.isEmpty())
        return;
    qint64 frame = qBound<qint64>(0, msToFrame(positionMs), m_totalFrames);

    if (m_state == State::Stopped) {
        m_startFrame = frame;
        m_seekBase = frame;
        const int t = trackAtFrame(frame);
        if (t != m_currentTrack) {
            m_currentTrack = t;
            emit currentTrackChanged(m_currentTrack);
        }
        emit positionChanged(frameToMs(frame), frameToMs(m_totalFrames));
        return;
    }

    const State prev = m_state;
    if (d->deviceReady)
        ma_device_stop(&d->device);
    startDecoder(frame);
    if (prev == State::Playing)
        ma_device_start(&d->device);
    emit positionChanged(frameToMs(frame), frameToMs(m_totalFrames));
    const int t = trackAtFrame(frame);
    if (t != m_currentTrack) {
        m_currentTrack = t;
        emit currentTrackChanged(m_currentTrack);
    }
}

void PlaybackEngine::setStartTrack(int index)
{
    if (index < 0 || index >= int(m_tracks.size()))
        return;
    if (m_state != State::Stopped)
        return;
    m_startFrame = m_boundaries[index];
    m_seekBase = m_startFrame;
    if (m_currentTrack != index) {
        m_currentTrack = index;
        emit currentTrackChanged(m_currentTrack);
    }
    emit positionChanged(frameToMs(m_startFrame), frameToMs(m_totalFrames));
}

void PlaybackEngine::seekToTrack(int index)
{
    if (index < 0 || index >= int(m_tracks.size()))
        return;
    if (m_state == State::Stopped)
        setStartTrack(index);
    else
        seek(frameToMs(m_boundaries[index]));
}

void PlaybackEngine::poll()
{
    if (m_state != State::Playing)
        return;

    if (m_decoder && m_decoder->failed()) {
        const QString msg = m_decoder->errorText();
        stop();
        emit errorOccurred(tr("Playback failed:\n%1").arg(msg));
        return;
    }

    const qint64 frame =
        qBound<qint64>(0, m_seekBase + d->framesOut.load(std::memory_order_relaxed),
                       m_totalFrames);
    emit positionChanged(frameToMs(frame), frameToMs(m_totalFrames));

    const int t = trackAtFrame(frame);
    if (t != m_currentTrack) {
        m_currentTrack = t;
        emit currentTrackChanged(m_currentTrack);
    }

    // Reached the end: the decoder is done and the buffer has drained.
    if (m_decoder && m_decoder->isFinished()
        && ma_pcm_rb_available_read(&d->rb) == 0) {
        emit positionChanged(frameToMs(m_totalFrames), frameToMs(m_totalFrames));
        stop();
    }
}
