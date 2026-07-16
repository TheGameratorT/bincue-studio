// Dialog construction: the sidebar groups and their bindings to LabelConfig.
// State handling (sync, presets, cover order, title editor, export) lives in
// labeldialog_state.cpp.

#include "labeldialog.h"

#include "colorbutton.h"
#include "covers.h"
#include "discspec.h"
#include "previewwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QImageReader>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QModelIndex>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

LabelDialog::LabelDialog(const LabelProject &project,
                         const QString &defaultName, const QString &projectPath,
                         bool projectMode, QWidget *parent)
    : QDialog(parent), m_titleText(project.title), m_tracks(project.tracks),
      m_defaultName(defaultName), m_projectPath(projectPath),
      m_projectMode(projectMode), m_cfg(project.design)
{
    setWindowTitle(tr("CD Label"));
    resize(1180, 720);

    m_trackTitles = shownTrackTitles(m_tracks);
    m_covers = shownCovers(m_tracks);

    RenderInput input;
    input.title = m_titleText;
    input.trackTitles = m_trackTitles;
    input.covers = m_covers;
    m_preview = new PreviewWidget(input, m_cfg, this);

    auto *root = new QHBoxLayout(this);
    // Keep the window's margin everywhere except the right edge, so the
    // sidebar's scrollbar sits flush against the side rather than floating in
    // a reserved gutter.
    QMargins rootMargins = root->contentsMargins();
    rootMargins.setRight(0);
    root->setContentsMargins(rootMargins);
    // The left content panel is always shown: it holds the title override and
    // the per-track name/cover/rename choices, pre-filled from the project.
    root->addWidget(buildContentPanel());
    root->addWidget(m_preview, 1);
    root->addWidget(buildSidebar());

    // A background image named in the design (e.g. a saved project) is loaded
    // before the controls sync so its rows enable correctly.
    loadBgFromConfig();
    syncControlsFromConfig();
}

void LabelDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    // Warn about the initial project's missing fonts on first show, deferred so
    // the editor is painted behind the message box. Projects opened later warn
    // from openProject() instead.
    if (!m_fontWarningShown) {
        m_fontWarningShown = true;
        QTimer::singleShot(0, this, [this] { warnMissingFonts(m_cfg); });
    }
}

void LabelDialog::touch()
{
    if (m_loading)
        return;
    m_preview->setConfig(m_cfg);
    updatePresetLabel();
    updateDiscPresetLabel();
}

// ---- Small helpers ---------------------------------------------------------------

QFormLayout *LabelDialog::makeForm(QWidget *parent)
{
    // Tuned for the narrow, fixed-width sidebar: fields shrink to the
    // available width and an overlong row drops its field onto its own line.
    auto *form = new QFormLayout(parent);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setContentsMargins(6, 6, 6, 6);
    form->setHorizontalSpacing(6);
    return form;
}

QSlider *LabelDialog::makeSlider(int min, int max, const QString &tooltip)
{
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    if (!tooltip.isEmpty())
        slider->setToolTip(tooltip);
    return slider;
}

QWidget *LabelDialog::spinRow(QSlider *slider)
{
    // Pair the slider with a spinbox showing (and editing) its live value.
    auto *spin = new QSpinBox;
    spin->setRange(slider->minimum(), slider->maximum());
    spin->setValue(slider->value());
    spin->setKeyboardTracking(false);
    spin->setFixedWidth(58);
    connect(slider, &QSlider::valueChanged, spin, &QSpinBox::setValue);
    connect(spin, qOverload<int>(&QSpinBox::valueChanged), slider,
            &QSlider::setValue);
    auto *box = new QWidget;
    auto *lay = new QHBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    lay->addWidget(slider, 1);
    lay->addWidget(spin);
    return box;
}

void LabelDialog::setRowVisible(QFormLayout *form, QWidget *fieldOrSlider,
                                bool visible)
{
    // Sliders live inside their spinRow wrapper, which is the form's actual
    // field widget.
    QWidget *field = fieldOrSlider;
    if (qobject_cast<QSlider *>(fieldOrSlider))
        field = fieldOrSlider->parentWidget();
    form->setRowVisible(field, visible);
}

namespace {

QWidget *wrapRow(std::initializer_list<QWidget *> widgets)
{
    auto *box = new QWidget;
    auto *lay = new QHBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    for (QWidget *w : widgets)
        lay->addWidget(w);
    lay->addStretch();
    return box;
}

} // namespace

// ---- Content panel ----------------------------------------------------------
//
// Always shown. Supplies the label's content: the album title (with a rich,
// per-line-sized override) and a per-track list where each track's name and
// cover can be toggled and the track renamed. Two buttons open/save the whole
// cdlabel project (this content plus the design) as a file.

