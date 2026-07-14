#include "mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QProxyStyle>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#include "burndialog.h"
#include "burnjob.h"
#include "cdtextcompletion.h"
#include "exportworker.h"
#include "playbackengine.h"
#include "programaudio.h"
#include "tagreader.h"
#include "cdtextfields.h"
#include "discdetailsdialog.h"
#include "trackdetailsdialog.h"

#include <hostkit/HostSession.h>

namespace {

// Makes a left-click anywhere on a slider's groove jump the handle straight to
// that spot (Qt's default only page-steps toward the click). The absolute jump
// still fires the usual pressed/moved/released signals, so seeking works the
// same whether you click or drag.
class DirectJumpStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;
    int styleHint(StyleHint hint, const QStyleOption *option,
                  const QWidget *widget,
                  QStyleHintReturn *returnData) const override
    {
        if (hint == QStyle::SH_Slider_AbsoluteSetButtons)
            return Qt::LeftButton;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

enum Column {
    ColPlay,
    ColTitle,
    ColPerformer,
    ColDuration,
    ColBaked,
    ColActions,
    ColumnCount
};

struct DiscSize {
    const char *label;
    int minutes;
};
constexpr DiscSize DISC_SIZES[] = {{"74 min (~650 MB)", 74},
                                   {"80 min (~700 MB)", 80}};
constexpr int MAX_RECENT_FILES = 8;  // how many recent projects to remember

QIcon themedIcon(const char *name, QStyle::StandardPixmap fallback)
{
    const QIcon icon = QIcon::fromTheme(QLatin1String(name));
    return icon.isNull() ? qApp->style()->standardIcon(fallback) : icon;
}

// mm:ss for the player's position/total time labels.
QString formatClock(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(s / 60, 2, 10, QLatin1Char('0'))
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}

bool probeDurationSeconds(const QString &path, double &outSeconds,
                          QString &error)
{
    return programaudio::probeDuration(path, &outSeconds, &error);
}

// The file dialog filter shared by "Add Tracks" and a track's "Re-import".
QString audioFileFilter()
{
    return QCoreApplication::translate(
        "MainWindow",
        "Audio files (*.flac *.wav *.mp3 *.ogg *.opus *.m4a *.aiff);;"
        "All files (*)");
}

// Fill a track's per-track metadata from its source file's tags, exactly as a
// fresh import does: the title falls back to the file name, the rest to
// whatever the tags carry (blank when absent). Arranger, message and the
// playback flags aren't tag-derived and are left untouched by the caller.
void importTagsInto(Track &track)
{
    track.title = QFileInfo(track.sourcePath).completeBaseName();
    if (!tagReaderAvailable())
        return;
    const TrackTags tags = readTrackTags(track.sourcePath);
    if (!tags.title.isEmpty())
        track.title = tags.title;
    track.performer = tags.performer;
    track.songwriter = tags.songwriter;
    track.composer = tags.composer;
    track.isrc = tags.isrc;
}

// The label editor is a sibling executable built by the same CMake project;
// CDLABEL_BIN overrides, PATH is the last resort.
QString cdlabelBinary()
{
    const QString env = qEnvironmentVariable("CDLABEL_BIN");
    if (!env.isEmpty() && QFileInfo::exists(env))
        return env;
    const QString sibling =
        QCoreApplication::applicationDirPath() + QStringLiteral("/cdlabel");
    if (QFileInfo::exists(sibling))
        return sibling;
    return QStandardPaths::findExecutable(QStringLiteral("cdlabel"));
}

} // namespace

MainWindow::MainWindow()
{
    resize(980, 660);

    buildActions();
    buildMenus();
    buildToolBar();
    buildUi();

    m_statsLabel = new QLabel;
    statusBar()->addPermanentWidget(m_statsLabel);

    updateTitle();
    updatePregapWarning();
    updateCapacity();
}

// -- Actions, menus, toolbar -------------------------------------------------

void MainWindow::buildActions()
{
    m_newAct = new QAction(themedIcon("document-new", QStyle::SP_FileIcon),
                           tr("&New Project"), this);
    m_newAct->setShortcut(QKeySequence::New);
    connect(m_newAct, &QAction::triggered, this, &MainWindow::newProject);

    m_openAct = new QAction(themedIcon("document-open", QStyle::SP_DirOpenIcon),
                            tr("&Open Project…"), this);
    m_openAct->setShortcut(QKeySequence::Open);
    connect(m_openAct, &QAction::triggered, this, [this] { openProject(); });

    m_saveAct =
        new QAction(themedIcon("document-save", QStyle::SP_DialogSaveButton),
                    tr("&Save Project"), this);
    m_saveAct->setShortcut(QKeySequence::Save);
    connect(m_saveAct, &QAction::triggered, this, &MainWindow::saveProject);

    m_saveAsAct = new QAction(
        themedIcon("document-save-as", QStyle::SP_DialogSaveButton),
        tr("Save Project &As…"), this);
    m_saveAsAct->setShortcut(QKeySequence::SaveAs);
    connect(m_saveAsAct, &QAction::triggered, this,
            &MainWindow::saveProjectAs);

    m_exportAct = new QAction(QIcon(QStringLiteral(":/icons/export.svg")),
                              tr("&Export BIN + CUE…"), this);
    m_exportAct->setShortcut(QStringLiteral("Ctrl+E"));
    m_exportAct->setToolTip(tr("Build the BIN + CUE disc image"));
    connect(m_exportAct, &QAction::triggered, this,
            &MainWindow::exportProject);

    m_burnAct = new QAction(QIcon(QStringLiteral(":/icons/burn.svg")),
                            tr("&Burn to Disc…"), this);
    m_burnAct->setShortcut(QStringLiteral("Ctrl+B"));
    m_burnAct->setToolTip(
        tr("Build the image and burn it to a CD here or over SSH"));
    connect(m_burnAct, &QAction::triggered, this, &MainWindow::burnProject);

    m_addAct = new QAction(themedIcon("list-add", QStyle::SP_FileDialogStart),
                           tr("&Add Tracks…"), this);
    m_addAct->setShortcut(QStringLiteral("Ctrl+T"));
    connect(m_addAct, &QAction::triggered, this, &MainWindow::addTracks);

    m_removeAct = new QAction(themedIcon("list-remove", QStyle::SP_TrashIcon),
                              tr("&Remove Selected"), this);
    m_removeAct->setShortcut(QKeySequence::Delete);
    connect(m_removeAct, &QAction::triggered, this,
            &MainWindow::removeSelected);

    m_upAct = new QAction(themedIcon("go-up", QStyle::SP_ArrowUp),
                          tr("Move &Up"), this);
    m_upAct->setShortcut(QStringLiteral("Ctrl+Up"));
    connect(m_upAct, &QAction::triggered, this, [this] { moveSelected(-1); });

    m_downAct = new QAction(themedIcon("go-down", QStyle::SP_ArrowDown),
                            tr("Move &Down"), this);
    m_downAct->setShortcut(QStringLiteral("Ctrl+Down"));
    connect(m_downAct, &QAction::triggered, this, [this] { moveSelected(1); });

    // The cdlabel app icon, so the action visually points at the editor it
    // launches. Same theme-then-embedded lookup as the app icons in main().
    m_labelAct = new QAction(
        QIcon::fromTheme(QStringLiteral("cdlabel"),
                         QIcon(QStringLiteral(":/icons/cdlabel.svg"))),
        tr("Create &Label…"), this);
    m_labelAct->setToolTip(tr("Design a printable label in the cdlabel editor"));
    connect(m_labelAct, &QAction::triggered, this, &MainWindow::createLabel);
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_newAct);
    fileMenu->addAction(m_openAct);
    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    rebuildRecentMenu();
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAct);
    fileMenu->addAction(m_saveAsAct);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportAct);
    fileMenu->addAction(m_burnAct);
    fileMenu->addSeparator();
    QAction *quitAct = fileMenu->addAction(tr("&Quit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &MainWindow::close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_addAct);
    editMenu->addAction(m_removeAct);
    editMenu->addSeparator();
    editMenu->addAction(m_upAct);
    editMenu->addAction(m_downAct);

    QMenu *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addAction(m_labelAct);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAct = helpMenu->addAction(tr("&About BinCue Studio"));
    connect(aboutAct, &QAction::triggered, this, &MainWindow::showAbout);

    // LGPL notice for the linked Qt libraries.
    QAction *aboutQtAct = helpMenu->addAction(tr("About &Qt"));
    connect(aboutQtAct, &QAction::triggered, qApp, &QApplication::aboutQt);
}

void MainWindow::buildToolBar()
{
    QToolBar *bar = addToolBar(tr("Main"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->setMovable(false);
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bar->addAction(m_newAct);
    bar->addAction(m_openAct);
    bar->addAction(m_saveAct);
    bar->addSeparator();
    bar->addAction(m_addAct);
    bar->addAction(m_removeAct);
    bar->addAction(m_upAct);
    bar->addAction(m_downAct);
    bar->addSeparator();
    bar->addAction(m_labelAct);
    bar->addAction(m_exportAct);
    bar->addAction(m_burnAct);
}

// -- Central widget ----------------------------------------------------------

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    setCentralWidget(central);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // Disc info: two compact form columns instead of one tall form. Wrapped in
    // a collapsible section — a framed card with a flat header button carrying a
    // disclosure arrow (▾/▸); pressing it folds the body away to give the track
    // table more room.
    auto *discSection = new QFrame;
    discSection->setFrameShape(QFrame::StyledPanel);
    auto *discOuter = new QVBoxLayout(discSection);
    discOuter->setContentsMargins(0, 0, 0, 0);
    discOuter->setSpacing(0);

    auto *discToggle = new QToolButton;
    discToggle->setText(tr("Disc"));
    discToggle->setCheckable(true);
    discToggle->setAutoRaise(true);
    discToggle->setArrowType(Qt::DownArrow);
    discToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    discToggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    discToggle->setCursor(Qt::PointingHandCursor);
    discToggle->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; font-weight: bold; padding: 6px 8px; "
        "text-align: left; }"));
    discOuter->addWidget(discToggle);

    auto *discBody = new QWidget;
    discOuter->addWidget(discBody);
    // A single narrow column: the two mandatory CD-Text fields, a compact row of
    // burn settings, then the action buttons. Everything else lives behind the
    // Disc details button, so the whole panel is only a handful of rows tall.
    auto *discCol = new QVBoxLayout(discBody);
    discCol->setContentsMargins(4, 4, 4, 8);
    discCol->setSpacing(8);

    auto *metaForm = new QFormLayout;
    metaForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    // Keep a clear gutter between the labels and their input fields; several
    // styles default this to 0, gluing them together.
    metaForm->setHorizontalSpacing(10);
    metaForm->setVerticalSpacing(8);
    discCol->addLayout(metaForm);

    m_albumTitleEdit = new QLineEdit;
    m_albumPerformerEdit = new QLineEdit;

    m_discSizeCombo = new QComboBox;
    for (const DiscSize &size : DISC_SIZES)
        m_discSizeCombo->addItem(QLatin1String(size.label));
    connect(m_discSizeCombo, &QComboBox::currentIndexChanged, this,
            &MainWindow::updateCapacity);

    m_pregapSpin = new QDoubleSpinBox;
    m_pregapSpin->setRange(0.0, 10.0);
    m_pregapSpin->setSingleStep(0.5);
    m_pregapSpin->setDecimals(1);
    m_pregapSpin->setSuffix(tr(" s"));
    m_pregapSpin->setValue(redbook::DEFAULT_PREGAP_SECONDS);
    m_pregapSpin->setToolTip(
        tr("Track 1 pre-gap (PREGAP). The Red Book fixes this at 2s; "
           "other values are off-spec and may not play back reliably."));
    connect(m_pregapSpin, &QDoubleSpinBox::valueChanged, this,
            &MainWindow::updateCapacity);
    connect(m_pregapSpin, &QDoubleSpinBox::valueChanged, this,
            &MainWindow::updatePregapWarning);

    // Warning shown when the track 1 pre-gap is pushed off the 2s Red Book
    // standard. A flat clickable button that explains itself when pressed.
    m_pregapWarnBtn = new QToolButton;
    m_pregapWarnBtn->setText(QStringLiteral("⚠"));
    m_pregapWarnBtn->setAutoRaise(true);
    m_pregapWarnBtn->setStyleSheet(
        QStringLiteral("color: #cc8800; font-weight: bold;"));
    m_pregapWarnBtn->setCursor(Qt::PointingHandCursor);
    connect(m_pregapWarnBtn, &QToolButton::clicked, this,
            &MainWindow::showPregapWarning);
    m_pregapWarnBtn->hide();
    auto *pregapRow = new QHBoxLayout;
    pregapRow->setContentsMargins(0, 0, 0, 0);
    pregapRow->addWidget(m_pregapSpin, 1);
    pregapRow->addWidget(m_pregapWarnBtn);

    m_gapSpin = new QDoubleSpinBox;
    m_gapSpin->setRange(0.0, 10.0);
    m_gapSpin->setSingleStep(0.5);
    m_gapSpin->setDecimals(1);
    m_gapSpin->setSuffix(tr(" s"));
    m_gapSpin->setValue(redbook::DEFAULT_GAP_SECONDS);
    m_gapSpin->setToolTip(
        tr("Gap every track ends up with (0 after the last). Each track's "
           "baked-in gap is trimmed or filled to reach this."));
    connect(m_gapSpin, &QDoubleSpinBox::valueChanged, this,
            &MainWindow::updateCapacity);

    // Always-visible info hint: the inter-track gap defaults to 2s but, unlike
    // the track 1 pre-gap, isn't fixed by the Red Book, so it can be trimmed to
    // reclaim program time. A flat clickable button that explains on press.
    m_gapInfoBtn = new QToolButton;
    m_gapInfoBtn->setText(QStringLiteral("ⓘ"));
    m_gapInfoBtn->setAutoRaise(true);
    m_gapInfoBtn->setStyleSheet(
        QStringLiteral("color: #4a90d9; font-weight: bold;"));
    m_gapInfoBtn->setCursor(Qt::PointingHandCursor);
    m_gapInfoBtn->setToolTip(
        tr("The 2s gap is a convention, not enforced. Click for details."));
    connect(m_gapInfoBtn, &QToolButton::clicked, this,
            &MainWindow::showGapInfo);
    auto *gapRow = new QHBoxLayout;
    gapRow->setContentsMargins(0, 0, 0, 0);
    gapRow->addWidget(m_gapSpin, 1);
    gapRow->addWidget(m_gapInfoBtn);

    auto *fillBtn = new QPushButton(tr("Fill from Selected Track…"));
    fillBtn->setToolTip(
        tr("Copy album title, performer, songwriter, genre, year and catalog "
           "from the selected track's tags into these disc-wide fields."));
    connect(fillBtn, &QPushButton::clicked, this,
            &MainWindow::fillDiscInfoFromTrack);

    // Door to the disc-wide fields kept off the panel (songwriter, genre, year,
    // catalog, composer, arranger, message, disc id) — the disc counterpart of
    // the track "Details…" button.
    m_discDetailsBtn = new QPushButton(tr("Disc Details…"));
    m_discDetailsBtn->setToolTip(
        tr("Edit the disc-wide songwriter, genre, year, catalog, composer, "
           "arranger, message and disc id."));
    connect(m_discDetailsBtn, &QPushButton::clicked, this,
            &MainWindow::openDiscDetails);

    // Error badges for the two fields cdrdao makes mandatory once any CD-Text
    // exists. A flat red button that explains itself when pressed; hidden until
    // updateCdTextWarnings() decides the field is a problem. Each rides in an
    // HBox to the right of its edit, like the pregap warning.
    auto makeErrBadge = [this](QToolButton *&btn, QLineEdit *edit) -> QLayout * {
        btn = new QToolButton;
        btn->setText(QStringLiteral("⚠"));
        btn->setAutoRaise(true);
        btn->setStyleSheet(
            QStringLiteral("color: #d13438; font-weight: bold;"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->hide();
        connect(btn, &QToolButton::clicked, this,
                &MainWindow::showCdTextWarning);
        auto *rowLayout = new QHBoxLayout;
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(edit, 1);
        rowLayout->addWidget(btn);
        return rowLayout;
    };

    metaForm->addRow(tr("Title:"),
                     makeErrBadge(m_titleErrBtn, m_albumTitleEdit));
    metaForm->addRow(tr("Performer:"),
                     makeErrBadge(m_performerErrBtn, m_albumPerformerEdit));

    // The three burn settings that shape the capacity bar packed onto one line,
    // each with an inline label, rather than a stacked form of their own.
    auto *settingsRow = new QHBoxLayout;
    settingsRow->setContentsMargins(0, 0, 0, 0);
    settingsRow->setSpacing(8);
    settingsRow->addWidget(new QLabel(tr("Disc size:")));
    settingsRow->addWidget(m_discSizeCombo, 1);
    settingsRow->addSpacing(6);
    settingsRow->addWidget(new QLabel(tr("Pre-gap:")));
    settingsRow->addLayout(pregapRow);
    settingsRow->addSpacing(6);
    settingsRow->addWidget(new QLabel(tr("Gap:")));
    settingsRow->addLayout(gapRow);
    discCol->addLayout(settingsRow);

    // Fill and Disc details share one row instead of stacking as two.
    auto *discBtnRow = new QHBoxLayout;
    discBtnRow->setContentsMargins(0, 0, 0, 0);
    discBtnRow->setSpacing(8);
    discBtnRow->addWidget(fillBtn, 1);
    discBtnRow->addWidget(m_discDetailsBtn, 1);
    discCol->addLayout(discBtnRow);

    root->addWidget(discSection);

    // Restore the collapsed/expanded state from the previous session and keep
    // it in sync. Toggling the header button folds the body and flips the arrow
    // between ▾ (expanded) and ▸ (collapsed).
    const bool discExpanded =
        m_settings.value(QStringLiteral("discInfoExpanded"), true).toBool();
    discToggle->setChecked(discExpanded);
    discBody->setVisible(discExpanded);
    discToggle->setArrowType(discExpanded ? Qt::DownArrow : Qt::RightArrow);
    connect(discToggle, &QToolButton::toggled, this,
            [this, discToggle, discBody](bool on) {
                discBody->setVisible(on);
                discToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                m_settings.setValue(QStringLiteral("discInfoExpanded"), on);
            });

    // Mark the project dirty on any user edit to the two panel fields. The rest
    // of the disc info is edited through the dialog, which marks dirty on accept.
    // textEdited (unlike textChanged) only fires for user input, not programmatic
    // setText, so loading a project doesn't spuriously flag it as modified.
    for (QLineEdit *edit : {m_albumTitleEdit, m_albumPerformerEdit})
        connect(edit, &QLineEdit::textEdited, this, &MainWindow::markDirty);
    // The mandatory-field badges depend on the disc title and performer as much
    // as on the tracks, so refresh them whenever one changes.
    for (QLineEdit *edit : {m_albumTitleEdit, m_albumPerformerEdit})
        connect(edit, &QLineEdit::textChanged, this,
                &MainWindow::updateCdTextWarnings);
    connect(m_discSizeCombo, &QComboBox::activated, this,
            &MainWindow::markDirty);
    connect(m_pregapSpin, &QDoubleSpinBox::valueChanged, this,
            &MainWindow::markDirty);
    connect(m_gapSpin, &QDoubleSpinBox::valueChanged, this,
            &MainWindow::markDirty);

    // Track table. The vertical header doubles as the track number.
    m_table = new QTableWidget(0, ColumnCount);
    m_table->setHorizontalHeaderLabels({QString(), tr("Title"), tr("Performer"),
                                        tr("Duration"), tr("Baked-in Gap"),
                                        tr("Actions")});
    QHeaderView *header = m_table->horizontalHeader();
    header->setSectionResizeMode(ColPlay, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColTitle, QHeaderView::Stretch);
    header->setSectionResizeMode(ColPerformer, QHeaderView::Stretch);
    header->setSectionResizeMode(ColDuration, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColActions, QHeaderView::ResizeToContents);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_table->addActions({m_addAct, m_removeAct, m_upAct, m_downAct});
    connect(m_table, &QTableWidget::itemChanged, this,
            &MainWindow::onItemChanged);
    root->addWidget(m_table, 1);

    // Preview player: hear the assembled program — every track, with the exact
    // inter-track gaps, seamlessly — before committing it to a disc.
    m_player = new PlaybackEngine(this);

    auto *playerBar = new QFrame;
    playerBar->setFrameShape(QFrame::StyledPanel);
    auto *playerRow = new QHBoxLayout(playerBar);
    playerRow->setContentsMargins(8, 4, 8, 4);
    playerRow->setSpacing(6);

    auto *style = qApp->style();
    m_prevBtn = new QToolButton;
    m_prevBtn->setAutoRaise(true);
    m_prevBtn->setIcon(themedIcon("media-skip-backward",
                                  QStyle::SP_MediaSkipBackward));
    m_prevBtn->setToolTip(tr("Previous track"));
    m_playPauseBtn = new QToolButton;
    m_playPauseBtn->setAutoRaise(true);
    m_playPauseBtn->setIcon(style->standardIcon(QStyle::SP_MediaPlay));
    m_playPauseBtn->setToolTip(tr("Play / Pause"));
    m_stopBtn = new QToolButton;
    m_stopBtn->setAutoRaise(true);
    m_stopBtn->setIcon(style->standardIcon(QStyle::SP_MediaStop));
    m_stopBtn->setToolTip(tr("Stop"));
    m_nextBtn = new QToolButton;
    m_nextBtn->setAutoRaise(true);
    m_nextBtn->setIcon(themedIcon("media-skip-forward",
                                  QStyle::SP_MediaSkipForward));
    m_nextBtn->setToolTip(tr("Next track"));

    m_posLabel = new QLabel(formatClock(0));
    m_posLabel->setToolTip(tr("Elapsed"));
    m_seekSlider = new QSlider(Qt::Horizontal);
    m_seekSlider->setRange(0, 0);
    // Click the groove to seek there, not just drag the handle. The style is
    // parented to the slider so it's cleaned up with it (setStyle doesn't own
    // it).
    auto *seekStyle = new DirectJumpStyle;
    seekStyle->setParent(m_seekSlider);
    m_seekSlider->setStyle(seekStyle);
    m_totalLabel = new QLabel(formatClock(0));
    m_totalLabel->setToolTip(tr("Total program length"));
    m_nowPlayingLabel = new QLabel;
    m_nowPlayingLabel->setTextFormat(Qt::PlainText);
    // Shares a row with the seek slider, so its width must not follow the
    // text: a longer title would steal groove width and visually jump the
    // slider handle at every track change. Fix the width and elide instead.
    m_nowPlayingLabel->setFixedWidth(
        m_nowPlayingLabel->fontMetrics().averageCharWidth() * 36);
    setNowPlayingIdle();

    playerRow->addWidget(m_prevBtn);
    playerRow->addWidget(m_playPauseBtn);
    playerRow->addWidget(m_stopBtn);
    playerRow->addWidget(m_nextBtn);
    playerRow->addSpacing(6);
    playerRow->addWidget(m_posLabel);
    playerRow->addWidget(m_seekSlider, 1);
    playerRow->addWidget(m_totalLabel);
    playerRow->addSpacing(8);
    playerRow->addWidget(m_nowPlayingLabel);
    root->addWidget(playerBar);

    // Transport.
    connect(m_playPauseBtn, &QToolButton::clicked, m_player,
            &PlaybackEngine::togglePlayPause);
    connect(m_stopBtn, &QToolButton::clicked, m_player, &PlaybackEngine::stop);
    connect(m_prevBtn, &QToolButton::clicked, this, [this] {
        m_player->seekToTrack(qMax(0, m_player->currentTrack() - 1));
        if (m_player->state() != PlaybackEngine::State::Playing)
            m_player->play();
    });
    connect(m_nextBtn, &QToolButton::clicked, this, [this] {
        const int next = m_player->currentTrack() + 1;
        if (next < m_player->trackCount()) {
            m_player->seekToTrack(next);
            if (m_player->state() != PlaybackEngine::State::Playing)
                m_player->play();
        }
    });

    // While stopped, selecting a row arms Play to begin there.
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &current) {
                if (current.isValid()
                    && m_player->state() == PlaybackEngine::State::Stopped)
                    m_player->setStartTrack(current.row());
            });

    // Seek slider: freeze the auto-updates while the user drags, commit on
    // release.
    connect(m_seekSlider, &QSlider::sliderPressed, this,
            [this] { m_sliderHeld = true; });
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int value) {
        m_posLabel->setText(formatClock(value));
    });
    connect(m_seekSlider, &QSlider::sliderReleased, this, [this] {
        m_sliderHeld = false;
        m_player->seek(m_seekSlider->value());
    });

    // Engine → UI.
    connect(m_player, &PlaybackEngine::stateChanged, this,
            [this](PlaybackEngine::State state) {
                const bool playing = state == PlaybackEngine::State::Playing;
                m_playPauseBtn->setIcon(qApp->style()->standardIcon(
                    playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
                m_stopBtn->setEnabled(state != PlaybackEngine::State::Stopped);
                updatePlayButtons();
            });
    connect(m_player, &PlaybackEngine::positionChanged, this,
            [this](qint64 pos, qint64 total) {
                if (!m_sliderHeld) {
                    m_seekSlider->setRange(0, int(total));
                    m_seekSlider->setValue(int(pos));
                    m_posLabel->setText(formatClock(pos));
                }
                m_totalLabel->setText(formatClock(total));
            });
    connect(m_player, &PlaybackEngine::currentTrackChanged, this,
            [this](int index) {
                updatePlayButtons();
                if (index < 0 || index >= m_tracks.size()) {
                    setNowPlayingIdle();
                    return;
                }
                const Track &t = m_tracks[index];
                QString text = QStringLiteral("%1. %2")
                                   .arg(index + 1)
                                   .arg(t.title.isEmpty()
                                            ? tr("(untitled)")
                                            : t.title);
                if (!t.performer.isEmpty())
                    text += QStringLiteral(" — %1").arg(t.performer);
                m_nowPlayingLabel->setEnabled(true);
                m_nowPlayingLabel->setToolTip(text);
                m_nowPlayingLabel->setText(m_nowPlayingLabel->fontMetrics().elidedText(
                    text, Qt::ElideRight, m_nowPlayingLabel->width()));
            });
    connect(m_player, &PlaybackEngine::errorOccurred, this,
            [this](const QString &message) {
                QMessageBox::warning(this, tr("Playback"), message);
            });

    // Bottom strip: capacity meter on the left, primary actions on the right.
    auto *bottomRow = new QHBoxLayout;
    m_capacityBar = new QProgressBar;
    m_capacityBar->setTextVisible(false);
    m_capacityBar->setFixedHeight(16);
    m_capacityLabel = new QLabel;
    bottomRow->addWidget(m_capacityBar, 1);
    bottomRow->addWidget(m_capacityLabel);
    bottomRow->addSpacing(16);
    auto *labelBtn = new QPushButton(m_labelAct->icon(), tr("Create Label…"));
    connect(labelBtn, &QPushButton::clicked, m_labelAct, &QAction::trigger);
    bottomRow->addWidget(labelBtn);
    m_exportBtn =
        new QPushButton(m_exportAct->icon(), tr("Export BIN + CUE…"));
    connect(m_exportBtn, &QPushButton::clicked, m_exportAct,
            &QAction::trigger);
    bottomRow->addWidget(m_exportBtn);
    auto *burnBtn = new QPushButton(m_burnAct->icon(), tr("Burn to Disc…"));
    burnBtn->setDefault(true);
    connect(burnBtn, &QPushButton::clicked, m_burnAct, &QAction::trigger);
    bottomRow->addWidget(burnBtn);
    root->addLayout(bottomRow);

    // Reflect the (empty) initial disc state on the details button and badges.
    updateCdTextWarnings();
}

// -- Window title / dirty state ----------------------------------------------

void MainWindow::updateTitle()
{
    const QString name = m_projectPath.isEmpty()
                             ? tr("Untitled")
                             : QFileInfo(m_projectPath).fileName();
    setWindowTitle(QStringLiteral("%1%2 — BinCue Studio")
                       .arg(m_dirty ? QStringLiteral("*") : QString(), name));
}

void MainWindow::markDirty()
{
    if (!m_dirty) {
        m_dirty = true;
        updateTitle();
    }
}

void MainWindow::setClean()
{
    m_dirty = false;
    updateTitle();
}

// -- Recent files -------------------------------------------------------------

QStringList MainWindow::recentFiles() const
{
    return m_settings.value(QStringLiteral("recentFiles")).toStringList();
}

void MainWindow::addRecentFile(const QString &path)
{
    const QString absolute =
        QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    QStringList files = recentFiles();
    files.removeAll(absolute);
    files.prepend(absolute);
    while (files.size() > MAX_RECENT_FILES)
        files.removeLast();
    m_settings.setValue(QStringLiteral("recentFiles"), files);
    rebuildRecentMenu();
}

void MainWindow::clearRecentFiles()
{
    m_settings.setValue(QStringLiteral("recentFiles"), QStringList());
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    QStringList files;
    for (const QString &f : recentFiles())
        if (QFileInfo(f).isFile())
            files.append(f);
    if (files.isEmpty()) {
        QAction *empty = m_recentMenu->addAction(tr("(No recent projects)"));
        empty->setEnabled(false);
        return;
    }
    for (qsizetype i = 0; i < files.size(); ++i) {
        const QString path = files[i];
        QAction *act = m_recentMenu->addAction(
            QStringLiteral("&%1  %2").arg(i + 1).arg(
                QFileInfo(path).fileName()));
        act->setStatusTip(path);
        connect(act, &QAction::triggered, this,
                [this, path] { openProject(path); });
    }
    m_recentMenu->addSeparator();
    QAction *clearAct = m_recentMenu->addAction(tr("Clear Recent"));
    connect(clearAct, &QAction::triggered, this,
            &MainWindow::clearRecentFiles);
}

// -- Track table ---------------------------------------------------------------

void MainWindow::onItemChanged(QTableWidgetItem *item)
{
    const int row = item->row();
    if (row >= m_tracks.size())
        return;
    Track &track = m_tracks[row];
    switch (item->column()) {
    case ColTitle:
        track.title = item->text();
        break;
    case ColPerformer:
        track.performer = item->text();
        break;
    default:
        return;
    }
    markDirty();
    // Editing a track title/performer can flip whether CD-Text exists.
    updateCdTextWarnings();
}

void MainWindow::refreshTable()
{
    m_table->blockSignals(true);
    m_table->setRowCount(int(m_tracks.size()));
    // Rebuilt row by row below; the old buttons are destroyed with their cells.
    m_playButtons.clear();
    for (int row = 0; row < m_tracks.size(); ++row) {
        const Track &track = m_tracks[row];

        // Play from this track. On the track that's already active it doubles as
        // a play/pause toggle; double-clicking a field must never start audio.
        auto *playBtn = new QToolButton;
        playBtn->setAutoRaise(true);
        playBtn->setToolTip(tr("Play from this track"));
        connect(playBtn, &QToolButton::clicked, this, [this, row] {
            if (m_player->currentTrack() == row
                && m_player->state() != PlaybackEngine::State::Stopped) {
                m_player->togglePlayPause();
            } else {
                m_player->seekToTrack(row);
                if (m_player->state() != PlaybackEngine::State::Playing)
                    m_player->play();
            }
        });
        m_table->setCellWidget(row, ColPlay, playBtn);
        m_playButtons.append(playBtn);

        auto *titleItem = new QTableWidgetItem(track.title);
        auto *perfItem = new QTableWidgetItem(track.performer);

        auto *durItem = new QTableWidgetItem(
            redbook::secondsToMmss(track.durationSeconds));
        durItem->setFlags(durItem->flags() & ~Qt::ItemIsEditable);
        durItem->setTextAlignment(Qt::AlignCenter);

        m_table->setItem(row, ColTitle, titleItem);
        m_table->setItem(row, ColPerformer, perfItem);
        m_table->setItem(row, ColDuration, durItem);

        auto *bakedSpin = new QDoubleSpinBox;
        bakedSpin->setRange(0.0, 30.0);
        bakedSpin->setSingleStep(0.5);
        bakedSpin->setDecimals(1);
        bakedSpin->setSuffix(tr(" s"));
        bakedSpin->setFrame(false);
        // Let the row's alternating colour show through instead of the spin
        // box painting its own opaque base colour over it.
        bakedSpin->setStyleSheet(
            QStringLiteral("QDoubleSpinBox { background: transparent; }"));
        bakedSpin->setValue(track.bakedInGap);
        bakedSpin->setToolTip(
            tr("Trailing silence this source already has. Trimmed or filled "
               "to match the inter-track gap on export."));
        connect(bakedSpin, &QDoubleSpinBox::valueChanged, this,
                [this, row](double value) {
                    if (row < m_tracks.size()) {
                        m_tracks[row].bakedInGap = value;
                        markDirty();
                        updateCapacity();
                    }
                });
        m_table->setCellWidget(row, ColBaked, bakedSpin);

        // Per-track actions, kept compact so the table doesn't grow a column per
        // verb: edit the hidden metadata, and re-import the source file.
        auto *actions = new QWidget;
        auto *actionsLayout = new QHBoxLayout(actions);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->setSpacing(2);

        // A door to the fields kept out of the table (songwriter, composer,
        // arranger, message, ISRC and the subchannel flags).
        const bool hasDetails = TrackDetailsDialog::hasDetails(track);
        auto *detailsBtn = new QPushButton(tr("Edit"));
        detailsBtn->setFlat(true);
        // Rows that already carry some of that hidden metadata get a bold label
        // so they still stand out — cleaner than a glyph in the text.
        if (hasDetails) {
            QFont font = detailsBtn->font();
            font.setBold(true);
            detailsBtn->setFont(font);
        }
        detailsBtn->setToolTip(
            hasDetails
                ? tr("This track has extra metadata. Edit songwriter, composer, "
                     "arranger, message, ISRC and playback flags.")
                : tr("Edit songwriter, composer, arranger, message, ISRC and "
                     "playback flags for this track."));
        connect(detailsBtn, &QPushButton::clicked, this,
                [this, row] { openTrackDetails(row); });

        // Re-point this track at its source file — after a move or a re-encode —
        // optionally pulling the new file's tags in as well.
        auto *reimportBtn = new QToolButton;
        reimportBtn->setIcon(
            themedIcon("view-refresh", QStyle::SP_BrowserReload));
        reimportBtn->setAutoRaise(true);
        reimportBtn->setToolTip(
            tr("Re-import: pick this track's source file again, optionally "
               "refreshing its tags."));
        connect(reimportBtn, &QToolButton::clicked, this,
                [this, row] { reimportTrack(row); });

        actionsLayout->addWidget(detailsBtn);
        actionsLayout->addWidget(reimportBtn);
        m_table->setCellWidget(row, ColActions, actions);
    }
    m_table->blockSignals(false);
    updatePlayButtons();
    updateCapacity();
    // Track CD-Text may have changed, so a disc Title/Performer that was
    // optional could now be mandatory (or vice versa).
    updateCdTextWarnings();
}

// Point each row's play button at the right glyph: a pause icon on the track
// that's currently playing, a play icon everywhere else.
void MainWindow::updatePlayButtons()
{
    auto *style = qApp->style();
    const int current = m_player->currentTrack();
    const bool playing = m_player->state() == PlaybackEngine::State::Playing;
    for (int row = 0; row < m_playButtons.size(); ++row) {
        const bool active = row == current
            && m_player->state() != PlaybackEngine::State::Stopped;
        m_playButtons[row]->setIcon(style->standardIcon(
            (active && playing) ? QStyle::SP_MediaPause
                                : QStyle::SP_MediaPlay));
    }
}

// Muted placeholder for the now-playing readout so the transport row never
// looks broken-empty when playback is stopped.
void MainWindow::setNowPlayingIdle()
{
    m_nowPlayingLabel->setEnabled(false);
    m_nowPlayingLabel->setToolTip(QString());
    m_nowPlayingLabel->setText(tr("Nothing playing"));
}

QList<int> MainWindow::selectedRows() const
{
    QList<int> rows;
    const QModelIndexList indexes = m_table->selectionModel()->selectedRows();
    for (const QModelIndex &idx : indexes)
        rows.append(idx.row());
    std::sort(rows.begin(), rows.end());
    return rows;
}

void MainWindow::addTracks()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Tracks"), QString(), audioFileFilter());
    if (paths.isEmpty())
        return;

    QStringList errors;
    for (const QString &path : paths) {
        double duration = 0.0;
        QString error;
        if (!probeDurationSeconds(path, duration, error)) {
            errors.append(QStringLiteral("%1: %2")
                              .arg(QFileInfo(path).fileName(), error));
            continue;
        }

        Track track;
        track.sourcePath = path;
        track.durationSeconds = duration;
        importTagsInto(track);
        m_tracks.append(track);
        markDirty();
    }

    refreshTable();

    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Some files could not be read"),
                             tr("The following files were skipped:\n\n%1")
                                 .arg(errors.join(QLatin1Char('\n'))));
}

