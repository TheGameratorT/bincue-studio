#include "previewwidget.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

PreviewWidget::PreviewWidget(const RenderInput &input, const LabelConfig &cfg,
                             QWidget *parent)
    : QWidget(parent), m_input(input), m_cfg(cfg)
{
    setMinimumSize(380, 380);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::StrongFocus);
    // Re-render at full resolution once edits pause; a cheap draft carries
    // the view in the meantime so slider dragging stays responsive.
    m_fullTimer.setSingleShot(true);
    connect(&m_fullTimer, &QTimer::timeout, this, &PreviewWidget::renderFull);
}

void PreviewWidget::setConfig(const LabelConfig &cfg)
{
    m_cfg = cfg;
    invalidate();
}

void PreviewWidget::setBgImage(const QImage &img)
{
    m_input.bgImage = img;
    invalidate();
}

void PreviewWidget::setContent(const QString &title,
                               const QStringList &trackTitles,
                               const QList<QImage> &covers)
{
    m_input.title = title;
    m_input.trackTitles = trackTitles;
    m_input.covers = covers;
    invalidate();
}

void PreviewWidget::invalidate()
{
    m_base = QImage();
    m_basePx = 0;
    m_dispCache.clear();
    m_fullTimer.start(140);
    update();
}

void PreviewWidget::buildBase(int px)
{
    QImage img(px, px, QImage::Format_ARGB32);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.scale(px / PREVIEW_CANVAS_PX, px / PREVIEW_CANVAS_PX);
    drawLabel(p, PREVIEW_CANVAS_PX, m_input, m_cfg, false);
    p.end();
    m_base = img;
    m_basePx = px;
    m_dispCache.clear();
}

void PreviewWidget::renderFull()
{
    buildBase(RENDER_PX);
    update();
}

QImage PreviewWidget::display(int px)
{
    if (m_base.isNull()) {
        buildBase(DRAFT_PX);          // fast draft...
        if (!m_fullTimer.isActive())
            m_fullTimer.start(140);   // ...crisp full render follows
    }
    px = qMax(1, px);
    if (px == m_basePx)
        return m_base;
    auto it = m_dispCache.constFind(px);
    if (it != m_dispCache.constEnd())
        return *it;
    if (m_dispCache.size() > 6)       // keep the cache small
        m_dispCache.clear();
    const QImage img = m_base.scaled(px, px, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
    m_dispCache.insert(px, img);
    return img;
}

double PreviewWidget::fitSide() const
{
    return qMax(1.0, double(qMin(width(), height())) - 16.0);
}

void PreviewWidget::clampPan()
{
    // When the disc is larger than the viewport you may pan until its edge
    // meets the viewport edge; when it fits, it stays centred.
    const double side = fitSide() * m_zoom;
    const double xlimit = qMax(0.0, (side - width()) / 2.0);
    const double ylimit = qMax(0.0, (side - height()) / 2.0);
    m_pan.setX(qBound(-xlimit, m_pan.x(), xlimit));
    m_pan.setY(qBound(-ylimit, m_pan.y(), ylimit));
}

void PreviewWidget::fitView()
{
    m_zoom = 1.0;
    m_pan = QPointF(0.0, 0.0);
    update();
}

void PreviewWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(60, 60, 60));   // dark backdrop
    const double side = fitSide() * m_zoom;
    if (side <= 0)
        return;
    const double cx = width() / 2.0 + m_pan.x();
    const double cy = height() / 2.0 + m_pan.y();
    const QPointF origin(qRound(cx - side / 2.0), qRound(cy - side / 2.0));
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(origin, display(int(qRound(side))));
}

void PreviewWidget::wheelEvent(QWheelEvent *event)
{
    const double side = fitSide() * m_zoom;
    const double cx = width() / 2.0 + m_pan.x();
    const double cy = height() / 2.0 + m_pan.y();
    const QPointF origin(cx - side / 2.0, cy - side / 2.0);
    const QPointF pos = event->position();
    // Fraction of the disc square under the cursor — fixed across the zoom.
    const double fx = (pos.x() - origin.x()) / side;
    const double fy = (pos.y() - origin.y()) / side;
    const double step = std::pow(2.0, event->angleDelta().y() / 240.0);
    const double newZoom = qBound(MIN_ZOOM, m_zoom * step, MAX_ZOOM);
    if (newZoom == m_zoom)
        return;
    m_zoom = newZoom;
    const double side2 = fitSide() * newZoom;
    const double ox = pos.x() - fx * side2;
    const double oy = pos.y() - fy * side2;
    m_pan = QPointF(ox + side2 / 2.0 - width() / 2.0,
                    oy + side2 / 2.0 - height() / 2.0);
    clampPan();
    update();
}

void PreviewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        setFocus(Qt::MouseFocusReason);
        m_dragging = true;
        m_dragFrom = event->position();
        setCursor(Qt::ClosedHandCursor);
    }
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_pan += event->position() - m_dragFrom;
        m_dragFrom = event->position();
        clampPan();
        update();
    }
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = false;
        unsetCursor();
    }
}

void PreviewWidget::mouseDoubleClickEvent(QMouseEvent *)
{
    fitView();
}

void PreviewWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F)
        fitView();
    else
        QWidget::keyPressEvent(event);
}