QWidget *LabelDialog::buildContentPanel()
{
    auto *panel = new QWidget;
    panel->setFixedWidth(320);
    auto *col = new QVBoxLayout(panel);
    col->setContentsMargins(6, 6, 6, 6);

    // Project open / save. In project mode the content is bound to the BinCue
    // Studio project, so opening a different cdlabel project is not offered.
    auto *projectRow = new QHBoxLayout;
    if (!m_projectMode) {
        auto *openBtn = new QPushButton(tr("Open…"));
        openBtn->setToolTip(tr("Open a cdlabel project (.cdlabel.json)"));
        connect(openBtn, &QPushButton::clicked, this, &LabelDialog::openProject);
        projectRow->addWidget(openBtn);
    }
    auto *saveProjectBtn = new QPushButton(tr("Save Project…"));
    saveProjectBtn->setToolTip(
        tr("Save the title, per-track choices and design as a cdlabel project"));
    connect(saveProjectBtn, &QPushButton::clicked, this,
            &LabelDialog::saveProject);
    projectRow->addWidget(saveProjectBtn);
    col->addLayout(projectRow);

    // Title — a rich override, pre-filled with the album title.
    auto *titleBox = new QGroupBox(tr("Title"));
    auto *titleLay = new QVBoxLayout(titleBox);
    m_titleOverride = new QTextEdit;
    m_titleOverride->setFixedHeight(78);
    m_titleOverride->setAcceptRichText(false);
    m_titleOverride->setPlaceholderText(
        tr("Album title — type your own, one line each; select a line and use "
           "“Line size” to scale it."));
    m_titleOverride->setToolTip(
        tr("The title printed on the label, pre-filled with the album title. "
           "Add line breaks for a multi-line title; click into a line (or "
           "select several) and change the “Line size” multiplier to size it."));
    connect(m_titleOverride, &QTextEdit::textChanged, this,
            &LabelDialog::titleTextChanged);
    connect(m_titleOverride, &QTextEdit::cursorPositionChanged, this,
            &LabelDialog::syncTitleLineSize);
    connect(m_titleOverride, &QTextEdit::selectionChanged, this,
            &LabelDialog::syncTitleLineSize);
    titleLay->addWidget(m_titleOverride);

    auto *lineSizeRow = new QHBoxLayout;
    lineSizeRow->addWidget(new QLabel(tr("Line size:")));
    m_titleLineSize = new QDoubleSpinBox;
    m_titleLineSize->setRange(0.3, 3.0);
    m_titleLineSize->setSingleStep(0.1);
    m_titleLineSize->setValue(1.0);
    m_titleLineSize->setSuffix(QStringLiteral("×"));
    m_titleLineSize->setToolTip(tr(
        "Font size of the selected title line(s), relative to the base size."));
    connect(m_titleLineSize, &QDoubleSpinBox::valueChanged, this,
            &LabelDialog::applyTitleLineSize);
    lineSizeRow->addWidget(m_titleLineSize);
    lineSizeRow->addStretch();
    titleLay->addLayout(lineSizeRow);
    col->addWidget(titleBox);

    // Tracks — per-track name/cover toggles and rename.
    auto *tracksBox = new QGroupBox(tr("Tracks"));
    auto *tracksLay = new QVBoxLayout(tracksBox);
    m_trackTable = new QTableWidget(0, 4);
    m_trackTable->setHorizontalHeaderLabels(
        {tr("Name"), tr("Cover"), tr("Track"), QString()});
    m_trackTable->setToolTip(
        tr("Per track: tick whether its name appears in the listing and "
           "whether its cover joins the artwork, and rename it. Double-click "
           "the last column to set an image for a track added by hand."));
    m_trackTable->verticalHeader()->setVisible(false);
    m_trackTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_trackTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                  | QAbstractItemView::SelectedClicked
                                  | QAbstractItemView::EditKeyPressed);
    QHeaderView *th = m_trackTable->horizontalHeader();
    th->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    th->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    th->setSectionResizeMode(2, QHeaderView::Stretch);
    th->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(m_trackTable, &QTableWidget::itemChanged, this,
            &LabelDialog::trackItemChanged);
    connect(m_trackTable, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int column) {
                if (column == 3)
                    chooseTrackImage(row);
            });
    tracksLay->addWidget(m_trackTable, 1);

    auto *trackBtns = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("Add"));
    addBtn->setToolTip(tr("Add a track row"));
    connect(addBtn, &QPushButton::clicked, this, &LabelDialog::addTrack);
    auto *removeBtn = new QPushButton(tr("Remove"));
    removeBtn->setToolTip(tr("Remove the selected track(s)"));
    connect(removeBtn, &QPushButton::clicked, this,
            &LabelDialog::removeSelectedTracks);
    trackBtns->addWidget(addBtn);
    trackBtns->addWidget(removeBtn);
    tracksLay->addLayout(trackBtns);
    col->addWidget(tracksBox, 1);

    rebuildTrackTable();
    return panel;
}

void LabelDialog::pushContentToPreview()
{
    m_preview->setContent(m_titleText, m_trackTitles, m_covers);
}

void LabelDialog::rebuildDerivedContent()
{
    m_trackTitles = shownTrackTitles(m_tracks);
    m_covers = shownCovers(m_tracks);
    pushContentToPreview();
}

// The set of shown covers changed: the manual cover order (indices into
// m_covers) and the sidebar's cover rows must be rebuilt to match.
void LabelDialog::refreshCoverBookkeeping()
{
    const bool wasEmpty = m_covers.isEmpty();
    m_cfg.coverOrder.clear();
    m_covers = shownCovers(m_tracks);
    const bool any = !m_covers.isEmpty();
    if (!any)
        m_cfg.coversEnabled = false;
    else if (wasEmpty && !m_cfg.coversEnabled)
        m_cfg.coversEnabled = true;   // first cover shown: turn the layer on
    m_loading = true;
    if (m_coversCheck) {
        m_coversCheck->setEnabled(any);
        m_coversCheck->setChecked(m_cfg.coversEnabled && any);
    }
    m_loading = false;
    rebuildOrderList();
    updateCoverRows();
    m_preview->setConfig(m_cfg);   // coversEnabled / coverOrder may have changed
    rebuildDerivedContent();
}

void LabelDialog::rebuildTrackTable()
{
    m_loading = true;
    m_trackTable->setRowCount(int(m_tracks.size()));
    for (int row = 0; row < m_tracks.size(); ++row) {
        const LabelTrack &t = m_tracks[row];

        auto *nameCheck = new QTableWidgetItem;
        nameCheck->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        nameCheck->setCheckState(t.showName ? Qt::Checked : Qt::Unchecked);
        nameCheck->setTextAlignment(Qt::AlignCenter);
        m_trackTable->setItem(row, 0, nameCheck);

        auto *coverCheck = new QTableWidgetItem;
        coverCheck->setTextAlignment(Qt::AlignCenter);
        if (t.cover.isNull()) {
            // No art to offer: a disabled, unticked box.
            coverCheck->setFlags(Qt::ItemIsUserCheckable);
            coverCheck->setCheckState(Qt::Unchecked);
            coverCheck->setToolTip(tr("This track has no cover art."));
        } else {
            coverCheck->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            coverCheck->setCheckState(t.showCover ? Qt::Checked : Qt::Unchecked);
        }
        m_trackTable->setItem(row, 1, coverCheck);

        auto *nameItem = new QTableWidgetItem(t.displayName);
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                           | Qt::ItemIsEditable);
        m_trackTable->setItem(row, 2, nameItem);

        auto *thumb = new QTableWidgetItem;
        thumb->setFlags(Qt::ItemIsEnabled);
        thumb->setToolTip(tr("Double-click to set a cover image."));
        if (!t.cover.isNull())
            thumb->setIcon(QIcon(QPixmap::fromImage(t.cover.scaled(
                28, 28, Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation))));
        m_trackTable->setItem(row, 3, thumb);
    }
    m_trackTable->resizeRowsToContents();
    m_loading = false;
}

void LabelDialog::trackItemChanged(QTableWidgetItem *item)
{
    if (m_loading)
        return;
    const int row = item->row();
    if (row < 0 || row >= m_tracks.size())
        return;
    LabelTrack &t = m_tracks[row];
    switch (item->column()) {
    case 0:
        t.showName = (item->checkState() == Qt::Checked);
        rebuildDerivedContent();
        break;
    case 1:
        t.showCover = (item->checkState() == Qt::Checked);
        refreshCoverBookkeeping();
        break;
    case 2: {
        const QString name = item->text().trimmed();
        t.displayName = name.isEmpty() ? t.name : name;
        rebuildDerivedContent();
        break;
    }
    default:
        break;
    }
}