void MainWindow::removeSelected()
{
    QList<int> rows = selectedRows();
    if (rows.isEmpty())
        return;
    for (auto it = rows.crbegin(); it != rows.crend(); ++it)
        m_tracks.removeAt(*it);
    markDirty();
    refreshTable();
}

void MainWindow::moveSelected(int direction)
{
    const QList<int> rows = selectedRows();
    if (rows.size() != 1)
        return;
    const int row = rows.first();
    const int newRow = row + direction;
    if (newRow < 0 || newRow >= m_tracks.size())
        return;
    m_tracks.swapItemsAt(row, newRow);
    markDirty();
    refreshTable();
    m_table->selectRow(newRow);
}

void MainWindow::openTrackDetails(int row)
{
    if (row < 0 || row >= m_tracks.size())
        return;
    TrackDetailsDialog dialog(m_tracks[row], this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_tracks[row] = dialog.track();
    markDirty();
    // The row's own cells don't change, but the button's badge might, so redraw.
    refreshTable();
}

void MainWindow::openDiscDetails()
{
    DiscDetailsDialog dialog(gatherMetadataParams(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_albumSongwriter = dialog.songwriter();
    m_albumGenre = dialog.genre();
    m_albumYear = dialog.year();
    m_albumCatalog = dialog.catalog();
    m_albumComposer = dialog.composer();
    m_albumArranger = dialog.arranger();
    m_albumMessage = dialog.message();
    m_albumDiscId = dialog.discId();
    markDirty();
    // A disc pack type may have appeared or gone — refresh the button badge and
    // every warning.
    updateCdTextWarnings();
}

ExportWorker::Params MainWindow::gatherMetadataParams() const
{
    ExportWorker::Params p;
    p.tracks = m_tracks;
    p.albumTitle = m_albumTitleEdit->text();
    p.albumPerformer = m_albumPerformerEdit->text();
    p.albumSongwriter = m_albumSongwriter;
    p.albumComposer = m_albumComposer;
    p.albumArranger = m_albumArranger;
    p.albumMessage = m_albumMessage;
    p.albumDiscId = m_albumDiscId;
    p.albumGenre = m_albumGenre;
    p.albumYear = m_albumYear;
    p.albumCatalog = m_albumCatalog;
    return p;
}

void MainWindow::applyCdTextToProject(const ExportWorker::Params &params)
{
    // The completion prompt only ever fills blank tracks or drops a whole
    // non-mandatory pack, so the fills all ride in params.tracks and the only
    // disc values it can have cleared are the non-mandatory ones. Title and
    // Performer are never dropped, so their disc edits are left untouched.
    m_tracks = params.tracks;
    m_albumSongwriter = params.albumSongwriter;
    m_albumComposer = params.albumComposer;
    m_albumArranger = params.albumArranger;
    m_albumMessage = params.albumMessage;

    refreshTable();
    markDirty();
    updateCdTextWarnings();
}

void MainWindow::updateCdTextWarnings()
{
    if (!m_titleErrBtn)
        return;  // called before buildUi finished

    const ExportWorker::Params p = gatherMetadataParams();
    auto attn = [&p](cdtext::Pack pack) {
        return cdtext::needsAttention(p, cdtext::field(pack));
    };

    // Each panel field flags exactly when the completion prompt would list it.
    m_titleErrBtn->setVisible(attn(cdtext::Pack::Title));
    m_performerErrBtn->setVisible(attn(cdtext::Pack::Performer));

    // The disc-details fields have no panel row, so roll their warnings up onto
    // the button: ⚠ if any is partial, else • if any merely carries a value.
    const bool discAttn = attn(cdtext::Pack::Songwriter)
        || attn(cdtext::Pack::Composer) || attn(cdtext::Pack::Arranger)
        || attn(cdtext::Pack::Message);
    const bool has = DiscDetailsDialog::hasDetails(p);
    QString label = tr("Disc Details…");
    if (discAttn)
        label += QStringLiteral("  ⚠");
    else if (has)
        label += QStringLiteral("  •");
    m_discDetailsBtn->setText(label);
    m_discDetailsBtn->setStyleSheet(
        discAttn ? QStringLiteral("color: #d13438;") : QString());
}

void MainWindow::showCdTextWarning()
{
    QMessageBox::warning(
        this, tr("CD-Text field incomplete"),
        tr("This disc has CD-Text, so cdrdao needs this field on the whole "
           "disc and every track (or nowhere at all) — otherwise the CD-Text "
           "can't be written.\n\nFill it in here (and on any track that lacks "
           "it). You'll also be prompted to complete it when you burn or "
           "export."));
}

void MainWindow::reimportTrack(int row)
{
    if (row < 0 || row >= m_tracks.size())
        return;
    Track &track = m_tracks[row];

    // Start the picker in the old source's folder — the replacement is usually
    // right next to it (a re-encode) or the whole set has moved together.
    const QString startDir = QFileInfo(track.sourcePath).absolutePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Re-import Track Source"), startDir, audioFileFilter());
    if (path.isEmpty())
        return;

    double duration = 0.0;
    QString error;
    if (!probeDurationSeconds(path, duration, error)) {
        QMessageBox::warning(
            this, tr("Could not read file"),
            tr("%1 could not be read, so the track was left unchanged:\n\n%2")
                .arg(QFileInfo(path).fileName(), error));
        return;
    }

    track.sourcePath = path;
    track.durationSeconds = duration;

    // Swapping the file always re-points the source; the tags are only touched
    // if the user asks, so hand-edited metadata survives a plain re-point.
    if (tagReaderAvailable()) {
        const auto reply = QMessageBox::question(
            this, tr("Re-import tags?"),
            tr("Re-import this track's tags from the selected file?\n\n"
               "This overwrites the title, performer, songwriter, composer and "
               "ISRC with the file's tags. The arranger, message and playback "
               "flags are left unchanged."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply == QMessageBox::Yes)
            importTagsInto(track);
    }

    markDirty();
    refreshTable();
    statusBar()->showMessage(
        tr("Re-imported %1.").arg(QFileInfo(path).fileName()), 4000);
}

void MainWindow::fillDiscInfoFromTrack()
{
    // Populate the album-wide Disc fields from the tags of the single
    // selected track. This is an explicit action (not done on import) because
    // a mixed compilation has no single "album" — the user points at the one
    // track whose album metadata should stand for the whole disc.
    if (!tagReaderAvailable()) {
        QMessageBox::warning(
            this, tr("Tag reading not available"),
            tr("This build has no TagLib support, so tags cannot be read. "
               "Rebuild with TagLib installed."));
        return;
    }
    const QList<int> rows = selectedRows();
    if (rows.size() != 1) {
        QMessageBox::information(
            this, tr("Pick one track"),
            tr("Select the single track whose album tags should fill the "
               "Disc fields."));
        return;
    }
    const Track &track = m_tracks[rows.first()];
    const AlbumTags tags = readAlbumTags(track.sourcePath);
    const QString fileName = QFileInfo(track.sourcePath).fileName();

    // Explicit action: replace each field the source actually carries; a
    // missing tag leaves whatever is already there rather than blanking it. The
    // two panel edits and the dialog-backed strings are filled the same way.
    bool applied = false;
    auto fillEdit = [&](QLineEdit *edit, const QString &value) {
        if (!value.isEmpty()) {
            edit->setText(value);
            applied = true;
        }
    };
    auto fillStr = [&](QString &dest, const QString &value) {
        if (!value.isEmpty()) {
            dest = value;
            applied = true;
        }
    };
    fillEdit(m_albumTitleEdit, tags.title);
    fillEdit(m_albumPerformerEdit, tags.performer);
    fillStr(m_albumSongwriter, tags.songwriter);
    fillStr(m_albumGenre, tags.genre);
    fillStr(m_albumYear, tags.year);
    fillStr(m_albumCatalog, tags.catalog);
    if (applied) {
        markDirty();
        // Songwriter feeds the Disc details badge, so refresh it.
        updateCdTextWarnings();
        statusBar()->showMessage(
            tr("Disc info filled from %1.").arg(fileName), 4000);
    } else {
        statusBar()->showMessage(
            tr("%1 has no album tags to copy.").arg(fileName), 4000);
    }
}

// -- Pre-gap Red Book warning ---------------------------------------------------

void MainWindow::updatePregapWarning()
{
    // Flag a track 1 pre-gap that has drifted off the 2s Red Book value.
    const bool offSpec =
        std::abs(m_pregapSpin->value() - redbook::REDBOOK_PREGAP_SECONDS)
        > 1e-9;
    m_pregapWarnBtn->setVisible(offSpec);
    if (offSpec)
        m_pregapWarnBtn->setToolTip(
            tr("Off-spec: the Red Book fixes the track 1 pre-gap at %1s. "
               "Click for details.")
                .arg(redbook::REDBOOK_PREGAP_SECONDS));
}

void MainWindow::showPregapWarning()
{
    QMessageBox::warning(
        this, tr("Non-standard track 1 pre-gap"),
        tr("The Red Book standard fixes track 1's pre-gap (PREGAP) at "
           "%1 seconds. A value above 2s is against the standard, and other "
           "values may not be honoured by every player or burner.\n\nThis "
           "only affects the pre-gap before track 1 — the gap between tracks "
           "is set separately by \"Gap between tracks\".")
            .arg(redbook::REDBOOK_PREGAP_SECONDS));
}

void MainWindow::showGapInfo()
{
    QMessageBox::information(
        this, tr("Gap between tracks"),
        tr("The standard gap between tracks is %1 seconds, and that's the "
           "default here. Unlike track 1's pre-gap, though, this gap "
           "isn't fixed by the Red Book — it's only a convention, and players "
           "and burners honour whatever you set (0s gives a gapless "
           "album).\n\nSo if a disc is just a few seconds over its capacity, "
           "you can lower this value to reclaim that time and squeeze the last "
           "song in. Each gap you trim frees roughly that many seconds times "
           "the number of tracks.")
            .arg(redbook::DEFAULT_GAP_SECONDS));
}

// -- Capacity meter ---------------------------------------------------------------

void MainWindow::updateCapacity()
{
    const int sizeIndex = qMax(0, m_discSizeCombo->currentIndex());
    const qint64 maxFrames =
        qint64(DISC_SIZES[sizeIndex].minutes) * 60 * redbook::FRAME_RATE;
    const qint64 usedFrames = totalProgramFrames(
        m_tracks, m_pregapSpin->value(), m_gapSpin->value());

    m_capacityBar->setMaximum(int(maxFrames));
    m_capacityBar->setValue(int(qMin(usedFrames, maxFrames)));

    const double usedSeconds = usedFrames / double(redbook::FRAME_RATE);
    const double maxSeconds = maxFrames / double(redbook::FRAME_RATE);
    const double pct = maxFrames ? usedFrames * 100.0 / maxFrames : 0.0;
    const bool over = usedFrames > maxFrames;

    QString text = QStringLiteral("%1 / %2 (%3%)")
                       .arg(redbook::secondsToMmss(usedSeconds),
                            redbook::secondsToMmss(maxSeconds),
                            QString::number(pct, 'f', 0));
    if (over) {
        const double overflow =
            (usedFrames - maxFrames) / double(redbook::FRAME_RATE);
        text += tr(" — over by %1").arg(redbook::secondsToMmss(overflow));
    }
    m_capacityLabel->setText(text);

    const QString color = over ? QStringLiteral("#cc3333")
                          : pct >= 90.0 ? QStringLiteral("#cc8800")
                                        : QStringLiteral("#3a7ecc");
    m_capacityBar->setStyleSheet(
        QStringLiteral("QProgressBar { border: 1px solid palette(mid); "
                       "border-radius: 4px; background: palette(base); } "
                       "QProgressBar::chunk { border-radius: 3px; "
                       "background-color: %1; }")
            .arg(color));

    m_statsLabel->setText(tr("%n track(s) · %1", nullptr, int(m_tracks.size()))
                              .arg(redbook::secondsToMmss(usedSeconds)));

    syncPlayerProgram();
}

// Hand the current track order and gap to the preview player. Called from
// updateCapacity, the one place every audio-relevant change funnels through
// (add/remove/reorder/re-import a track, or edit a gap). Resets playback, so
// title/performer edits — which don't reach here — don't interrupt a preview.
void MainWindow::syncPlayerProgram()
{
    if (!m_player)
        return;
    m_player->setProgram(m_tracks, m_gapSpin->value());
    const bool has = !m_tracks.isEmpty();
    m_playPauseBtn->setEnabled(has);
    m_prevBtn->setEnabled(has);
    m_nextBtn->setEnabled(has);
    m_seekSlider->setEnabled(has);
}

// -- Project save/load ---------------------------------------------------------------

QJsonObject MainWindow::gatherProjectJson() const
{
    QJsonObject o;
    o[QStringLiteral("album_title")] = m_albumTitleEdit->text();
    o[QStringLiteral("album_performer")] = m_albumPerformerEdit->text();
    o[QStringLiteral("album_songwriter")] = m_albumSongwriter;
    o[QStringLiteral("album_composer")] = m_albumComposer;
    o[QStringLiteral("album_arranger")] = m_albumArranger;
    o[QStringLiteral("album_message")] = m_albumMessage;
    o[QStringLiteral("album_disc_id")] = m_albumDiscId;
    o[QStringLiteral("album_genre")] = m_albumGenre;
    o[QStringLiteral("album_year")] = m_albumYear;
    o[QStringLiteral("album_catalog")] = m_albumCatalog;
    o[QStringLiteral("disc_size")] = m_discSizeCombo->currentText();
    o[QStringLiteral("pregap_seconds")] = m_pregapSpin->value();
    o[QStringLiteral("gap_seconds")] = m_gapSpin->value();
    QJsonArray tracks;
    for (const Track &t : m_tracks)
        tracks.append(t.toJson());
    o[QStringLiteral("tracks")] = tracks;
    return o;
}

void MainWindow::newProject()
{
    if (m_dirty && !confirmDiscard(tr("start a new project")))
        return;
    m_albumTitleEdit->clear();
    m_albumPerformerEdit->clear();
    m_albumSongwriter.clear();
    m_albumComposer.clear();
    m_albumArranger.clear();
    m_albumMessage.clear();
    m_albumDiscId.clear();
    m_albumGenre.clear();
    m_albumYear.clear();
    m_albumCatalog.clear();
    m_pregapSpin->setValue(redbook::DEFAULT_PREGAP_SECONDS);
    m_gapSpin->setValue(redbook::DEFAULT_GAP_SECONDS);
    updatePregapWarning();
    m_tracks.clear();
    m_projectPath.clear();
    refreshTable();
    setClean();
    statusBar()->showMessage(tr("New project."), 4000);
}

bool MainWindow::confirmDiscard(const QString &action)
{
    const auto reply = QMessageBox::question(
        this, tr("Discard current project?"),
        tr("The current project has %n track(s). Discard it and %1?", nullptr,
           int(m_tracks.size()))
            .arg(action),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return reply == QMessageBox::Yes;
}

void MainWindow::writeProject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("Failed to save project"),
                              file.errorString());
        return;
    }
    file.write(QJsonDocument(gatherProjectJson())
                   .toJson(QJsonDocument::Indented));
    file.close();
    m_projectPath = path;
    addRecentFile(path);
    setClean();
    statusBar()->showMessage(tr("Saved to %1").arg(path), 5000);
}

void MainWindow::saveProject()
{
    // Save to the current project path, prompting only if there isn't one.
    if (!m_projectPath.isEmpty())
        writeProject(m_projectPath);
    else
        saveProjectAs();
}

void MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project As"),
        m_projectPath.isEmpty() ? QStringLiteral("project.bincue.json")
                                : m_projectPath,
        tr("BinCue Project (*.bincue.json)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".json")))
        path += QStringLiteral(".bincue.json");
    writeProject(path);
}

void MainWindow::openProject(QString path)
{
    if (path.isEmpty()) {
        path = QFileDialog::getOpenFileName(
            this, tr("Open Project"), QString(),
            tr("BinCue Project (*.bincue.json);;All files (*)"));
        if (path.isEmpty())
            return;
    }
    if (m_dirty
        && !confirmDiscard(
            tr("open %1").arg(QFileInfo(path).fileName())))
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Failed to open project"),
                              file.errorString());
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        QMessageBox::critical(this, tr("Failed to open project"),
                              parseError.errorString());
        return;
    }
    const QJsonObject data = doc.object();

    m_albumTitleEdit->setText(
        data.value(QStringLiteral("album_title")).toString());
    m_albumPerformerEdit->setText(
        data.value(QStringLiteral("album_performer")).toString());
    m_albumSongwriter =
        data.value(QStringLiteral("album_songwriter")).toString();
    m_albumComposer = data.value(QStringLiteral("album_composer")).toString();
    m_albumArranger = data.value(QStringLiteral("album_arranger")).toString();
    m_albumMessage = data.value(QStringLiteral("album_message")).toString();
    m_albumDiscId = data.value(QStringLiteral("album_disc_id")).toString();
    m_albumGenre = data.value(QStringLiteral("album_genre")).toString();
    m_albumYear = data.value(QStringLiteral("album_year")).toString();
    m_albumCatalog = data.value(QStringLiteral("album_catalog")).toString();
    const int sizeIndex = m_discSizeCombo->findText(
        data.value(QStringLiteral("disc_size")).toString());
    if (sizeIndex >= 0)
        m_discSizeCombo->setCurrentIndex(sizeIndex);
    const double pregap = data.value(QStringLiteral("pregap_seconds"))
                              .toDouble(redbook::DEFAULT_PREGAP_SECONDS);
    m_pregapSpin->setValue(pregap);
    m_gapSpin->setValue(data.value(QStringLiteral("gap_seconds"))
                            .toDouble(redbook::DEFAULT_GAP_SECONDS));
    updatePregapWarning();

    QStringList missing;
    m_tracks.clear();
    for (const QJsonValue &v :
         data.value(QStringLiteral("tracks")).toArray()) {
        const Track track = Track::fromJson(v.toObject());
        if (!QFileInfo(track.sourcePath).isFile())
            missing.append(track.sourcePath);
        m_tracks.append(track);
    }

    m_projectPath = path;
    addRecentFile(path);
    refreshTable();
    setClean();

    if (!missing.isEmpty())
        QMessageBox::warning(
            this, tr("Some source files are missing"),
            tr("These source files could not be found on disk:\n\n%1")
                .arg(missing.join(QLatin1Char('\n'))));
}

