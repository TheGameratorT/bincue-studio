#include "labelconfig.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonValue>

// ---- JSON conversion helpers -----------------------------------------------

namespace {

QJsonValue toJsonValue(bool v) { return v; }
QJsonValue toJsonValue(int v) { return v; }
QJsonValue toJsonValue(double v) { return v; }
QJsonValue toJsonValue(const QString &v) { return v; }
QJsonValue toJsonValue(const QList<int> &v)
{
    QJsonArray a;
    for (int i : v)
        a.append(i);
    return a;
}

void fromJsonValue(const QJsonValue &j, bool &out)
{
    if (j.isBool())
        out = j.toBool();
}
void fromJsonValue(const QJsonValue &j, int &out)
{
    if (j.isDouble())
        out = j.toInt();
}
void fromJsonValue(const QJsonValue &j, double &out)
{
    if (j.isDouble())
        out = j.toDouble();
}
void fromJsonValue(const QJsonValue &j, QString &out)
{
    if (j.isString())
        out = j.toString();
}
void fromJsonValue(const QJsonValue &j, QList<int> &out)
{
    if (!j.isArray())
        return;
    out.clear();
    for (const QJsonValue &v : j.toArray())
        if (v.isDouble() && v.toInt() >= 0)
            out.append(v.toInt());
}

} // namespace

QJsonObject LabelConfig::toJson() const
{
    QJsonObject obj;
    // Stamp the format tag so a later load can verify compatibility.
    obj.insert(LABEL_FORMAT_KEY, LABEL_FORMAT_ID);
#define WRITE_FIELD(type, member, key, def) obj.insert(QStringLiteral(key), toJsonValue(member));
    LABEL_CONFIG_FIELDS(WRITE_FIELD)
#undef WRITE_FIELD
    // Remembered file paths are stored with forward slashes so presets stay
    // portable across OSes (no-op on Unix; rewrites Windows backslashes).
    obj.insert(QStringLiteral("bg_image_path"),
               QDir::fromNativeSeparators(bgImagePath));
    return obj;
}

bool LabelConfig::formatCompatible(const QJsonObject &obj, QString &error)
{
    const QString tag = obj.value(LABEL_FORMAT_KEY).toString();
    if (tag.isEmpty()) {
        error = QStringLiteral("This file has no BinCue Studio Label format "
                               "tag (expected \"%1\").")
                    .arg(LABEL_FORMAT_ID);
        return false;
    }
    // Parse the "BCSLv<major>" major number; anything else is foreign.
    if (tag.startsWith(QStringLiteral("BCSLv"))) {
        bool ok = false;
        const int major = tag.mid(5).toInt(&ok);
        if (ok && major == LABEL_FORMAT_MAJOR)
            return true;
        if (ok) {
            error = QStringLiteral(
                        "This preset is format %1, but this version reads %2. "
                        "Use a matching BinCue Studio version.")
                        .arg(tag, LABEL_FORMAT_ID);
            return false;
        }
    }
    error = QStringLiteral("\"%1\" is not a BinCue Studio Label preset "
                           "(expected format %2).")
                .arg(tag, LABEL_FORMAT_ID);
    return false;
}

LabelConfig LabelConfig::fromJson(const QJsonObject &obj)
{
    LabelConfig cfg;
#define READ_FIELD(type, member, key, def)                                     \
    if (obj.contains(QStringLiteral(key)))                                     \
        fromJsonValue(obj.value(QStringLiteral(key)), cfg.member);
    LABEL_CONFIG_FIELDS(READ_FIELD)
#undef READ_FIELD
    return cfg;
}

// ---- Built-in presets --------------------------------------------------------

const QVector<QPair<QString, LabelConfig>> &builtinPresets()
{
    static const QVector<QPair<QString, LabelConfig>> presets = [] {
        QVector<QPair<QString, LabelConfig>> out;

        // Poster: straight title banner + track table over a washed cover
        // mosaic, blurred panels behind both text zones. Order-independent
        // (coverOrder empty = automatic anti-clumping spread) and uses the
        // system default sans for the title with a monospace track table.
        {
            LabelConfig c;
            c.titleLayout = TITLE_STRAIGHT;
            c.titleBand = 0.2;
            c.titleBandEdge = true;
            c.titleUnderline = false;
            c.titleColor = QStringLiteral("#ffffff");
            c.trackLayout = TRACKS_TABLE;
            c.trackBand = 0.3;
            c.trackPad = 0.37;
            c.trackUnderline = false;
            c.trackColor = QStringLiteral("#f2f2f2");
            c.trackFont = QStringLiteral("DejaVu Sans Mono");
            c.coversEnabled = true;
            c.coverBg = COVER_BG_GRID;
            c.coverDesat = 1;
            c.coverFade = 0;
            c.coverFadeColor = QStringLiteral("#0a0d16");
            c.panelTitle = true;
            c.panelTracks = true;
            c.panelMode = QStringLiteral("blur");
            c.panelBlur = 10.0;
            c.panelColor = QStringLiteral("#0a0d16");
            c.panelFade = 160;
            c.hubMode = QStringLiteral("fill");
            c.hubColor = QStringLiteral("#243449");
            c.hubColor2 = QStringLiteral("#0a0d16");
            c.hubGradient = true;
            c.hubRingEnabled = true;
            c.bleedEdge = true;
            c.backdropEnabled = true;
            c.backdropColor = QStringLiteral("#0a0d16");
            out.append({QStringLiteral("Poster"), c});
        }

        // Polaroid: framed covers fanned in a ring over a red radial glow,
        // curved title and small curved tracks pulled toward the hub, a broad
        // faint waveform behind. Order-independent (each distinct cover once,
        // spread automatically) and uses the system default sans throughout.
        {
            LabelConfig c;
            c.coversEnabled = true;
            c.coverBg = COVER_BG_NONE;
            c.coverDesat = 1;
            c.coverFade = 0;
            c.coverFeature = COVER_FEAT_RING;
            c.coverFrame = true;
            c.featureTilt = 16.0;
            c.titleUnderline = false;
            c.titleColor = QStringLiteral("#ffffff");
            c.trackUnderline = false;
            c.trackColor = QStringLiteral("#ffffff");
            c.trackSize = 0.66;
            c.trackOffset = 0.16;
            c.backdropEnabled = true;
            c.backdropColor = QStringLiteral("#aa0000");
            c.backdropColor2 = QStringLiteral("#000000");
            c.backdropGradient = true;
            c.backdropGradientRadial = true;
            c.hubMode = QStringLiteral("fill");
            c.hubColor = QStringLiteral("#000000");
            c.hubRingEnabled = true;
            c.bleedEdge = true;
            c.waveformEnabled = true;
            c.waveformColor = QStringLiteral("#ffff00");
            c.waveformAlpha = 45;
            c.waveformRadius = 0.61;
            c.waveformAmplitude = 0.04;
            out.append({QStringLiteral("Polaroid"), c});
        }
        return out;
    }();
    return presets;
}