void LabelDialog::addTrack()
{
    LabelTrack t;
    t.name = tr("Track %1").arg(m_tracks.size() + 1);
    t.displayName = t.name;
    m_tracks.append(t);
    rebuildTrackTable();
    rebuildDerivedContent();
}

void LabelDialog::removeSelectedTracks()
{
    QList<int> rows;
    const QModelIndexList picked =
        m_trackTable->selectionModel()->selectedRows();
    for (const QModelIndex &idx : picked)
        rows.append(idx.row());
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        if (row >= 0 && row < m_tracks.size())
            m_tracks.removeAt(row);
    rebuildTrackTable();
    refreshCoverBookkeeping();
}

void LabelDialog::chooseTrackImage(int row)
{
    if (row < 0 || row >= m_tracks.size())
        return;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    QStringList exts;
    for (const QByteArray &f : formats)
        exts << QStringLiteral("*.") + QString::fromLatin1(f);
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Choose cover image"), QString(),
        tr("Images (%1)").arg(exts.join(QLatin1Char(' '))));
    if (file.isEmpty())
        return;
    QImage img(file);
    if (img.isNull()) {
        QMessageBox::critical(this, tr("Could not load image"),
                              tr("Not a readable image:\n%1").arg(file));
        return;
    }
    m_tracks[row].cover = img.convertToFormat(QImage::Format_ARGB32);
    m_tracks[row].showCover = true;
    rebuildTrackTable();
    refreshCoverBookkeeping();
}

// ---- Sidebar --------------------------------------------------------------------

