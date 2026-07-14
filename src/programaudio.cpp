#include "programaudio.h"

#include "redbook.h"

#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

using namespace redbook;

namespace programaudio {

namespace {

// The Red Book CD-DA target every source is resampled to.
constexpr int SAMPLE_RATE = 44100;
constexpr int CHANNELS = 2;
constexpr int BYTES_PER_SAMPLE_FRAME = CHANNELS * int(sizeof(int16_t)); // 4

QString avError(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

// RAII holders so every early return frees the libav objects. libav's alloc/free
// come in matched pairs that take a pointer-to-pointer, which these wrap.
struct Format {
    AVFormatContext *ctx = nullptr;
    ~Format() { if (ctx) avformat_close_input(&ctx); }
};
struct Codec {
    AVCodecContext *ctx = nullptr;
    ~Codec() { if (ctx) avcodec_free_context(&ctx); }
};
struct Resampler {
    SwrContext *ctx = nullptr;
    ~Resampler() { if (ctx) swr_free(&ctx); }
};
struct Packet {
    AVPacket *p = av_packet_alloc();
    ~Packet() { if (p) av_packet_free(&p); }
};
struct Frame {
    AVFrame *f = av_frame_alloc();
    ~Frame() { if (f) av_frame_free(&f); }
};

// Open the file and locate its best audio stream. Returns the stream index, or a
// negative AVERROR with *error set.
int openAudio(const QString &path, Format &fmt, int *streamIndex, QString *error)
{
    // libav's file protocol takes the path as UTF-8 on every platform (on
    // Windows it converts UTF-8 to UTF-16 internally for _wopen). QFile::encodeName
    // would hand it the local 8-bit codec instead, so a non-ASCII Windows path
    // (accented file/album names) becomes invalid UTF-8 and open fails with EINVAL.
    const QByteArray p = path.toUtf8();
    int rc = avformat_open_input(&fmt.ctx, p.constData(), nullptr, nullptr);
    if (rc < 0) {
        if (error)
            *error = avError(rc);
        return rc;
    }
    rc = avformat_find_stream_info(fmt.ctx, nullptr);
    if (rc < 0) {
        if (error)
            *error = avError(rc);
        return rc;
    }
    rc = av_find_best_stream(fmt.ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (rc < 0) {
        if (error)
            *error = QStringLiteral("no audio stream found");
        return rc;
    }
    *streamIndex = rc;
    return 0;
}

} // namespace

bool probeDuration(const QString &sourcePath, double *outSeconds, QString *error)
{
    Format fmt;
    int stream = -1;
    if (openAudio(sourcePath, fmt, &stream, error) < 0)
        return false;

    // Prefer the container's overall duration; fall back to the audio stream's
    // own duration (some formats leave the container one unset). Both are in a
    // rational time base, AV_TIME_BASE for the container.
    double seconds = 0.0;
    if (fmt.ctx->duration > 0) {
        seconds = double(fmt.ctx->duration) / AV_TIME_BASE;
    } else {
        const AVStream *st = fmt.ctx->streams[stream];
        if (st->duration > 0)
            seconds = double(st->duration) * av_q2d(st->time_base);
    }
    if (seconds <= 0.0) {
        if (error)
            *error = QStringLiteral("could not determine the track duration");
        return false;
    }
    if (outSeconds)
        *outSeconds = seconds;
    if (error)
        error->clear();
    return true;
}

// -- AudioDecoder -------------------------------------------------------------

struct AudioDecoder::State {
    Format fmt;
    Codec codec;
    Resampler swr;
    Packet pkt;
    Frame frame;
    int stream = -1;
    AVStream *st = nullptr;

    QByteArray pending;   // converted-but-not-yet-returned PCM
    bool finished = false;

    // Absolute output sample-frame position of the next sample swr will emit, and
    // the target below which output is dropped after a seek (0 = keep everything).
    qint64 outPos = 0;
    qint64 dropTarget = 0;
    bool needBase = false; // establish outPos from the next frame's PTS

    int rc = 0;

    // Build the resampler for the current codec context. Recreated after a seek
    // so its filter/delay state starts fresh at the seek point.
    bool setupResampler()
    {
        if (swr.ctx)
            swr_free(&swr.ctx);
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, CHANNELS);
        int r = swr_alloc_set_opts2(&swr.ctx, &outLayout, AV_SAMPLE_FMT_S16,
                                    SAMPLE_RATE, &codec.ctx->ch_layout,
                                    codec.ctx->sample_fmt, codec.ctx->sample_rate,
                                    0, nullptr);
        if (r >= 0)
            r = swr_init(swr.ctx);
        av_channel_layout_uninit(&outLayout);
        return r >= 0;
    }

    // Convert one decoded frame (null flushes swresample), appending its
    // resampled PCM to pending. After a seek, drops output before dropTarget.
    bool convert(const AVFrame *in)
    {
        if (needBase && in) {
            int64_t pts = in->best_effort_timestamp;
            if (pts == AV_NOPTS_VALUE)
                pts = in->pts;
            double sec = 0.0;
            if (pts != AV_NOPTS_VALUE) {
                const int64_t start =
                    (st->start_time != AV_NOPTS_VALUE) ? st->start_time : 0;
                sec = double(pts - start) * av_q2d(st->time_base);
            }
            outPos = qint64(std::llround(sec * SAMPLE_RATE));
            needBase = false;
        }

        const int inSamples = in ? in->nb_samples : 0;
        const int64_t delay = swr_get_delay(swr.ctx, SAMPLE_RATE);
        const int64_t maxOut = av_rescale_rnd(delay + inSamples, SAMPLE_RATE,
                                              codec.ctx->sample_rate, AV_ROUND_UP);
        if (maxOut <= 0)
            return true;
        uint8_t *buf = nullptr;
        int linesize = 0;
        if (av_samples_alloc(&buf, &linesize, CHANNELS, int(maxOut),
                             AV_SAMPLE_FMT_S16, 0) < 0)
            return false;
        const uint8_t **inData = in ? (const uint8_t **)in->extended_data : nullptr;
        const int n = swr_convert(swr.ctx, &buf, int(maxOut), inData, inSamples);
        if (n > 0) {
            const qint64 blockStart = outPos;
            outPos += n;
            // Drop the part of this block that falls before a seek target.
            qint64 keepFrom = 0;
            if (dropTarget > blockStart)
                keepFrom = qMin<qint64>(dropTarget - blockStart, n);
            if (keepFrom < n)
                pending.append(reinterpret_cast<char *>(buf)
                                   + keepFrom * BYTES_PER_SAMPLE_FRAME,
                               qsizetype(n - keepFrom) * BYTES_PER_SAMPLE_FRAME);
        }
        av_freep(&buf);
        return n >= 0;
    }

    // Feed one packet through the decoder, draining every frame it yields.
    bool drainDecoder()
    {
        for (;;) {
            rc = avcodec_receive_frame(codec.ctx, frame.f);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return true;
            if (rc < 0)
                return false;
            const bool ok = convert(frame.f);
            av_frame_unref(frame.f);
            if (!ok)
                return false;
        }
    }

    // Decode packets until pending holds at least targetBytes, or the stream
    // ends (which flushes the decoder and resampler and sets finished).
    bool fill(qsizetype targetBytes)
    {
        while (!finished && pending.size() < targetBytes) {
            const int r = av_read_frame(fmt.ctx, pkt.p);
            if (r < 0) {
                // End of input: flush the decoder, then the resampler's tail.
                avcodec_send_packet(codec.ctx, nullptr);
                if (!drainDecoder())
                    return false;
                while (swr_get_delay(swr.ctx, SAMPLE_RATE) > 0) {
                    const qsizetype before = pending.size();
                    if (!convert(nullptr))
                        return false;
                    if (pending.size() == before) // nothing more came out
                        break;
                }
                finished = true;
                return true;
            }
            bool ok = true;
            if (pkt.p->stream_index == stream) {
                rc = avcodec_send_packet(codec.ctx, pkt.p);
                ok = (rc >= 0) && drainDecoder();
            }
            av_packet_unref(pkt.p);
            if (!ok)
                return false;
        }
        return true;
    }
};

AudioDecoder::AudioDecoder() : s(std::make_unique<State>()) {}
AudioDecoder::~AudioDecoder() = default;

bool AudioDecoder::open(const QString &sourcePath, QString *error)
{
    if (openAudio(sourcePath, s->fmt, &s->stream, error) < 0)
        return false;
    s->st = s->fmt.ctx->streams[s->stream];

    const AVCodecParameters *par = s->st->codecpar;
    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        if (error)
            *error = QStringLiteral("no decoder for this audio format");
        return false;
    }
    s->codec.ctx = avcodec_alloc_context3(dec);
    if (!s->codec.ctx) {
        if (error)
            *error = QStringLiteral("out of memory");
        return false;
    }
    int rc = avcodec_parameters_to_context(s->codec.ctx, par);
    if (rc >= 0)
        rc = avcodec_open2(s->codec.ctx, dec, nullptr);
    if (rc < 0) {
        if (error)
            *error = avError(rc);
        return false;
    }

