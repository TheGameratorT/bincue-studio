#pragma once

#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QSettings>

#include "exportworker.h"
#include "track.h"

class PlaybackEngine;
class QAction;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QPushButton;
class QSlider;
class QTableWidget;
class QTableWidgetItem;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // construction
    void buildActions();
    void buildMenus();
    void buildToolBar();
    void buildUi();

    // state
    void updateTitle();
    void markDirty();
    void setClean();
    QStringList recentFiles() const;
    void addRecentFile(const QString &path);
    void clearRecentFiles();
    void rebuildRecentMenu();
    bool confirmDiscard(const QString &action);
    QJsonObject gatherProjectJson() const;
    void writeProject(const QString &path);
    void refreshTable();
    void updatePlayButtons();
    void setNowPlayingIdle();
    QList<int> selectedRows() const;
    void setExportEnabled(bool enabled);
    void syncPlayerProgram();

    // actions
    void newProject();
    void openProject(QString path = QString());
    void saveProject();
    void saveProjectAs();
    void exportProject();
    void burnProject();
    void addTracks();
    void removeSelected();
    void moveSelected(int direction);
    void openTrackDetails(int row);
    void openDiscDetails();
    void reimportTrack(int row);
    void fillDiscInfoFromTrack();
    // A Params snapshot of the current disc + track metadata (no output paths),
    // used both to gather for a burn/export and to feed the shared cdtext model
    // that drives the warning badges.
    ExportWorker::Params gatherMetadataParams() const;
    // Refresh every CD-Text ⚠ badge (the Title/Performer/Songwriter panel ones
    // and the Disc Details button) from the shared cdtext model, so they all
    // flag a partial pack the same way the completion prompt would.
    void updateCdTextWarnings();
    void showCdTextWarning();
    void createLabel();
    void showAbout();
    void onItemChanged(QTableWidgetItem *item);
    void updateCapacity();
    void updatePregapWarning();
    void showPregapWarning();
    void showGapInfo();
    void onExportDone(const QString &binPath, const QString &cuePath,
                      const QString &tocPath);
    void onExportFailed(const QString &message);

    QSettings m_settings;
    QList<Track> m_tracks;
    QString m_projectPath;
    ExportWorker *m_worker = nullptr;
    bool m_dirty = false;

    QAction *m_newAct = nullptr;
    QAction *m_openAct = nullptr;
    QAction *m_saveAct = nullptr;
    QAction *m_saveAsAct = nullptr;
    QAction *m_exportAct = nullptr;
    QAction *m_burnAct = nullptr;
    QAction *m_addAct = nullptr;
    QAction *m_removeAct = nullptr;
    QAction *m_upAct = nullptr;
    QAction *m_downAct = nullptr;
    QAction *m_labelAct = nullptr;
    QMenu *m_recentMenu = nullptr;

    QLineEdit *m_albumTitleEdit = nullptr;
    QLineEdit *m_albumPerformerEdit = nullptr;
    QLineEdit *m_albumSongwriterEdit = nullptr;
    QLineEdit *m_albumGenreEdit = nullptr;
    QLineEdit *m_albumYearEdit = nullptr;
    QLineEdit *m_albumCatalogEdit = nullptr;
    // Less-common disc CD-Text pack types, edited in the Disc details dialog
    // instead of on the panel (see DiscDetailsDialog).
    QString m_albumComposer;
    QString m_albumArranger;
    QString m_albumMessage;
    QString m_albumDiscId;
    QPushButton *m_discDetailsBtn = nullptr;
    // Error badges next to the disc CD-Text panel fields, shown when the pack is
    // partially used (see cdtext::needsAttention). The disc-details fields
    // (composer/arranger/message) roll their badges up into m_discDetailsBtn.
    QToolButton *m_titleErrBtn = nullptr;
    QToolButton *m_performerErrBtn = nullptr;
    QToolButton *m_songwriterErrBtn = nullptr;
    QComboBox *m_discSizeCombo = nullptr;
    QDoubleSpinBox *m_pregapSpin = nullptr;
    QDoubleSpinBox *m_gapSpin = nullptr;
    QToolButton *m_pregapWarnBtn = nullptr;
    QToolButton *m_gapInfoBtn = nullptr;
    QTableWidget *m_table = nullptr;
    QProgressBar *m_capacityBar = nullptr;
    QLabel *m_capacityLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QPushButton *m_exportBtn = nullptr;

    // Preview player: streams the assembled program (gaps and all) below the
    // table so you can hear the disc before burning it.
    PlaybackEngine *m_player = nullptr;
    QToolButton *m_playPauseBtn = nullptr;
    QToolButton *m_stopBtn = nullptr;
    QToolButton *m_prevBtn = nullptr;
    QToolButton *m_nextBtn = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_posLabel = nullptr;
    QLabel *m_totalLabel = nullptr;
    QLabel *m_nowPlayingLabel = nullptr;
    QList<QToolButton *> m_playButtons; // one per row, in ColPlay
    bool m_sliderHeld = false;
};