QWidget *LabelDialog::buildSidebar()
{
    auto *panel = new QWidget;
    panel->setFixedWidth(350);
    auto *outer = new QVBoxLayout(panel);
    outer->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget;
    auto *col = new QVBoxLayout(body);
    col->setContentsMargins(4, 4, 4, 4);

    col->addWidget(buildPresetGroup());
    col->addWidget(buildDiscGroup());
    col->addWidget(buildTitleGroup());
    col->addWidget(buildTracksGroup());
    col->addWidget(buildPlateGroup());
    col->addWidget(buildPanelGroup());
    col->addWidget(buildCoversGroup());
    col->addWidget(buildBgImageGroup());
    col->addWidget(buildBackdropGroup());
    col->addWidget(buildBandGroup());
    col->addWidget(buildWaveformGroup());
    col->addWidget(buildHubGroup());
    col->addWidget(buildHubRingGroup());
    col->addWidget(buildEdgeGroup());
    col->addStretch();

    // Non-editable combos derive their *minimum* width from their widest item
    // when minimumContentsLength is 0 (QFontComboBox picks the widest installed
    // family name). In the fixed-width sidebar that pushes content past the
    // scroll viewport, so it spilled under the vertical scrollbar. Cap every
    // combo's minimum to a few characters and let the form stretch it to fill
    // the available width instead.
    for (QComboBox *combo : body->findChildren<QComboBox *>()) {
        combo->setSizeAdjustPolicy(
            QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
    }

    scroll->setWidget(body);
    outer->addWidget(scroll, 1);

    auto *buttonRow = new QHBoxLayout;
    // The panel now runs flush to the window's right edge (the scrollbar wants
    // no gutter), so give the button row back the window's normal right margin
    // to keep Save off the very edge.
    buttonRow->setContentsMargins(
        0, 0, style()->pixelMetric(QStyle::PM_LayoutRightMargin), 0);
    auto *closeBtn = new QPushButton(tr("Close"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    auto *exportBtn = new QPushButton(tr("Export Label…"));
    exportBtn->setToolTip(tr("Render the label to a PNG/JPEG image for print"));
    exportBtn->setDefault(true);
    connect(exportBtn, &QPushButton::clicked, this, &LabelDialog::saveLabel);
    buttonRow->addWidget(closeBtn);
    buttonRow->addWidget(exportBtn);
    outer->addLayout(buttonRow);
    return panel;
}

QGroupBox *LabelDialog::buildPresetGroup()
{
    auto *box = new QGroupBox(tr("Preset"));
    auto *lay = new QVBoxLayout(box);
    m_presetCombo = new QComboBox;
    for (const auto &preset : builtinPresets())
        m_presetCombo->addItem(preset.first);
    m_presetCombo->addItem(tr("Custom"));
    connect(m_presetCombo, &QComboBox::activated, this,
            &LabelDialog::onPresetSelected);
    lay->addWidget(m_presetCombo);

    auto *row = new QHBoxLayout;
    auto *importBtn = new QPushButton(tr("Import…"));
    importBtn->setToolTip(tr("Load a preset (.json) from disk"));
    connect(importBtn, &QPushButton::clicked, this, &LabelDialog::importPreset);
    auto *exportBtn = new QPushButton(tr("Export…"));
    exportBtn->setToolTip(tr("Save the current settings as a preset (.json)"));
    connect(exportBtn, &QPushButton::clicked, this, &LabelDialog::exportPreset);
    row->addWidget(importBtn);
    row->addWidget(exportBtn);
    lay->addLayout(row);
    return box;
}

QGroupBox *LabelDialog::buildDiscGroup()
{
    auto *box = new QGroupBox(tr("Disc && printer"));
    box->setToolTip(tr(
        "The physical disc and the printer's printable margins. Pick a "
        "media preset or type the diameters your printer's spec gives."));
    auto *form = makeForm(box);

    m_discPresetCombo = new QComboBox;
    for (const MediaPreset &preset : mediaPresets())
        m_discPresetCombo->addItem(preset.name);
    m_discPresetCombo->addItem(tr("Custom"));
    connect(m_discPresetCombo, &QComboBox::activated, this, [this](int i) {
        const auto &presets = mediaPresets();
        if (i < 0 || i >= presets.size())
            return;   // "Custom" — leave the numbers alone
        m_cfg.discMm = presets[i].discMm;
        m_cfg.holeMm = presets[i].holeMm;
        m_cfg.printableOuterMm = presets[i].outerMm;
        m_cfg.printableInnerMm = presets[i].innerMm;
        syncControlsFromConfig();
    });
    form->addRow(tr("Media:"), m_discPresetCombo);

    const auto mmSpin = [this](double min, double max, double value,
                               const QString &tooltip, auto member) {
        auto *spin = new QDoubleSpinBox;
        spin->setRange(min, max);
        spin->setDecimals(1);
        spin->setSingleStep(1.0);
        spin->setSuffix(tr(" mm"));
        spin->setValue(value);
        spin->setToolTip(tooltip);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, member](double v) {
                    if (m_loading)
                        return;
                    m_cfg.*member = v;
                    touch();
                });
        return spin;
    };
    m_discMm = mmSpin(40, 400, m_cfg.discMm,
                      tr("Physical disc diameter (also the canvas size)."),
                      &LabelConfig::discMm);
    form->addRow(tr("Disc size:"), m_discMm);
    m_holeMm = mmSpin(4, 50, m_cfg.holeMm, tr("Centre hole diameter."),
                      &LabelConfig::holeMm);
    form->addRow(tr("Hole:"), m_holeMm);
    m_outerMm = mmSpin(10, 400, m_cfg.printableOuterMm,
                       tr("Printable outer diameter — artwork stays inside "
                          "this circle."),
                       &LabelConfig::printableOuterMm);
    form->addRow(tr("Print outer:"), m_outerMm);
    m_innerMm = mmSpin(4, 400, m_cfg.printableInnerMm,
                       tr("Printable inner diameter — artwork, text and the "
                          "hub fill all stay outside this circle."),
                       &LabelConfig::printableInnerMm);
    form->addRow(tr("Print inner:"), m_innerMm);
    return box;
}

QGroupBox *LabelDialog::buildTitleGroup()
{
    auto *box = new QGroupBox(tr("Title"));
    auto *form = makeForm(box);
    m_titleForm = form;

    m_titleLayout = new QComboBox;
    m_titleLayout->addItem(tr("Curved along the rim"), TITLE_ARC);
    m_titleLayout->addItem(tr("Straight (top banner)"), TITLE_STRAIGHT);
    connect(m_titleLayout, &QComboBox::activated, this, [this](int) {
        if (m_loading)
            return;
        m_cfg.titleLayout = m_titleLayout->currentData().toString();
        updateTitleRows();
        touch();
    });
    form->addRow(tr("Layout:"), m_titleLayout);

    m_titleBand = makeSlider(10, 55,
                             tr("Height of the top banner band (straight "
                                "layout), as %1 of the disc.").arg("%"));
    connect(m_titleBand, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.titleBand = v / 100.0; touch(); }
    });
    form->addRow(tr("Band height:"), spinRow(m_titleBand));

    m_titleBandEdge = new QCheckBox(tr("Fill the band up to the disc edge"));
    m_titleBandEdge->setToolTip(
        tr("Grow the band's panel up to the disc edge (the bleed edge when a "
           "full bleed is on) so an offset-down title still back-fills the gap "
           "to the rim instead of floating."));
    connect(m_titleBandEdge, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.titleBandEdge = v; touch(); }
    });
    form->addRow(QString(), m_titleBandEdge);

    m_titlePad = makeSlider(0, 200,
                            tr("Breathing room the banner text keeps from "
                               "the band edges. Lower lets it grow bigger."));
    connect(m_titlePad, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.titlePad = v / 100.0; touch(); }
    });
    form->addRow(tr("Padding:"), spinRow(m_titlePad));

    m_titleBold = new QCheckBox(tr("Bold"));
    connect(m_titleBold, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.titleBold = v; touch(); }
    });
    m_titleItalic = new QCheckBox(tr("Italic"));
    connect(m_titleItalic, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.titleItalic = v; touch(); }
    });
    m_titleUnderline = new QCheckBox(tr("Underline"));
    connect(m_titleUnderline, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.titleUnderline = v; touch(); }
    });
    form->addRow(tr("Style:"),
                 wrapRow({m_titleBold, m_titleItalic, m_titleUnderline}));

    m_titleFont = new QFontComboBox;
    connect(m_titleFont, &QFontComboBox::currentFontChanged, this,
            [this](const QFont &f) {
                if (!m_loading) { m_cfg.titleFont = f.family(); touch(); }
            });
    form->addRow(tr("Font:"), m_titleFont);

    m_titleSize = makeSlider(50, 200);
    connect(m_titleSize, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.titleSize = v / 100.0; touch(); }
    });
    form->addRow(tr("Size:"), spinRow(m_titleSize));

    m_titleColor = new ColorButton;
    connect(m_titleColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.titleColor = c; touch(); }
            });
    form->addRow(tr("Colour:"), m_titleColor);

    m_titleOutline = new QCheckBox(tr("Outline"));
    connect(m_titleOutline, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.titleOutline = v; touch(); }
    });
    m_titleOutlineColor = new ColorButton(QStringLiteral("#ffffff"));
    connect(m_titleOutlineColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.titleOutlineColor = c; touch(); }
            });
    form->addRow(tr("Outline:"), wrapRow({m_titleOutline, m_titleOutlineColor}));

    m_titleOutlineWidth = makeSlider(0, 30);
    connect(m_titleOutlineWidth, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.titleOutlineWidth = v / 100.0; touch(); }
    });
    form->addRow(tr("Outline width:"), spinRow(m_titleOutlineWidth));

    m_titleOffsetX = makeSlider(-50, 50);
    connect(m_titleOffsetX, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.titleOffsetX = v / 100.0; touch(); }
    });
    form->addRow(tr("Offset X:"), spinRow(m_titleOffsetX));
    m_titleOffsetY = makeSlider(-50, 50);
    connect(m_titleOffsetY, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.titleOffsetY = v / 100.0; touch(); }
    });
    form->addRow(tr("Offset Y:"), spinRow(m_titleOffsetY));

    // The title *text* (a rich, per-line-sized override) is edited in the left
    // content panel; this group is styling only.
    return box;
}