// -- CD label --------------------------------------------------------------------------

void MainWindow::createLabel()
{
    if (m_tracks.isEmpty()) {
        QMessageBox::warning(this, tr("Nothing to label"),
                             tr("Add some tracks first."));
        return;
    }
    const QString binary = cdlabelBinary();
    if (binary.isEmpty()) {
        QMessageBox::critical(
            this, tr("Label editor not found"),
            tr("The CD label editor (cdlabel) was not found.\n\n"
               "It is built by this project:\n"
               "  cmake -B build\n"
               "  cmake --build build\n\n"
               "Looked next to this executable and on PATH; the CDLABEL_BIN "
               "environment variable overrides the location."));
        return;
    }

    // Hand the project over as JSON (the same shape as a saved project); the
    // editor pulls the titles and cover art from it. Which track names and
    // covers actually appear on the label is chosen in cdlabel's own panel, so
    // this project just carries the raw content.
    auto *projectFile = new QTemporaryFile(
        QDir::temp().filePath(QStringLiteral("bincue_label_XXXXXX.json")),
        this);
    if (!projectFile->open()) {
        QMessageBox::critical(this, tr("Could not launch label editor"),
                              projectFile->errorString());
        delete projectFile;
        return;
    }
    projectFile->write(
        QJsonDocument(gatherProjectJson()).toJson(QJsonDocument::Compact));
    projectFile->close();

    QString defaultName = m_albumTitleEdit->text().trimmed();
    if (defaultName.isEmpty())
        defaultName = QStringLiteral("cd_label");

    // When this project is saved, point the editor at the label project that
    // lives beside it (same path, ".cdlabel.json" instead of ".bincue.json").
    // The editor loads it if present and syncs its per-track choices to the
    // current tracks, but only rewrites it when the user saves in the editor.
    QStringList args{QStringLiteral("--project"), projectFile->fileName(),
                     QStringLiteral("--name"), defaultName};
    if (!m_projectPath.isEmpty()) {
        QString artPath = m_projectPath;
        if (artPath.endsWith(QStringLiteral(".bincue.json"),
                             Qt::CaseInsensitive))
            artPath.chop(QStringLiteral(".bincue.json").size());
        else if (artPath.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
            artPath.chop(QStringLiteral(".json").size());
        artPath += QStringLiteral(".cdlabel.json");
        args << QStringLiteral("--art-project") << artPath;
    }

    // A parented QProcess whose signals land on the GUI thread: the editor is
    // reaped and the temp file dropped when it exits, with no watcher thread
    // to crash the app when the editor window closes.
    auto *process = new QProcess(this);
    projectFile->setParent(process);  // auto-removed when the process object dies
    process->setProgram(binary);
    process->setArguments(args);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
                if (error != QProcess::FailedToStart)
                    return;
                QMessageBox::critical(this,
                                      tr("Could not launch label editor"),
                                      process->errorString());
                process->deleteLater();
            });
    process->start();
}

