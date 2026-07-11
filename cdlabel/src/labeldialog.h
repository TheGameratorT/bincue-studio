// The label editor dialog: live preview beside a customisation sidebar.
//
// Every group edits LabelConfig directly and repaints the preview; there are
// no modes — the title layout, track layout, cover sub-layers, plates,
// panels and decor all combine freely. A preset combo (built-ins + JSON
// import/export) drops whole configs in wholesale, and a disc group picks
// the media/printer geometry (or fully custom diameters/margins).
#pragma once

#include <QDialog>
#include <QImage>
#include <QList>
#include <QStringList>

#include "labelconfig.h"
#include "labelproject.h"
#include "render.h"

class ColorButton;
class PreviewWidget;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QTableWidgetItem;
class QSlider;
class QTextEdit;
class QWidget;

class LabelDialog : public QDialog {
    Q_OBJECT
public:
    // `project` supplies the content (title + tracks) and design; `projectPath`
    // is where Save Project writes (empty = prompt on first save). `projectMode`
    // means launched from BinCue Studio (the content is bound to that project,
    // so opening a different cdlabel project is not offered).
    LabelDialog(const LabelProject &project,
                const QString &defaultName = QStringLiteral("cd_label"),
                const QString &projectPath = QString(),
                bool projectMode = false, QWidget *parent = nullptr);

    void applyConfig(const LabelConfig &cfg);   // e.g. a preset given on the CLI

private:
    // -- construction -----------------------------------------------------
    QWidget *buildContentPanel();   // title override + per-track list
    QWidget *buildSidebar();
    QGroupBox *buildPresetGroup();
    QGroupBox *buildDiscGroup();
    QGroupBox *buildTitleGroup();
    QGroupBox *buildTracksGroup();
    QGroupBox *buildPlateGroup();
    QGroupBox *buildPanelGroup();
    QGroupBox *buildCoversGroup();
    QGroupBox *buildBgImageGroup();
    QGroupBox *buildBackdropGroup();
    QGroupBox *buildBandGroup();
    QGroupBox *buildWaveformGroup();
    QGroupBox *buildHubGroup();
    QGroupBox *buildHubRingGroup();
    QGroupBox *buildEdgeGroup();

    // -- small helpers ------------------------------------------------------
    static QFormLayout *makeForm(QWidget *parent);
    QSlider *makeSlider(int min, int max, const QString &tooltip = {});
    QWidget *spinRow(QSlider *slider);   // slider + live spinbox, one row
    void setRowVisible(QFormLayout *form, QWidget *fieldOrSlider, bool visible);
    void touch();                        // config edited: repaint + preset label

    // -- config <-> controls ---------------------------------------------------
    void syncControlsFromConfig();
    void updatePresetLabel();
    void updateDiscPresetLabel();
    void updateTitleRows();
    void updateTrackRows();
    void updateCoverRows();
    void updatePanelRows();
    void updateHubRows();

    // -- cover order list ----------------------------------------------------------
    void rebuildOrderList();
    void orderListChanged();

    // -- rich title override editor ---------------------------------------------------
    QString serializeTitleDoc() const;
    void loadTitleDoc(const QString &markup);
    void titleTextChanged();
    void syncTitleLineSize();
    void applyTitleLineSize(double mult);

    // -- presets / files -----------------------------------------------------------------
    void onPresetSelected(int index);
    void importPreset();
    void exportPreset();
    void chooseBgImage();
    void loadBgFromConfig();
    void updateBgImageLabel();
    void saveLabel();

    // -- cdlabel project (content + design) ----------------------------------
    void openProject();
    void saveProject();               // Save to m_projectPath, else prompt
    void saveProjectAs();             // always prompt
    bool writeProjectTo(const QString &path);
    void adoptProject(const LabelProject &project, const QString &path);

    // -- content editing (title override + per-track list) -------------------
    void pushContentToPreview();      // derive titles/covers -> preview + repaint
    void rebuildDerivedContent();     // recompute m_trackTitles/m_covers from m_tracks
    void refreshCoverBookkeeping();   // covers changed: order list + sidebar rows
    void rebuildTrackTable();
    void trackItemChanged(QTableWidgetItem *item);
    void addTrack();
    void removeSelectedTracks();
    void chooseTrackImage(int row);   // assign a cover image to a manual track

    // -- data ----------------------------------------------------------------
    QString m_titleText;              // album title (fallback when no override)
    QList<LabelTrack> m_tracks;       // the content: source of truth
    QStringList m_trackTitles;        // derived: shown display names
    QList<QImage> m_covers;           // derived: distinct shown covers
    QString m_defaultName;
    QString m_projectPath;            // cdlabel project Save target ("" = none)
    bool m_projectMode = false;       // launched embedded from BinCue Studio
    LabelConfig m_cfg;
    QImage m_bgImage;
    bool m_loading = false;   // guards handlers during bulk control updates

    PreviewWidget *m_preview = nullptr;

    // Content panel
    QTableWidget *m_trackTable = nullptr;

    // Preset / disc
    QComboBox *m_presetCombo = nullptr;
    QComboBox *m_discPresetCombo = nullptr;
    QDoubleSpinBox *m_discMm = nullptr;
    QDoubleSpinBox *m_holeMm = nullptr;
    QDoubleSpinBox *m_outerMm = nullptr;
    QDoubleSpinBox *m_innerMm = nullptr;