QGroupBox *LabelDialog::buildTracksGroup()
{
    auto *box = new QGroupBox(tr("Tracks"));
    auto *form = makeForm(box);
    m_tracksForm = form;

    m_trackLayout = new QComboBox;
    m_trackLayout->addItem(tr("Curved rings"), TRACKS_ARC);
    m_trackLayout->addItem(tr("Two columns"), TRACKS_COLUMNS);
    m_trackLayout->addItem(tr("Table (bottom band)"), TRACKS_TABLE);
    connect(m_trackLayout, &QComboBox::activated, this, [this](int) {
        if (m_loading)
            return;
        m_cfg.trackLayout = m_trackLayout->currentData().toString();
        updateTrackRows();
        touch();
    });
    form->addRow(tr("Layout:"), m_trackLayout);

    m_trackBand = makeSlider(10, 55,
                             tr("Height of the bottom band the table fills."));
    connect(m_trackBand, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.trackBand = v / 100.0; touch(); }
    });
    form->addRow(tr("Band height:"), spinRow(m_trackBand));

    m_trackPad = makeSlider(0, 200,
                            tr("Breathing room the table keeps from the band "
                               "edges and the rim. Lower = bigger text."));
    connect(m_trackPad, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.trackPad = v / 100.0; touch(); }
    });
    form->addRow(tr("Padding:"), spinRow(m_trackPad));

    m_trackNumbers = new QCheckBox(tr("Numbering"));
    connect(m_trackNumbers, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.trackNumbers = v; touch(); }
    });
    m_trackUnderline = new QCheckBox(tr("Underline"));
    connect(m_trackUnderline, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.trackUnderline = v; touch(); }
    });
    form->addRow(tr("Show:"), wrapRow({m_trackNumbers, m_trackUnderline}));

    m_trackBold = new QCheckBox(tr("Bold"));
    connect(m_trackBold, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.trackBold = v; touch(); }
    });
    m_trackItalic = new QCheckBox(tr("Italic"));
    connect(m_trackItalic, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.trackItalic = v; touch(); }
    });
    form->addRow(tr("Style:"), wrapRow({m_trackBold, m_trackItalic}));

    m_trackFont = new QFontComboBox;
    connect(m_trackFont, &QFontComboBox::currentFontChanged, this,
            [this](const QFont &f) {
                if (!m_loading) { m_cfg.trackFont = f.family(); touch(); }
            });
    form->addRow(tr("Font:"), m_trackFont);

    m_trackSize = makeSlider(50, 200);
    connect(m_trackSize, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.trackSize = v / 100.0; touch(); }
    });
    form->addRow(tr("Size:"), spinRow(m_trackSize));

    m_trackOffset = makeSlider(-50, 50,
                               tr("Move the curved track rings toward the hub "
                                  "(right) or the rim (left)."));
    connect(m_trackOffset, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.trackOffset = v / 100.0; touch(); }
    });
    form->addRow(tr("Distance:"), spinRow(m_trackOffset));

    m_trackSpacing = makeSlider(50, 200,
                                tr("Spread the column rows apart or pack "
                                   "them tighter."));
    connect(m_trackSpacing, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.trackSpacing = v / 100.0; touch(); }
    });
    form->addRow(tr("Line spacing:"), spinRow(m_trackSpacing));

    m_trackColor = new ColorButton;
    connect(m_trackColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.trackColor = c; touch(); }
            });
    form->addRow(tr("Colour:"), m_trackColor);

    m_trackOutline = new QCheckBox(tr("Outline"));
    connect(m_trackOutline, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.trackOutline = v; touch(); }
    });
    m_trackOutlineColor = new ColorButton(QStringLiteral("#ffffff"));
    connect(m_trackOutlineColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.trackOutlineColor = c; touch(); }
            });
    form->addRow(tr("Outline:"), wrapRow({m_trackOutline, m_trackOutlineColor}));

    m_trackOutlineWidth = makeSlider(0, 30);
    connect(m_trackOutlineWidth, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.trackOutlineWidth = v / 100.0; touch(); }
    });
    form->addRow(tr("Outline width:"), spinRow(m_trackOutlineWidth));
    return box;
}

QGroupBox *LabelDialog::buildPlateGroup()
{
    auto *box = new QGroupBox(tr("Text plates"));
    auto *form = makeForm(box);

    m_plateCheck = new QCheckBox(tr("Pill behind each text run"));
    m_plateCheck->setToolTip(
        tr("Paint a rounded pill behind every text run — straight rows get "
           "rounded rectangles, curved text an arc-shaped pill — so the text "
           "stays legible over busy cover art."));
    connect(m_plateCheck, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.plateEnabled = v; touch(); }
    });
    form->addRow(m_plateCheck);

    m_plateColor = new ColorButton(QStringLiteral("#000000"));
    connect(m_plateColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.plateColor = c; touch(); }
            });
    form->addRow(tr("Colour:"), m_plateColor);

    m_plateAlpha = makeSlider(0, 255);
    connect(m_plateAlpha, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.plateAlpha = v; touch(); }
    });
    form->addRow(tr("Opacity:"), spinRow(m_plateAlpha));

    m_plateRadius = makeSlider(0, 50,
                               tr("0%% = square corners, 50%% = pill ends."));
    connect(m_plateRadius, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.plateRadius = v / 100.0; touch(); }
    });
    form->addRow(tr("Roundness:"), spinRow(m_plateRadius));

    m_platePad = makeSlider(0, 120,
                            tr("How far the pill extends past the text."));
    connect(m_platePad, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.platePad = v / 100.0; touch(); }
    });
    form->addRow(tr("Padding:"), spinRow(m_platePad));

    m_plateOutlineCheck = new QCheckBox(tr("Hairline outline"));
    connect(m_plateOutlineCheck, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.plateOutline = v; touch(); }
    });
    form->addRow(m_plateOutlineCheck);

    m_plateOutlineColor = new ColorButton(QStringLiteral("#ffffff"));
    connect(m_plateOutlineColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.plateOutlineColor = c; touch(); }
            });
    form->addRow(tr("Outline:"), m_plateOutlineColor);
    return box;
}

QGroupBox *LabelDialog::buildPanelGroup()
{
    auto *box = new QGroupBox(tr("Text panels"));
    box->setToolTip(
        tr("Treat the whole area behind the title and/or the track listing — "
           "a band for straight layouts, a ring for curved ones — so the text "
           "reads while the covers still show elsewhere."));
    auto *form = makeForm(box);
    m_panelForm = form;

    m_panelTitle = new QCheckBox(tr("Behind title"));
    connect(m_panelTitle, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.panelTitle = v; touch(); }
    });
    m_panelTracks = new QCheckBox(tr("Behind tracks"));
    connect(m_panelTracks, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.panelTracks = v; touch(); }
    });
    form->addRow(tr("Apply:"), wrapRow({m_panelTitle, m_panelTracks}));

    m_panelMode = new QComboBox;
    m_panelMode->addItem(tr("Blur the covers"), QStringLiteral("blur"));
    m_panelMode->addItem(tr("Solid fill"), QStringLiteral("solid"));
    connect(m_panelMode, &QComboBox::activated, this, [this](int) {
        if (m_loading)
            return;
        m_cfg.panelMode = m_panelMode->currentData().toString();
        updatePanelRows();
        touch();
    });
    form->addRow(tr("Mode:"), m_panelMode);

    m_panelBlur = makeSlider(0, 20,
                             tr("Extra blur of the cover mosaic inside the "
                                "panel, on top of the whole-disc blur."));
    connect(m_panelBlur, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.panelBlur = v; touch(); }
    });
    form->addRow(tr("Blur:"), spinRow(m_panelBlur));

    m_panelColor = new ColorButton(QStringLiteral("#0a0d16"));
    connect(m_panelColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.panelColor = c; touch(); }
            });
    form->addRow(tr("Fill colour:"), m_panelColor);

    m_panelTint = new ColorButton(QStringLiteral("#000000"));
    connect(m_panelTint, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.panelTint = c; touch(); }
            });
    form->addRow(tr("Tint:"), m_panelTint);

    m_panelTintStrength = makeSlider(0, 255);
    connect(m_panelTintStrength, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.panelTintStrength = v; touch(); }
    });
    form->addRow(tr("Tint amount:"), spinRow(m_panelTintStrength));

    m_panelFade = makeSlider(0, 255,
                             tr("Wash of the cover fade colour over just the "
                                "panel, sinking the covers further back."));
    connect(m_panelFade, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.panelFade = v; touch(); }
    });
    form->addRow(tr("Fade:"), spinRow(m_panelFade));
    return box;
}