// -- About ------------------------------------------------------------------------------

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About BinCue Studio"),
        tr("<h3>BinCue Studio %1</h3>"
           "<p>An audio CD authoring tool. Assemble a track list, edit "
           "CD-Text metadata, and export a Red Book-compliant disc image "
           "(BIN + CUE) — or burn it straight to disc, locally or over "
           "SSH.</p>"
           "<p>Printable disc labels are designed in the companion "
           "<b>cdlabel</b> editor (<i>Create Label…</i>).</p>"
           "<p>Audio decoding by <b>FFmpeg</b>, burning by <b>cdrdao</b>"
#ifdef HAVE_TAGLIB
           ", tag import by <b>TagLib</b>"
#endif
           ".</p>")
            .arg(QApplication::applicationVersion()));
}

// -- Window close -------------------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_worker && m_worker->isRunning()) {
        QMessageBox::information(
            this, tr("Export in progress"),
            tr("An export is still running. Wait for it to finish before "
               "closing."));
        event->ignore();
        return;
    }
    if (!m_dirty) {
        event->accept();
        return;
    }
    const auto reply = QMessageBox::question(
        this, tr("Unsaved changes"),
        tr("The current project has unsaved changes. Save before closing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (reply == QMessageBox::Save) {
        saveProject();
        // saveProject() falls back to a Save-As dialog when there's no path
        // yet; if that was cancelled, the project is still dirty and we must
        // not close.
        if (m_dirty)
            event->ignore();
        else
            event->accept();
    } else if (reply == QMessageBox::Discard) {
        event->accept();
    } else {
        event->ignore();
    }
}

