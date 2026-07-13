#include "programaudio.h"

#include "redbook.h"

#include <QFile>

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
    const QByteArray p = QFile::encodeName(path);
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

QByteArray decode(const QString &sourcePath, QString *error)
{
    Format fmt;
    int stream = -1;
    if (openAudio(sourcePath, fmt, &stream, error) < 0)
        return {};

    const AVCodecParameters *par = fmt.ctx->streams[stream]->codecpar;
    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    if (!dec) {
        if (error)
            *error = QStringLiteral("no decoder for this audio format");
        return {};
    }

    Codec codec;
    codec.ctx = avcodec_alloc_context3(dec);
    if (!codec.ctx) {
        if (error)
            *error = QStringLiteral("out of memory");
        return {};
    }
    int rc = avcodec_parameters_to_context(codec.ctx, par);
    if (rc >= 0)
        rc = avcodec_open2(codec.ctx, dec, nullptr);
    if (rc < 0) {
        if (error)
            *error = avError(rc);
        return {};
    }

    // Some decoders don't report a channel layout, only a channel count; give
    // swresample a valid input layout either way.
    if (codec.ctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&codec.ctx->ch_layout,
                                  codec.ctx->ch_layout.nb_channels);

    // Resample whatever the file is to the Red Book target: 44100 Hz, stereo,
    // interleaved 16-bit little-endian — exactly the PCM that lands on the disc.
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, CHANNELS);
    Resampler swr;
    rc = swr_alloc_set_opts2(&swr.ctx, &outLayout, AV_SAMPLE_FMT_S16, SAMPLE_RATE,
                             &codec.ctx->ch_layout, codec.ctx->sample_fmt,
                             codec.ctx->sample_rate, 0, nullptr);
    if (rc >= 0)
        rc = swr_init(swr.ctx);
    av_channel_layout_uninit(&outLayout);
    if (rc < 0) {
        if (error)
            *error = avError(rc);
        return {};
    }

    Packet pkt;
    Frame frame;
    if (!pkt.p || !frame.f) {
        if (error)
            *error = QStringLiteral("out of memory");
        return {};
    }

    QByteArray out;

    // Convert one decoded frame, appending its resampled PCM. A null frame
    // flushes swresample's internal delay at end of stream.
    auto convert = [&](const AVFrame *in) -> bool {
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
        if (n > 0)
            out.append(reinterpret_cast<char *>(buf),
                       qsizetype(n) * CHANNELS * int(sizeof(int16_t)));
        av_freep(&buf);
        return n >= 0;
    };

    // Feed one packet through the decoder, draining every frame it yields.
    auto drainDecoder = [&]() -> bool {
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
    };

    bool ok = true;
    while (ok && av_read_frame(fmt.ctx, pkt.p) >= 0) {
        if (pkt.p->stream_index == stream) {
            rc = avcodec_send_packet(codec.ctx, pkt.p);
            if (rc < 0)
                ok = false;
            else
                ok = drainDecoder();
        }
        av_packet_unref(pkt.p);
    }
    // Flush: signal end of stream to the decoder, drain it, then flush the
    // resampler's tail (it may hold a sample or two of delay across a rate
    // conversion — loop until it has nothing left).
    if (ok) {
        avcodec_send_packet(codec.ctx, nullptr);
        ok = drainDecoder();
    }
    while (ok && swr_get_delay(swr.ctx, SAMPLE_RATE) > 0) {
        const qsizetype before = out.size();
        ok = convert(nullptr);
        if (out.size() == before) // nothing more came out; avoid spinning
            break;
    }

    if (!ok) {
        if (error)
            *error = rc < 0 ? avError(rc)
                            : QStringLiteral("audio decoding failed");
        return {};
    }
    if (error)
        error->clear();
    return out;
}

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

} // namespace programaudio