QGroupBox *LabelDialog::buildCoversGroup()
{
    auto *box = new QGroupBox(tr("Cover art"));
    auto *form = makeForm(box);
    m_coversForm = form;
    const int n = int(m_covers.size());

    m_coversCheck = new QCheckBox(
        n ? tr("Use cover art (%n found)", nullptr, n)
          : tr("Use cover art (none found)"));
    m_coversCheck->setEnabled(n > 0);
    connect(m_coversCheck, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.coversEnabled = v; touch(); }
    });
    form->addRow(m_coversCheck);

    m_coverBg = new QComboBox;
    m_coverBg->addItem(tr("None"), COVER_BG_NONE);
    m_coverBg->addItem(tr("Grid mosaic"), COVER_BG_GRID);
    m_coverBg->addItem(tr("Spiral"), COVER_BG_SPIRAL);
    m_coverBg->setToolTip(
        tr("Background mosaic: repeated covers behind everything, washed "
           "back by the fade/colour/blur/tint below."));
    connect(m_coverBg, &QComboBox::activated, this, [this](int) {
        if (m_loading)
            return;
        m_cfg.coverBg = m_coverBg->currentData().toString();
        updateCoverRows();
        touch();
    });
    form->addRow(tr("Background:"), m_coverBg);

    m_coverFade = makeSlider(0, 255,
                             tr("Wash over the mosaic; higher = fainter."));
    connect(m_coverFade, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.coverFade = v; touch(); }
    });
    form->addRow(tr("Fade:"), spinRow(m_coverFade));

    m_coverFadeColor = new ColorButton(QStringLiteral("#ffffff"));
    connect(m_coverFadeColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.coverFadeColor = c; touch(); }
            });
    form->addRow(tr("Fade colour:"), m_coverFadeColor);

    m_coverDesat = makeSlider(0, 100,
                              tr("0 = greyscale, 100 = full colour."));
    connect(m_coverDesat, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.coverDesat = v / 100.0; touch(); }
    });
    form->addRow(tr("Colour:"), spinRow(m_coverDesat));

    m_coverScale = makeSlider(20, 120,
                              tr("Tile size as a fraction of the disc radius."));
    connect(m_coverScale, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.coverScale = v / 100.0; touch(); }
    });
    form->addRow(tr("Size:"), spinRow(m_coverScale));

    m_coverDensity = makeSlider(0, 100,
                                tr("How densely the spiral winds."));
    connect(m_coverDensity, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) {
            m_cfg.coverOverlap = 0.8 - qBound(0, v, 100) / 100.0 * 0.5;
            touch();
        }
    });
    form->addRow(tr("Density:"), spinRow(m_coverDensity));

    m_coverJitter = makeSlider(0, 45,
                               tr("Max random tilt of each spiral tile."));
    connect(m_coverJitter, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.coverJitter = v; touch(); }
    });
    form->addRow(tr("Rotation:"), spinRow(m_coverJitter));

    m_coverBlur = makeSlider(0, 20);
    connect(m_coverBlur, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.coverBlur = v; touch(); }
    });
    form->addRow(tr("Blur:"), spinRow(m_coverBlur));

    m_coverTint = new ColorButton(QStringLiteral("#000000"));
    connect(m_coverTint, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.coverTint = c; touch(); }
            });
    form->addRow(tr("Tint:"), m_coverTint);

    m_coverTintStrength = makeSlider(0, 255);
    connect(m_coverTintStrength, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.coverTintStrength = v; touch(); }
    });
    form->addRow(tr("Tint amount:"), spinRow(m_coverTintStrength));

    m_coverFeature = new QComboBox;
    m_coverFeature->addItem(tr("None"), COVER_FEAT_NONE);
    m_coverFeature->addItem(tr("Ring around the rim"), COVER_FEAT_RING);
    m_coverFeature->addItem(tr("Scattered through the middle"),
                            COVER_FEAT_SCATTER);
    m_coverFeature->setToolTip(
        tr("Feature covers: each distinct cover shown once, crisp and "
           "full-colour, on top of the background layers."));
    connect(m_coverFeature, &QComboBox::activated, this, [this](int) {
        if (m_loading)
            return;
        m_cfg.coverFeature = m_coverFeature->currentData().toString();
        updateCoverRows();
        touch();
    });
    form->addRow(tr("Feature:"), m_coverFeature);

    m_ringDepth = makeSlider(10, 70,
                             tr("Depth of the outer band the ring of covers "
                                "lives in."));
    connect(m_ringDepth, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.ringDepth = v / 100.0; touch(); }
    });
    form->addRow(tr("Ring depth:"), spinRow(m_ringDepth));

    m_ringTitleGap = makeSlider(0, 45,
                                tr("Extra room to leave around the title "
                                   "before the covers start."));
    connect(m_ringTitleGap, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.ringTitleGap = v; touch(); }
    });
    form->addRow(tr("Title gap:"), spinRow(m_ringTitleGap));

    m_featureScale = makeSlider(40, 160,
                                tr("Cover size relative to its natural slot: "
                                   "100%% just fits; above grows/overlaps."));
    connect(m_featureScale, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.featureScale = v / 100.0; touch(); }
    });
    form->addRow(tr("Cover size:"), spinRow(m_featureScale));

    m_featureTilt = makeSlider(0, 15,
                               tr("Max random tilt of each feature cover."));
    connect(m_featureTilt, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.featureTilt = v; touch(); }
    });
    form->addRow(tr("Tilt:"), spinRow(m_featureTilt));

    m_scatterAvoid = new QCheckBox(tr("Keep clear of text bands"));
    m_scatterAvoid->setToolTip(
        tr("Keep the scattered covers out of the straight title band and the "
           "table's track band."));
    connect(m_scatterAvoid, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.scatterAvoidText = v; touch(); }
    });
    form->addRow(m_scatterAvoid);

    m_coverFrame = new QCheckBox(tr("Frame && shadow"));
    m_coverFrame->setToolTip(
        tr("Give each cover a white photo border and a soft drop shadow."));
    connect(m_coverFrame, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.coverFrame = v; touch(); }
    });
    form->addRow(m_coverFrame);

    m_orderMode = new QComboBox;
    m_orderMode->addItem(tr("Spread out (avoid repeats)"), false);
    m_orderMode->addItem(tr("In my order"), true);
    m_orderMode->setToolTip(
        tr("How the covers fill their slots: an automatic spread that keeps "
           "each cover's repeats far apart, or strictly in the list order "
           "below (reading order for the grid and the scatter, along the arm "
           "for the spiral, clockwise for the ring)."));
    connect(m_orderMode, &QComboBox::activated, this, [this](int) {
        if (m_loading)
            return;
        m_cfg.coverSequence = m_orderMode->currentData().toBool();
        touch();
    });
    form->addRow(tr("Placement:"), m_orderMode);

    m_orderList = new QListWidget;
    m_orderList->setDragDropMode(QAbstractItemView::InternalMove);
    m_orderList->setDefaultDropAction(Qt::MoveAction);
    m_orderList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_orderList->setIconSize(QSize(28, 28));
    m_orderList->setFixedHeight(132);
    m_orderList->setToolTip(
        tr("Drag to reorder the covers; untick one to leave it off the "
           "label."));
    connect(m_orderList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *) { orderListChanged(); });
    connect(m_orderList->model(), &QAbstractItemModel::rowsMoved, this,
            [this]() { orderListChanged(); });
    form->addRow(tr("Order:"), m_orderList);

    box->setEnabled(true);
    return box;
}