// -- Export ----------------------------------------------------------------------------------

void MainWindow::setExportEnabled(bool enabled)
{
    m_exportAct->setEnabled(enabled);
    m_exportBtn->setEnabled(enabled);
}

void MainWindow::exportProject()
{
    if (m_tracks.isEmpty()) {
        QMessageBox::warning(this, tr("Nothing to export"),
                             tr("Add some tracks first."));
        return;
    }
    if (m_worker)
        return;  // an export is already running

    // Settle the CD-Text first — the moment the user asks to export — so a
    // blocking "disc info incomplete" warning or the track-fill prompt comes up
    // before the folder picker, not after. Cancelling here abandons the export.
    ExportWorker::Params params = gatherMetadataParams();
    const CdTextChoice choice = ensureCdTextComplete(params, this);
    if (!choice.proceed)
        return;
    if (choice.applyToProject)
        applyCdTextToProject(params);

    const QString outDir = QFileDialog::getExistingDirectory(
        this, tr("Choose output folder"));
    if (outDir.isEmpty())
        return;

    QString baseName = m_albumTitleEdit->text().trimmed();
    static const QRegularExpression forbidden(
        QStringLiteral("[\\\\/:*?\"<>|]"));
    baseName.remove(forbidden);
    if (baseName.isEmpty())
        baseName = QStringLiteral("album");

    params.outDir = outDir;
    params.baseName = baseName;
    params.pregapSeconds = m_pregapSpin->value();
    params.gapSeconds = m_gapSpin->value();
    params.writeToc = true;  // always emit a cdrdao .toc alongside the BIN + CUE

    setExportEnabled(false);
    m_worker = new ExportWorker(std::move(params), this);
    connect(m_worker, &ExportWorker::progress, this,
            [this](int current, int total, const QString &message) {
                statusBar()->showMessage(QStringLiteral("[%1/%2] %3")
                                             .arg(current)
                                             .arg(total)
                                             .arg(message));
            });
    connect(m_worker, &ExportWorker::finishedOk, this,
            &MainWindow::onExportDone);
    connect(m_worker, &ExportWorker::failed, this,
            &MainWindow::onExportFailed);
    connect(m_worker, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker->start();
}

void MainWindow::onExportDone(const QString &binPath, const QString &cuePath,
                              const QString &tocPath)
{
    m_worker = nullptr;
    setExportEnabled(true);
    statusBar()->showMessage(tr("Export complete."));
    QString created = tr("Created:\n%1\n%2").arg(binPath, cuePath);
    if (!tocPath.isEmpty())
        created += QStringLiteral("\n%1").arg(tocPath);
    QMessageBox::information(
        this, tr("Export complete"),
        tr("%1\n\nCopy the files together to the other PC and "
           "open the .cue to burn.")
            .arg(created));
}

// -- Burn --------------------------------------------------------------------

void MainWindow::burnProject()
{
    if (m_tracks.isEmpty()) {
        QMessageBox::warning(this, tr("Nothing to burn"),
                             tr("Add some tracks first."));
        return;
    }
    if (m_worker) {
        QMessageBox::information(
            this, tr("Export in progress"),
            tr("Wait for the current export to finish before burning."));
        return;
    }

    // Settle the CD-Text first — the moment the user asks to burn — so a
    // blocking "disc info incomplete" warning or the track-fill prompt comes up
    // before the burn-setup dialog, not after. Cancelling abandons the burn.
    ExportWorker::Params params = gatherMetadataParams();
    const CdTextChoice choice = ensureCdTextComplete(params, this);
    if (!choice.proceed)
        return;
    if (choice.applyToProject)
        applyCdTextToProject(params);

    BurnSetupDialog setup(this);
    if (setup.exec() != QDialog::Accepted)
        return;
    std::unique_ptr<hostkit::HostSession> session = setup.takeSession();
    if (!session)
        return;  // Ok is disabled without a session, but be defensive

    const QStringList options = setup.userOptions();
    const bool simulate = options.contains(QStringLiteral("--simulate"));

    QMessageBox confirm(this);
    confirm.setIcon(simulate ? QMessageBox::Question : QMessageBox::Warning);
    confirm.setWindowTitle(simulate ? tr("Start test run?")
                                    : tr("Start burning?"));
    confirm.setText(simulate
        ? tr("Run a simulated burn to %1 on %2?")
              .arg(setup.device(), session->label())
        : tr("Burn %1 tracks to the disc in %2 on %3?")
              .arg(m_tracks.size())
              .arg(setup.device(), session->label()));
    confirm.setInformativeText(simulate
        ? tr("No data is written — the laser stays off.")
        : tr("This writes to the disc and cannot be undone. Make sure the "
             "correct blank disc is loaded."));
    QPushButton *goBtn = confirm.addButton(
        simulate ? tr("Start Test Run") : tr("Burn"),
        QMessageBox::AcceptRole);
    confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(qobject_cast<QPushButton *>(
        confirm.button(QMessageBox::Cancel)));
    confirm.exec();
    if (confirm.clickedButton() != goBtn)
        return;

    params.pregapSeconds = m_pregapSpin->value();
    params.gapSeconds = m_gapSpin->value();
    // outDir, baseName and writeToc are set by BurnJob.

    auto *job = new BurnJob(std::move(params), std::move(session),
                            setup.device(), options);
    auto *dialog = new BurnProgressDialog(job, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::onExportFailed(const QString &message)
{
    m_worker = nullptr;
    setExportEnabled(true);
    statusBar()->showMessage(tr("Export failed."));
    QMessageBox::critical(this, tr("Export failed"), message);
}
