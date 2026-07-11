// Live label preview with mouse zoom/pan and an "F to fit" reset.
//
// The label is rasterised once to a high-resolution buffer (via the same
// scale pipeline the file export uses) and that buffer is smoothly scaled to
// the view, so the covers are sampled at high resolution and minified with a
// quality filter — matching the exported PNG. A cheaper draft is shown while
// the config is actively changing, with the crisp full render following once
// edits pause.
#pragma once

#include <QHash>
#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QWidget>

#include "labelconfig.h"
#include "render.h"

class PreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit PreviewWidget(const RenderInput &input, const LabelConfig &cfg,
                           QWidget *parent = nullptr);

    void setConfig(const LabelConfig &cfg);
    void setBgImage(const QImage &img);
    // Replace the label's editable content (standalone mode): title, track
    // listing and cover art all come from the left content panel, not a project.
    void setContent(const QString &title, const QStringList &trackTitles,
                    const QList<QImage> &covers);
    void invalidate();   // drop the cached render (label content changed)
    void fitView();

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    static constexpr double MIN_ZOOM = 1.0;   // 1.0 == whole disc fitted
    static constexpr double MAX_ZOOM = 8.0;
    static constexpr int RENDER_PX = 1800;    // crisp full render
    static constexpr int DRAFT_PX = 900;      // cheap draft while editing

    void buildBase(int px);
    void renderFull();
    QImage display(int px);
    double fitSide() const;
    void clampPan();

    RenderInput m_input;
    LabelConfig m_cfg;
    QImage m_base;
    int m_basePx = 0;
    QHash<int, QImage> m_dispCache;
    double m_zoom = 1.0;
    QPointF m_pan{0.0, 0.0};
    bool m_dragging = false;
    QPointF m_dragFrom;
    QTimer m_fullTimer;
};