QGroupBox *LabelDialog::buildBgImageGroup()
{
    auto *box = new QGroupBox(tr("Background image"));
    auto *form = makeForm(box);

    m_bgImageCheck = new QCheckBox(tr("Use image"));
    m_bgImageCheck->setEnabled(false);   // until an image is chosen
    connect(m_bgImageCheck, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.bgImageEnabled = v; touch(); }
    });
    auto *chooseBtn = new QPushButton(tr("Choose image…"));
    chooseBtn->setToolTip(tr("Load a picture from disk to fill the disc"));
    connect(chooseBtn, &QPushButton::clicked, this,
            &LabelDialog::chooseBgImage);
    form->addRow(wrapRow({m_bgImageCheck, chooseBtn}));

    m_bgImageLabel = new QLabel(tr("No image chosen"));
    m_bgImageLabel->setWordWrap(true);
    m_bgImageLabel->setStyleSheet(QStringLiteral("color: #888;"));
    form->addRow(m_bgImageLabel);

    m_bgFit = new QComboBox;
    m_bgFit->addItem(tr("Cover (fill disc)"), QStringLiteral("cover"));
    m_bgFit->addItem(tr("Contain (fit inside)"), QStringLiteral("contain"));
    m_bgFit->addItem(tr("Stretch"), QStringLiteral("stretch"));
    connect(m_bgFit, &QComboBox::activated, this, [this](int) {
        if (!m_loading) {
            m_cfg.bgImageFit = m_bgFit->currentData().toString();
            touch();
        }
    });
    form->addRow(tr("Fit:"), m_bgFit);

    m_bgFade = makeSlider(0, 255);
    connect(m_bgFade, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.bgImageFade = v; touch(); }
    });
    form->addRow(tr("Fade:"), spinRow(m_bgFade));

    m_bgDesat = makeSlider(0, 100);
    connect(m_bgDesat, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.bgImageDesat = v / 100.0; touch(); }
    });
    form->addRow(tr("Colour:"), spinRow(m_bgDesat));

    m_bgBlur = makeSlider(0, 20);
    connect(m_bgBlur, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.bgImageBlur = v; touch(); }
    });
    form->addRow(tr("Blur:"), spinRow(m_bgBlur));
    return box;
}

QGroupBox *LabelDialog::buildBackdropGroup()
{
    auto *box = new QGroupBox(tr("Backdrop"));
    auto *form = makeForm(box);

    m_backdropCheck = new QCheckBox(tr("Fill the disc"));
    connect(m_backdropCheck, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.backdropEnabled = v; touch(); }
    });
    form->addRow(m_backdropCheck);

    m_backdropColor = new ColorButton(QStringLiteral("#ffffff"));
    connect(m_backdropColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.backdropColor = c; touch(); }
            });
    form->addRow(tr("Colour:"), m_backdropColor);

    m_backdropGradient = new QCheckBox(tr("Gradient to"));
    connect(m_backdropGradient, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.backdropGradient = v; touch(); }
    });
    m_backdropColor2 = new ColorButton(QStringLiteral("#cccccc"));
    connect(m_backdropColor2, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.backdropColor2 = c; touch(); }
            });
    m_backdropGradShape = new QComboBox;
    m_backdropGradShape->addItem(tr("Linear"));
    m_backdropGradShape->addItem(tr("Radial"));
    m_backdropGradShape->setToolTip(
        tr("Linear runs top to bottom; radial runs centre to rim for a "
           "spotlight or vignette."));
    connect(m_backdropGradShape, &QComboBox::activated, this, [this](int i) {
        if (!m_loading) { m_cfg.backdropGradientRadial = i == 1; touch(); }
    });
    form->addRow(QString(), wrapRow({m_backdropGradient, m_backdropColor2,
                                     m_backdropGradShape}));
    return box;
}

QGroupBox *LabelDialog::buildBandGroup()
{
    auto *box = new QGroupBox(tr("Overlay band"));
    auto *form = makeForm(box);

    m_bandCheck = new QCheckBox(tr("Show band"));
    connect(m_bandCheck, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.bandEnabled = v; touch(); }
    });
    form->addRow(m_bandCheck);

    m_bandStyle = new QComboBox;
    m_bandStyle->addItem(tr("Ring (follows the disc)"), QStringLiteral("ring"));
    m_bandStyle->addItem(tr("Strip (straight across)"), QStringLiteral("strip"));
    connect(m_bandStyle, &QComboBox::activated, this, [this](int) {
        if (!m_loading) {
            m_cfg.bandStyle = m_bandStyle->currentData().toString();
            touch();
        }
    });
    form->addRow(tr("Shape:"), m_bandStyle);

    m_bandColor = new ColorButton(QStringLiteral("#ffffff"));
    connect(m_bandColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.bandColor = c; touch(); }
            });
    form->addRow(tr("Colour:"), m_bandColor);

    m_bandAlpha = makeSlider(0, 255);
    connect(m_bandAlpha, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.bandAlpha = v; touch(); }
    });
    form->addRow(tr("Opacity:"), spinRow(m_bandAlpha));

    m_bandHeight = makeSlider(20, 100);
    connect(m_bandHeight, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.bandHeight = v / 100.0; touch(); }
    });
    form->addRow(tr("Height:"), spinRow(m_bandHeight));

    m_bandFeather = makeSlider(0, 50,
                               tr("0%% = hard edges, 50%% = fully tapered."));
    connect(m_bandFeather, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.bandFeather = v / 100.0; touch(); }
    });
    form->addRow(tr("Edge fade:"), spinRow(m_bandFeather));
    return box;
}

