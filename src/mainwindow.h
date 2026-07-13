#pragma once

#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QSettings>

#include "track.h"

class ExportWorker;
class QAction;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QPushButton;
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
    QList<int> selectedRows() const;
    void setExportEnabled(bool enabled);

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
    void reimportTrack(int row);
    void fillDiscInfoFromTrack();
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
};