    // Title
    QFormLayout *m_titleForm = nullptr;
    QComboBox *m_titleLayout = nullptr;
    QSlider *m_titleBand = nullptr;
    QCheckBox *m_titleBandEdge = nullptr;
    QSlider *m_titlePad = nullptr;
    QCheckBox *m_titleBold = nullptr;
    QCheckBox *m_titleItalic = nullptr;
    QCheckBox *m_titleUnderline = nullptr;
    QFontComboBox *m_titleFont = nullptr;
    QSlider *m_titleSize = nullptr;
    ColorButton *m_titleColor = nullptr;
    QCheckBox *m_titleOutline = nullptr;
    ColorButton *m_titleOutlineColor = nullptr;
    QSlider *m_titleOutlineWidth = nullptr;
    QSlider *m_titleOffsetX = nullptr;
    QSlider *m_titleOffsetY = nullptr;
    QTextEdit *m_titleOverride = nullptr;
    QDoubleSpinBox *m_titleLineSize = nullptr;

    // Tracks
    QFormLayout *m_tracksForm = nullptr;
    QComboBox *m_trackLayout = nullptr;
    QSlider *m_trackBand = nullptr;
    QSlider *m_trackPad = nullptr;
    QCheckBox *m_trackNumbers = nullptr;
    QCheckBox *m_trackUnderline = nullptr;
    QCheckBox *m_trackBold = nullptr;
    QCheckBox *m_trackItalic = nullptr;
    QFontComboBox *m_trackFont = nullptr;
    QSlider *m_trackSize = nullptr;
    QSlider *m_trackOffset = nullptr;
    QSlider *m_trackSpacing = nullptr;
    ColorButton *m_trackColor = nullptr;
    QCheckBox *m_trackOutline = nullptr;
    ColorButton *m_trackOutlineColor = nullptr;
    QSlider *m_trackOutlineWidth = nullptr;

    // Plates
    QCheckBox *m_plateCheck = nullptr;
    ColorButton *m_plateColor = nullptr;
    QSlider *m_plateAlpha = nullptr;
    QSlider *m_plateRadius = nullptr;
    QSlider *m_platePad = nullptr;
    QCheckBox *m_plateOutlineCheck = nullptr;
    ColorButton *m_plateOutlineColor = nullptr;

    // Panels
    QFormLayout *m_panelForm = nullptr;
    QCheckBox *m_panelTitle = nullptr;
    QCheckBox *m_panelTracks = nullptr;
    QComboBox *m_panelMode = nullptr;
    QSlider *m_panelBlur = nullptr;
    ColorButton *m_panelColor = nullptr;
    ColorButton *m_panelTint = nullptr;
    QSlider *m_panelTintStrength = nullptr;
    QSlider *m_panelFade = nullptr;

    // Covers
    QFormLayout *m_coversForm = nullptr;
    QCheckBox *m_coversCheck = nullptr;
    QComboBox *m_coverBg = nullptr;
    QSlider *m_coverFade = nullptr;
    ColorButton *m_coverFadeColor = nullptr;
    QSlider *m_coverDesat = nullptr;
    QSlider *m_coverScale = nullptr;
    QSlider *m_coverDensity = nullptr;
    QSlider *m_coverJitter = nullptr;
    QSlider *m_coverBlur = nullptr;
    ColorButton *m_coverTint = nullptr;
    QSlider *m_coverTintStrength = nullptr;
    QComboBox *m_coverFeature = nullptr;
    QSlider *m_ringDepth = nullptr;
    QSlider *m_ringTitleGap = nullptr;
    QSlider *m_featureScale = nullptr;
    QSlider *m_featureTilt = nullptr;
    QCheckBox *m_scatterAvoid = nullptr;
    QCheckBox *m_coverFrame = nullptr;
    QComboBox *m_orderMode = nullptr;
    QListWidget *m_orderList = nullptr;

    // Background image
    QCheckBox *m_bgImageCheck = nullptr;
    QLabel *m_bgImageLabel = nullptr;
    QComboBox *m_bgFit = nullptr;
    QSlider *m_bgFade = nullptr;
    QSlider *m_bgDesat = nullptr;
    QSlider *m_bgBlur = nullptr;

    // Backdrop
    QCheckBox *m_backdropCheck = nullptr;
    ColorButton *m_backdropColor = nullptr;
    QCheckBox *m_backdropGradient = nullptr;
    ColorButton *m_backdropColor2 = nullptr;
    QComboBox *m_backdropGradShape = nullptr;

    // Overlay band
    QCheckBox *m_bandCheck = nullptr;
    QComboBox *m_bandStyle = nullptr;
    ColorButton *m_bandColor = nullptr;
    QSlider *m_bandAlpha = nullptr;
    QSlider *m_bandHeight = nullptr;
    QSlider *m_bandFeather = nullptr;

    // Waveform
    QCheckBox *m_waveformCheck = nullptr;
    ColorButton *m_waveformColor = nullptr;
    QSlider *m_waveformAlpha = nullptr;
    QSlider *m_waveformRadius = nullptr;
    QSlider *m_waveformAmp = nullptr;
    QSlider *m_waveformBars = nullptr;

    // Hub / hub ring / edge
    QComboBox *m_hubMode = nullptr;
    ColorButton *m_hubColor = nullptr;
    QCheckBox *m_hubGradient = nullptr;
    ColorButton *m_hubColor2 = nullptr;
    QComboBox *m_hubGradShape = nullptr;
    QCheckBox *m_hubRingCheck = nullptr;
    ColorButton *m_hubRingColor = nullptr;
    QSlider *m_hubRingWidth = nullptr;
    QSlider *m_hubRingShine = nullptr;
    QCheckBox *m_bleedCheck = nullptr;
};