    // Some decoders don't report a channel layout, only a channel count; give
    // swresample a valid input layout either way.
    if (s->codec.ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&s->codec.ctx->ch_layout,
                                  s->codec.ctx->ch_layout.nb_channels);

    // Resample whatever the file is to the Red Book target: 44100 Hz, stereo,
    // interleaved 16-bit little-endian — exactly the PCM that lands on the disc.
    if (!s->setupResampler()) {
        if (error)
            *error = QStringLiteral("could not initialise the resampler");
        return false;
    }
    if (!s->pkt.p || !s->frame.f) {
        if (error)
            *error = QStringLiteral("out of memory");
        return false;
    }
    if (error)
        error->clear();
    return true;
}

QByteArray AudioDecoder::read(int maxFrames, QString *error)
{
    if (maxFrames <= 0)
        maxFrames = 1;
    const qsizetype targetBytes = qsizetype(maxFrames) * BYTES_PER_SAMPLE_FRAME;
    if (!s->fill(targetBytes)) {
        if (error)
            *error = s->rc < 0 ? avError(s->rc)
                               : QStringLiteral("audio decoding failed");
        return {};
    }
    if (error)
        error->clear();
    const qsizetype take = qMin<qsizetype>(s->pending.size(), targetBytes);
    QByteArray out = s->pending.left(take);
    s->pending.remove(0, take);
    return out;
}

bool AudioDecoder::seek(qint64 sampleFrame, QString *error)
{
    // libav seeks land on a packet boundary, which is not sample-accurate for
    // MP3/AAC, so seek a little before the target and decode-drop the rest.
    constexpr qint64 preRoll = SAMPLE_RATE / 10; // 0.1 s of output
    const qint64 seekOut = qMax<qint64>(0, sampleFrame - preRoll);
    const double seconds = double(seekOut) / SAMPLE_RATE;
    int64_t ts = int64_t(std::llround(seconds / av_q2d(s->st->time_base)));
    if (s->st->start_time != AV_NOPTS_VALUE)
        ts += s->st->start_time;

    int rc = av_seek_frame(s->fmt.ctx, s->stream, ts, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) {
        if (error)
            *error = avError(rc);
        return false;
    }
    avcodec_flush_buffers(s->codec.ctx);
    if (!s->setupResampler()) {
        if (error)
            *error = QStringLiteral("could not initialise the resampler");
        return false;
    }
    s->pending.clear();
    s->finished = false;
    s->dropTarget = sampleFrame;
    s->needBase = true;
    if (error)
        error->clear();
    return true;
}

bool AudioDecoder::atEnd() const
{
    return s->finished && s->pending.isEmpty();
}

QByteArray decode(const QString &sourcePath, QString *error)
{
    AudioDecoder dec;
    if (!dec.open(sourcePath, error))
        return {};

    // Drain to end of stream in generous chunks; the concatenation is identical
    // to decoding the whole file at once.
    QByteArray out;
    for (;;) {
        QString err;
        QByteArray chunk = dec.read(1 << 16, &err);
        if (!err.isEmpty()) {
            if (error)
                *error = err;
            return {};
        }
        if (chunk.isEmpty())
            break;
        out.append(chunk);
    }
    if (error)
        error->clear();
    return out;
}

// -- Gap fitting --------------------------------------------------------------

QByteArray fitGap(QByteArray pcm, const Track &track, bool isLast,
                  qint64 gapFrames)
{
    // Align the decoded audio to a whole CD sector, then trim or fill its
    // baked-in trailing silence so the actual gap after this track equals the
    // inter-track gap (0 after the last).
    const qsizetype remainder = pcm.size() % BYTES_PER_FRAME;
    if (remainder)
        pcm.append(BYTES_PER_FRAME - remainder, '\0');

    const qint64 desiredGap = isLast ? 0 : gapFrames;
    const qint64 bakedFrames = secondsToFrames(track.bakedInGap);
    const qint64 delta = desiredGap - bakedFrames;
    if (delta < 0) {
        // Trim excess trailing silence (never past the start).
        const qint64 trim = qMin(-delta, qint64(pcm.size()) / BYTES_PER_FRAME);
        pcm.chop(trim * BYTES_PER_FRAME);
    } else if (delta > 0) {
        // Pad with silence up to the target gap.
        pcm.append(delta * BYTES_PER_FRAME, '\0');
    }
    return pcm;
}

GapProcessor::GapProcessor(const Track &track, bool isLast, qint64 gapFrames,
                           qint64 startByteOffset)
    : m_startOffset(startByteOffset)
{
    const qint64 desiredGap = isLast ? 0 : gapFrames;
    const qint64 bakedFrames = secondsToFrames(track.bakedInGap);
    m_delta = desiredGap - bakedFrames;
    // Withhold at most what the end might be trimmed (when delta < 0), plus one
    // sector of slack for the alignment pad, so the tail is always recoverable.
    const qint64 maxTrim = m_delta < 0 ? -m_delta : 0;
    m_hold = maxTrim * BYTES_PER_FRAME + BYTES_PER_FRAME;
}

QByteArray GapProcessor::process(const QByteArray &chunk)
{
    m_fed += chunk.size();
    m_buf.append(chunk);
    QByteArray out;
    if (m_buf.size() > m_hold) {
        const qsizetype release = m_buf.size() - m_hold;
        out = m_buf.left(release);
        m_buf.remove(0, release);
    }
    return out;
}

QByteArray GapProcessor::finish()
{
    // The full track length (including anything already emitted). Sector-align it
    // by padding the withheld tail, then trim or pad to hit the target gap.
    const qint64 total = m_startOffset + m_fed;
    QByteArray tail = m_buf;
    const qint64 remainder = total % BYTES_PER_FRAME;
    if (remainder)
        tail.append(BYTES_PER_FRAME - remainder, '\0');

    const qint64 alignedTotal = m_startOffset + m_fed + (remainder ? BYTES_PER_FRAME - remainder : 0);
    if (m_delta < 0) {
        const qint64 trim = qMin(-m_delta, alignedTotal / BYTES_PER_FRAME);
        const qint64 chopBytes = qMin<qint64>(trim * BYTES_PER_FRAME, tail.size());
        tail.chop(chopBytes);
    } else if (m_delta > 0) {
        tail.append(m_delta * BYTES_PER_FRAME, '\0');
    }
    return tail;
}

} // namespace programaudio