QGroupBox *LabelDialog::buildWaveformGroup()
{
    auto *box = new QGroupBox(tr("Waveform"));
    auto *form = makeForm(box);

    m_waveformCheck = new QCheckBox(tr("Show waveform"));
    m_waveformCheck->setToolTip(
        tr("A faint circular waveform behind the track list, seeded by the "
           "track names — stable but unique to this disc."));
    connect(m_waveformCheck, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.waveformEnabled = v; touch(); }
    });
    form->addRow(m_waveformCheck);

    m_waveformColor = new ColorButton(QStringLiteral("#ffffff"));
    connect(m_waveformColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.waveformColor = c; touch(); }
            });
    form->addRow(tr("Colour:"), m_waveformColor);

    m_waveformAlpha = makeSlider(0, 255,
                                 tr("Opacity — keep low for a faint wash."));
    connect(m_waveformAlpha, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.waveformAlpha = v; touch(); }
    });
    form->addRow(tr("Opacity:"), spinRow(m_waveformAlpha));

    m_waveformRadius = makeSlider(30, 95);
    connect(m_waveformRadius, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.waveformRadius = v / 100.0; touch(); }
    });
    form->addRow(tr("Radius:"), spinRow(m_waveformRadius));

    m_waveformAmp = makeSlider(0, 30);
    connect(m_waveformAmp, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.waveformAmplitude = v / 100.0; touch(); }
    });
    form->addRow(tr("Height:"), spinRow(m_waveformAmp));

    m_waveformBars = makeSlider(16, 360);
    connect(m_waveformBars, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.waveformBars = v; touch(); }
    });
    form->addRow(tr("Bars:"), spinRow(m_waveformBars));
    return box;
}

QGroupBox *LabelDialog::buildHubGroup()
{
    auto *box = new QGroupBox(tr("Centre hub"));
    auto *form = makeForm(box);

    m_hubMode = new QComboBox;
    m_hubMode->addItem(tr("Blank (transparent)"), QStringLiteral("blank"));
    m_hubMode->addItem(tr("Fill with colour"), QStringLiteral("fill"));
    m_hubMode->addItem(tr("Keep background"), QStringLiteral("background"));
    m_hubMode->setToolTip(
        tr("How to treat the disc's inner hub (inside the printable inner "
           "ring). Standard printable discs can't take ink there; "
           "hub-printable discs can."));
    connect(m_hubMode, &QComboBox::activated, this, [this](int) {
        if (m_loading)
            return;
        m_cfg.hubMode = m_hubMode->currentData().toString();
        updateHubRows();
        touch();
    });
    form->addRow(tr("Inner area:"), m_hubMode);

    m_hubColor = new ColorButton(QStringLiteral("#ffffff"));
    connect(m_hubColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.hubColor = c; touch(); }
            });
    form->addRow(tr("Fill colour:"), m_hubColor);

    m_hubGradient = new QCheckBox(tr("Gradient to"));
    connect(m_hubGradient, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.hubGradient = v; touch(); }
    });
    m_hubColor2 = new ColorButton(QStringLiteral("#334155"));
    connect(m_hubColor2, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.hubColor2 = c; touch(); }
            });
    m_hubGradShape = new QComboBox;
    m_hubGradShape->addItem(tr("Linear"));
    m_hubGradShape->addItem(tr("Radial"));
    connect(m_hubGradShape, &QComboBox::activated, this, [this](int i) {
        if (!m_loading) { m_cfg.hubGradientRadial = i == 1; touch(); }
    });
    form->addRow(QString(),
                 wrapRow({m_hubGradient, m_hubColor2, m_hubGradShape}));
    return box;
}

QGroupBox *LabelDialog::buildHubRingGroup()
{
    auto *box = new QGroupBox(tr("Centre ring"));
    auto *form = makeForm(box);

    m_hubRingCheck = new QCheckBox(tr("Metallic ring"));
    m_hubRingCheck->setToolTip(
        tr("Draw a chrome/foil ring around the centre hole. It prints into "
           "the hub, so use a hub-printable disc (or the 'Keep background' "
           "hub) to see it fully."));
    connect(m_hubRingCheck, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.hubRingEnabled = v; touch(); }
    });
    form->addRow(m_hubRingCheck);

    m_hubRingColor = new ColorButton(QStringLiteral("#b9bcc4"));
    connect(m_hubRingColor, &ColorButton::colorChanged, this,
            [this](const QString &c) {
                if (!m_loading) { m_cfg.hubRingColor = c; touch(); }
            });
    form->addRow(tr("Metal:"), m_hubRingColor);

    m_hubRingWidth = makeSlider(0, 100);
    connect(m_hubRingWidth, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.hubRingWidth = v / 100.0; touch(); }
    });
    form->addRow(tr("Width:"), spinRow(m_hubRingWidth));

    m_hubRingShine = makeSlider(0, 255,
                                tr("Strength of the metallic sheen."));
    connect(m_hubRingShine, &QSlider::valueChanged, this, [this](int v) {
        if (!m_loading) { m_cfg.hubRingShine = v; touch(); }
    });
    form->addRow(tr("Shine:"), spinRow(m_hubRingShine));
    return box;
}

QGroupBox *LabelDialog::buildEdgeGroup()
{
    auto *box = new QGroupBox(tr("Outer edge"));
    auto *lay = new QVBoxLayout(box);
    m_bleedCheck = new QCheckBox(tr("Bleed background to disc edge"));
    m_bleedCheck->setToolTip(
        tr("Let the background layers run past the printable outer ring all "
           "the way to the disc edge (a full bleed), leaving the printer to "
           "clip to its own printable diameter."));
    connect(m_bleedCheck, &QCheckBox::toggled, this, [this](bool v) {
        if (!m_loading) { m_cfg.bleedEdge = v; touch(); }
    });
    lay->addWidget(m_bleedCheck);
    return box;
}
