#include "covers.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSet>

#ifdef HAVE_TAGLIB
#include <taglib/fileref.h>
#include <taglib/tvariant.h>
#endif

namespace {

// ---- Embedded pictures --------------------------------------------------------

QList<QByteArray> embeddedPictureBytes(const QString &path)
{
    QList<QByteArray> out;
#ifdef HAVE_TAGLIB
    // TagLib 2's unified picture API covers FLAC picture blocks, ID3 APIC
    // frames, MP4 covr atoms and Vorbis/Opus METADATA_BLOCK_PICTURE alike.
    TagLib::FileRef file(path.toUtf8().constData());
    if (file.isNull())
        return out;
    for (const auto &pic : file.complexProperties("PICTURE")) {
        const auto it = pic.find("data");
        if (it == pic.end())
            continue;
        const TagLib::ByteVector data = it->second.toByteVector();
        if (!data.isEmpty())
            out.append(QByteArray(data.data(), int(data.size())));
    }
#else
    Q_UNUSED(path);
#endif
    return out;
}

// ---- Sidecar images ------------------------------------------------------------
//
// Some sources save the art as a separate image beside the audio rather than
// inside its tags. Files with a known cover name are preferred; failing that,
// a lone image in the folder is used.

const QStringList SIDECAR_STEMS = {"cover", "folder", "front", "album",
                                   "albumart", "art"};
const QStringList IMAGE_EXTS = {"jpg", "jpeg", "png", "webp", "gif", "bmp"};

QList<QByteArray> sidecarPictureBytes(const QString &path)
{
    QList<QByteArray> out;
    QDir dir = QFileInfo(path).absoluteDir();
    QStringList images;
    for (const QFileInfo &fi :
         dir.entryInfoList(QDir::Files, QDir::Name)) {
        if (IMAGE_EXTS.contains(fi.suffix().toLower()))
            images.append(fi.absoluteFilePath());
    }
    QStringList named;
    for (const QString &img : images)
        if (SIDECAR_STEMS.contains(QFileInfo(img).completeBaseName().toLower()))
            named.append(img);
    const QStringList chosen =
        !named.isEmpty() ? named
                         : (images.size() == 1 ? images : QStringList());
    for (const QString &img : chosen) {
        QFile f(img);
        if (f.open(QIODevice::ReadOnly))
            out.append(f.readAll());
    }
    return out;
}

} // namespace

QList<QImage> extractCovers(const QStringList &paths)
{
    QList<QImage> covers;
    QSet<QByteArray> seen;
    for (const QString &path : paths) {
        QList<QByteArray> datas = embeddedPictureBytes(path);
        if (datas.isEmpty())
            datas = sidecarPictureBytes(path);
        for (const QByteArray &data : datas) {
            const QByteArray digest =
                QCryptographicHash::hash(data, QCryptographicHash::Md5);
            if (seen.contains(digest))
                continue;
            seen.insert(digest);
            QImage img;
            if (img.loadFromData(data) && !img.isNull())
                covers.append(img);
        }
    }
    return covers;
}

QImage desaturate(const QImage &img, double keep)
{
    const QImage src = img.convertToFormat(QImage::Format_ARGB32);
    QImage gray = src.convertToFormat(QImage::Format_Grayscale8)
                      .convertToFormat(QImage::Format_ARGB32);
    QPainter p(&gray);
    p.setOpacity(qBound(0.0, keep, 1.0));
    p.drawImage(0, 0, src);
    p.end();
    return gray;
}

QImage blurImage(const QImage &img, double radius)
{
    // QImage has no blur of its own, so the image is wrapped in a one-item
    // QGraphicsScene carrying a QGraphicsBlurEffect (a real separable
    // Gaussian) and re-rendered. The effect bleeds past the item's edges, so
    // callers needing the soft halo should pad the image first. Rendering
    // source → target 1:1 keeps the result aligned and unscaled.
    if (radius <= 0)
        return img;
    const QRectF rect(0, 0, img.width(), img.height());
    QGraphicsScene scene;
    scene.setSceneRect(rect);
    auto *item = new QGraphicsPixmapItem(QPixmap::fromImage(img));
    auto *effect = new QGraphicsBlurEffect;
    effect->setBlurHints(QGraphicsBlurEffect::QualityHint);
    effect->setBlurRadius(radius);
    item->setGraphicsEffect(effect);
    scene.addItem(item);   // scene takes ownership of the item (and effect)

    QImage out(img.width(), img.height(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    scene.render(&p, rect, rect);
    p.end();
    return out;
}

QImage tintImage(const QImage &img, const QColor &color, int strength)
{
    if (strength <= 0)
        return img;
    QImage out = img.convertToFormat(QImage::Format_ARGB32);
    QPainter p(&out);
    p.setCompositionMode(QPainter::CompositionMode_SourceAtop);
    QColor c = color;
    c.setAlpha(qBound(0, strength, 255));
    p.fillRect(out.rect(), c);
    p.end();
    return out;
}

QImage processCover(const QImage &img, double keep, double blurPx,
                    const QString &tint, int tintStrength)
{
    // Memoised on the source image plus every knob that shapes it, so
    // dragging a slider doesn't re-blend/-blur the whole pile per frame.
    static QHash<QString, QImage> cache;
    const int keepStep = int(qRound(qBound(0.0, keep, 1.0) * 20));  // 0.05 steps
    const int blurStep = int(qRound(qMax(0.0, blurPx)));
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(img.cacheKey())
                            .arg(keepStep)
                            .arg(blurStep)
                            .arg(tint)
                            .arg(tintStrength);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    QImage out = desaturate(img, keepStep / 20.0);
    out = blurImage(out, blurStep);
    out = tintImage(out, QColor(tint), tintStrength);
    cache.insert(key, out);
    return out;
}

QImage squareCrop(const QImage &img)
{
    const int w = img.width(), h = img.height();
    if (w == h || w <= 0 || h <= 0)
        return img;
    const int s = qMin(w, h);
    return img.copy((w - s) / 2, (h - s) / 2, s, s);
}
