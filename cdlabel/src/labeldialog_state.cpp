// Dialog state handling: config <-> controls sync, presets, the manual cover
// order list, the rich title-override editor and file export.

#include "labeldialog.h"

#include "colorbutton.h"
#include "discspec.h"
#include "previewwidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QSlider>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>

#include <cmath>

namespace {

// On-screen point size that means 1.0× in the title-override editor.
constexpr double TITLE_EDIT_BASE_PT = 12.0;

const QRegularExpression &lineSizeRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*\[\s*(\d*\.?\d+)\s*\]\s?(.*)$)"));
    return re;
}

void setComboData(QComboBox *combo, const QVariant &value)
{
    const int idx = combo->findData(value);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

bool nearlyEqual(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

} // namespace

void LabelDialog::applyConfig(const LabelConfig &cfg)
{
    m_cfg = cfg;
    if (m_covers.isEmpty())
        m_cfg.coversEnabled = false;
    loadBgFromConfig();
    syncControlsFromConfig();
}

// ---- Config -> controls -------------------------------------------------------

void LabelDialog::syncControlsFromConfig()
{
    m_loading = true;
    const LabelConfig &c = m_cfg;

    m_discMm->setValue(c.discMm);
    m_holeMm->setValue(c.holeMm);
    m_outerMm->setValue(c.printableOuterMm);
    m_innerMm->setValue(c.printableInnerMm);

    setComboData(m_titleLayout, c.titleLayout);
    m_titleBand->setValue(int(std::lround(c.titleBand * 100)));
    m_titleBandEdge->setChecked(c.titleBandEdge);
    m_titlePad->setValue(int(std::lround(c.titlePad * 100)));
    m_titleBold->setChecked(c.titleBold);
    m_titleItalic->setChecked(c.titleItalic);
    m_titleUnderline->setChecked(c.titleUnderline);
    if (!c.titleFont.isEmpty())
        m_titleFont->setCurrentFont(QFont(c.titleFont));
    m_titleSize->setValue(int(std::lround(c.titleSize * 100)));
    m_titleColor->setColor(c.titleColor);
    m_titleOutline->setChecked(c.titleOutline);
    m_titleOutlineColor->setColor(c.titleOutlineColor);
    m_titleOutlineWidth->setValue(int(std::lround(c.titleOutlineWidth * 100)));
    m_titleOffsetX->setValue(int(std::lround(c.titleOffsetX * 100)));
    m_titleOffsetY->setValue(int(std::lround(c.titleOffsetY * 100)));
    if (serializeTitleDoc() != c.titleOverride)
        loadTitleDoc(c.titleOverride);

    setComboData(m_trackLayout, c.trackLayout);
    m_trackBand->setValue(int(std::lround(c.trackBand * 100)));
    m_trackPad->setValue(int(std::lround(c.trackPad * 100)));
    m_trackNumbers->setChecked(c.trackNumbers);
    m_trackUnderline->setChecked(c.trackUnderline);
    m_trackBold->setChecked(c.trackBold);
    m_trackItalic->setChecked(c.trackItalic);
    if (!c.trackFont.isEmpty())
        m_trackFont->setCurrentFont(QFont(c.trackFont));
    m_trackSize->setValue(int(std::lround(c.trackSize * 100)));
    m_trackOffset->setValue(int(std::lround(c.trackOffset * 100)));
    m_trackSpacing->setValue(int(std::lround(c.trackSpacing * 100)));
    m_trackColor->setColor(c.trackColor);
    m_trackOutline->setChecked(c.trackOutline);
    m_trackOutlineColor->setColor(c.trackOutlineColor);
    m_trackOutlineWidth->setValue(int(std::lround(c.trackOutlineWidth * 100)));

    m_plateCheck->setChecked(c.plateEnabled);
    m_plateColor->setColor(c.plateColor);
    m_plateAlpha->setValue(c.plateAlpha);
    m_plateRadius->setValue(int(std::lround(c.plateRadius * 100)));
    m_platePad->setValue(int(std::lround(c.platePad * 100)));
    m_plateOutlineCheck->setChecked(c.plateOutline);
    m_plateOutlineColor->setColor(c.plateOutlineColor);

    m_panelTitle->setChecked(c.panelTitle);
    m_panelTracks->setChecked(c.panelTracks);
    setComboData(m_panelMode, c.panelMode);
    m_panelBlur->setValue(int(std::lround(c.panelBlur)));
    m_panelColor->setColor(c.panelColor);
    m_panelTint->setColor(c.panelTint);
    m_panelTintStrength->setValue(c.panelTintStrength);
    m_panelFade->setValue(c.panelFade);

    m_coversCheck->setChecked(c.coversEnabled && !m_covers.isEmpty());
    setComboData(m_coverBg, c.coverBg);
    m_coverFade->setValue(c.coverFade);
    m_coverFadeColor->setColor(c.coverFadeColor);
    m_coverDesat->setValue(int(std::lround(c.coverDesat * 100)));
    m_coverScale->setValue(int(std::lround(c.coverScale * 100)));
    m_coverDensity->setValue(
        qBound(0, int(std::lround((0.8 - c.coverOverlap) / 0.5 * 100)), 100));
    m_coverJitter->setValue(int(std::lround(c.coverJitter)));
    m_coverBlur->setValue(int(std::lround(c.coverBlur)));
    m_coverTint->setColor(c.coverTint);
    m_coverTintStrength->setValue(c.coverTintStrength);
    setComboData(m_coverFeature, c.coverFeature);
    m_ringDepth->setValue(int(std::lround(c.ringDepth * 100)));
    m_ringTitleGap->setValue(int(std::lround(c.ringTitleGap)));
    m_featureScale->setValue(int(std::lround(c.featureScale * 100)));
    m_featureTilt->setValue(int(std::lround(c.featureTilt)));
    m_scatterAvoid->setChecked(c.scatterAvoidText);
    m_coverFrame->setChecked(c.coverFrame);
    setComboData(m_orderMode, c.coverSequence);
    rebuildOrderList();

    const bool hasBg = !m_bgImage.isNull();
    m_bgImageCheck->setEnabled(hasBg);
    m_bgImageCheck->setChecked(c.bgImageEnabled && hasBg);
    setComboData(m_bgFit, c.bgImageFit);
    m_bgFade->setValue(c.bgImageFade);
    m_bgDesat->setValue(int(std::lround(c.bgImageDesat * 100)));
    m_bgBlur->setValue(int(std::lround(c.bgImageBlur)));
    updateBgImageLabel();

    m_backdropCheck->setChecked(c.backdropEnabled);
    m_backdropColor->setColor(c.backdropColor);
    m_backdropGradient->setChecked(c.backdropGradient);
    m_backdropColor2->setColor(c.backdropColor2);
    m_backdropGradShape->setCurrentIndex(c.backdropGradientRadial ? 1 : 0);

    m_bandCheck->setChecked(c.bandEnabled);
    setComboData(m_bandStyle, c.bandStyle);
    m_bandColor->setColor(c.bandColor);
    m_bandAlpha->setValue(c.bandAlpha);
    m_bandHeight->setValue(int(std::lround(c.bandHeight * 100)));
    m_bandFeather->setValue(int(std::lround(c.bandFeather * 100)));

    m_waveformCheck->setChecked(c.waveformEnabled);
    m_waveformColor->setColor(c.waveformColor);
    m_waveformAlpha->setValue(c.waveformAlpha);
    m_waveformRadius->setValue(int(std::lround(c.waveformRadius * 100)));
    m_waveformAmp->setValue(int(std::lround(c.waveformAmplitude * 100)));
    m_waveformBars->setValue(c.waveformBars);

    setComboData(m_hubMode, c.hubMode);
    m_hubColor->setColor(c.hubColor);
    m_hubGradient->setChecked(c.hubGradient);
    m_hubColor2->setColor(c.hubColor2);
    m_hubGradShape->setCurrentIndex(c.hubGradientRadial ? 1 : 0);

    m_hubRingCheck->setChecked(c.hubRingEnabled);
    m_hubRingColor->setColor(c.hubRingColor);
    m_hubRingWidth->setValue(int(std::lround(c.hubRingWidth * 100)));
    m_hubRingShine->setValue(c.hubRingShine);

    m_bleedCheck->setChecked(c.bleedEdge);

    updateTitleRows();
    updateTrackRows();
    updateCoverRows();
    updatePanelRows();
    updateHubRows();

    m_loading = false;
    m_preview->setConfig(m_cfg);
    updatePresetLabel();
    updateDiscPresetLabel();
}

void LabelDialog::updatePresetLabel()
{
    // Reflect whether the live config still matches a named built-in. Disc
    // geometry is deliberately ignored: presets are looks, not media.
    QString match = tr("Custom");
    for (const auto &preset : builtinPresets()) {
        LabelConfig normalised = preset.second;
        normalised.discMm = m_cfg.discMm;
        normalised.holeMm = m_cfg.holeMm;
        normalised.printableOuterMm = m_cfg.printableOuterMm;
        normalised.printableInnerMm = m_cfg.printableInnerMm;
        if (normalised == m_cfg) {
            match = preset.first;
            break;
        }
    }
    const int idx = m_presetCombo->findText(match);
    if (idx >= 0 && idx != m_presetCombo->currentIndex())
        m_presetCombo->setCurrentIndex(idx);
}

void LabelDialog::updateDiscPresetLabel()
{
    int match = m_discPresetCombo->count() - 1;   // "Custom"
    const auto &presets = mediaPresets();
    for (int i = 0; i < presets.size(); ++i) {
        if (nearlyEqual(presets[i].discMm, m_cfg.discMm)
            && nearlyEqual(presets[i].holeMm, m_cfg.holeMm)
            && nearlyEqual(presets[i].outerMm, m_cfg.printableOuterMm)
            && nearlyEqual(presets[i].innerMm, m_cfg.printableInnerMm)) {
            match = i;
            break;
        }
    }
    if (match != m_discPresetCombo->currentIndex())
        m_discPresetCombo->setCurrentIndex(match);
}

// ---- Row visibility -----------------------------------------------------------

void LabelDialog::updateTitleRows()
{
    const bool straight = m_cfg.titleLayout == TITLE_STRAIGHT;
    setRowVisible(m_titleForm, m_titleBand, straight);
    setRowVisible(m_titleForm, m_titleBandEdge, straight);
    setRowVisible(m_titleForm, m_titlePad, straight);
}

void LabelDialog::updateTrackRows()
{
    const bool table = m_cfg.trackLayout == TRACKS_TABLE;
    setRowVisible(m_tracksForm, m_trackBand, table);
    setRowVisible(m_tracksForm, m_trackPad, table);
    setRowVisible(m_tracksForm, m_trackOffset,
                  m_cfg.trackLayout == TRACKS_ARC);
    setRowVisible(m_tracksForm, m_trackSpacing,
                  m_cfg.trackLayout == TRACKS_COLUMNS);
}

void LabelDialog::updateCoverRows()
{
    const bool any = !m_covers.isEmpty();
    const bool mosaic = m_cfg.coverBg != COVER_BG_NONE;
    const bool spiral = m_cfg.coverBg == COVER_BG_SPIRAL;
    const bool ring = m_cfg.coverFeature == COVER_FEAT_RING;
    const bool scatter = m_cfg.coverFeature == COVER_FEAT_SCATTER;
    const bool feature = ring || scatter;

    for (QWidget *w : std::initializer_list<QWidget *>{
             m_coverBg, m_coverFeature, m_orderMode, m_orderList})
        w->setEnabled(any);

    setRowVisible(m_coversForm, m_coverFade, mosaic);
    setRowVisible(m_coversForm, m_coverFadeColor, mosaic);
    setRowVisible(m_coversForm, m_coverDesat, mosaic);
    setRowVisible(m_coversForm, m_coverScale, mosaic);
    setRowVisible(m_coversForm, m_coverDensity, spiral);
    setRowVisible(m_coversForm, m_coverJitter, spiral);
    setRowVisible(m_coversForm, m_coverBlur, mosaic);
    setRowVisible(m_coversForm, m_coverTint, mosaic);
    setRowVisible(m_coversForm, m_coverTintStrength, mosaic);
    setRowVisible(m_coversForm, m_ringDepth, ring);
    setRowVisible(m_coversForm, m_ringTitleGap, ring);
    setRowVisible(m_coversForm, m_featureScale, feature);
    setRowVisible(m_coversForm, m_featureTilt, feature);
    setRowVisible(m_coversForm, m_scatterAvoid, scatter);
    setRowVisible(m_coversForm, m_coverFrame, mosaic || feature);
}

void LabelDialog::updatePanelRows()
{
    const bool blur = m_cfg.panelMode == QStringLiteral("blur");
    setRowVisible(m_panelForm, m_panelBlur, blur);
    setRowVisible(m_panelForm, m_panelColor, !blur);
}

void LabelDialog::updateHubRows()
{
    const bool fill = m_cfg.hubMode == QStringLiteral("fill");
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_hubColor, m_hubGradient, m_hubColor2, m_hubGradShape})
        w->setEnabled(fill);
}

// ---- Cover order list -----------------------------------------------------------

void LabelDialog::rebuildOrderList()
{
    m_orderList->blockSignals(true);
    m_orderList->model()->blockSignals(true);
    m_orderList->clear();

    // Included covers first, in their configured order; excluded (unticked)
    // covers follow so they can be re-enabled.
    QList<int> included;
    if (m_cfg.coverOrder.isEmpty()) {
        for (int i = 0; i < m_covers.size(); ++i)
            included.append(i);
    } else {
        for (int i : m_cfg.coverOrder)
            if (i >= 0 && i < m_covers.size() && !included.contains(i))
                included.append(i);
    }
    QList<int> rows = included;
    for (int i = 0; i < m_covers.size(); ++i)
        if (!included.contains(i))
            rows.append(i);

    for (int idx : rows) {
        auto *item = new QListWidgetItem(tr("Cover %1").arg(idx + 1));
        item->setData(Qt::UserRole, idx);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                       | Qt::ItemIsDragEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(included.contains(idx) ? Qt::Checked
                                                   : Qt::Unchecked);
        item->setIcon(QIcon(QPixmap::fromImage(
            m_covers[idx].scaled(28, 28, Qt::KeepAspectRatioByExpanding,
                                 Qt::SmoothTransformation))));
        m_orderList->addItem(item);
    }
    m_orderList->model()->blockSignals(false);
    m_orderList->blockSignals(false);
}

void LabelDialog::orderListChanged()
{
    if (m_loading)
        return;
    QList<int> order;
    for (int i = 0; i < m_orderList->count(); ++i) {
        const QListWidgetItem *item = m_orderList->item(i);
        if (item->checkState() == Qt::Checked)
            order.append(item->data(Qt::UserRole).toInt());
    }
    // The natural full order means "automatic" — store it as empty so presets
    // stay portable across albums.
    QList<int> natural;
    for (int i = 0; i < m_covers.size(); ++i)
        natural.append(i);
    m_cfg.coverOrder = (order == natural) ? QList<int>() : order;
    touch();
}

// ---- Rich title override editor ------------------------------------------------------
//
// The title-override box is a WYSIWYG editor: each line is shown at its own
// size. Per-line sizes ride on the document's character formats (a point size
// relative to TITLE_EDIT_BASE_PT), and the whole document is serialised to
// the config's "[mult]text" markup so the renderer and JSON presets keep
// using one plain string.

namespace {

double blockMultiplier(const QTextBlock &block)
{
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        if (frag.isValid()) {
            const double pt = frag.charFormat().fontPointSize();
            if (pt > 0)
                return pt / TITLE_EDIT_BASE_PT;
            break;
        }
    }
    return 1.0;
}

} // namespace

QString LabelDialog::serializeTitleDoc() const
{
    QStringList lines;
    for (QTextBlock block = m_titleOverride->document()->begin();
         block.isValid(); block = block.next()) {
        const QString text = block.text();
        const double mult = blockMultiplier(block);
        if (!text.trimmed().isEmpty() && std::abs(mult - 1.0) > 0.01)
            lines.append(QStringLiteral("[%1]%2")
                             .arg(QString::number(mult, 'g', 3), text));
        else
            lines.append(text);
    }
    return lines.join(QLatin1Char('\n'));
}

void LabelDialog::loadTitleDoc(const QString &markup)
{
    QTextEdit *edit = m_titleOverride;
    edit->blockSignals(true);
    edit->clear();
    QTextCursor cursor = edit->textCursor();
    const QStringList lines = markup.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        double mult = 1.0;
        QString text = lines[i];
        const QRegularExpressionMatch m = lineSizeRe().match(lines[i]);
        if (m.hasMatch()) {
            bool ok = false;
            const double v = m.captured(1).toDouble(&ok);
            mult = ok ? v : 1.0;
            text = m.captured(2);
        }
        if (i)
            cursor.insertBlock();
        QTextCharFormat fmt;
        fmt.setFontPointSize(TITLE_EDIT_BASE_PT * qMax(0.1, mult));
        cursor.insertText(text, fmt);
    }
    edit->blockSignals(false);
}

void LabelDialog::titleTextChanged()
{
    if (m_loading)
        return;
    m_cfg.titleOverride = serializeTitleDoc();
    touch();
}

void LabelDialog::syncTitleLineSize()
{
    if (m_loading)
        return;
    const QTextBlock block = m_titleOverride->textCursor().block();
    m_titleLineSize->blockSignals(true);
    m_titleLineSize->setValue(blockMultiplier(block));
    m_titleLineSize->blockSignals(false);
}

void LabelDialog::applyTitleLineSize(double mult)
{
    if (m_loading)
        return;
    QTextEdit *edit = m_titleOverride;
    QTextDocument *doc = edit->document();
    const QTextCursor cur = edit->textCursor();
    const int start = qMin(cur.selectionStart(), cur.selectionEnd());
    const int end = qMax(cur.selectionStart(), cur.selectionEnd());
    QTextBlock block = doc->findBlock(start);
    const QTextBlock last = doc->findBlock(end);
    QTextCharFormat fmt;
    fmt.setFontPointSize(TITLE_EDIT_BASE_PT * qMax(0.1, mult));
    edit->blockSignals(true);
    while (block.isValid()) {
        QTextCursor c(block);
        c.movePosition(QTextCursor::StartOfBlock);
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        c.mergeCharFormat(fmt);
        if (block == last)
            break;
        block = block.next();
    }
    // New text typed at the cursor keeps the size just applied.
    edit->setCurrentCharFormat(fmt);
    edit->blockSignals(false);
    m_cfg.titleOverride = serializeTitleDoc();
    touch();
}

// ---- Presets -----------------------------------------------------------------------

void LabelDialog::onPresetSelected(int index)
{
    const auto &presets = builtinPresets();
    if (index < 0 || index >= presets.size())
        return;   // "Custom" — leave the current config alone
    LabelConfig cfg = presets[index].second;
    // Presets are looks, not media: keep the current disc geometry.
    cfg.discMm = m_cfg.discMm;
    cfg.holeMm = m_cfg.holeMm;
    cfg.printableOuterMm = m_cfg.printableOuterMm;
    cfg.printableInnerMm = m_cfg.printableInnerMm;
    if (m_covers.isEmpty())
        cfg.coversEnabled = false;
    m_cfg = cfg;
    syncControlsFromConfig();
}

void LabelDialog::importPreset()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Preset"), QString(),
        tr("Label preset (*.json);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, tr("Import failed"), file.errorString());
        return;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::critical(this, tr("Import failed"), err.errorString());
        return;
    }
    QString formatErr;
    if (!LabelConfig::formatCompatible(doc.object(), formatErr)) {
        QMessageBox::critical(this, tr("Import failed"), formatErr);
        return;
    }
    applyConfig(LabelConfig::fromJson(doc.object()));
}

void LabelDialog::exportPreset()
{
    QString path = QFileDialog::getSaveFileName(
        this, tr("Export Preset"), QStringLiteral("cd_label_preset.json"),
        tr("Label preset (*.json)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Export failed"), file.errorString());
        return;
    }
    file.write(QJsonDocument(m_cfg.toJson()).toJson(QJsonDocument::Indented));
    QMessageBox::information(this, tr("Preset exported"),
                             tr("Saved to:\n%1").arg(path));
}

// ---- Background image ------------------------------------------------------------------

void LabelDialog::chooseBgImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose background image"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All files (*)"));
    if (path.isEmpty())
        return;
    QImage img(path);
    if (img.isNull()) {
        QMessageBox::critical(this, tr("Could not load image"),
                              tr("Not a readable image:\n%1").arg(path));
        return;
    }
    m_bgImage = img;
    m_preview->setBgImage(img);
    m_cfg.bgImagePath = path;
    m_cfg.bgImageEnabled = true;
    syncControlsFromConfig();
}

void LabelDialog::loadBgFromConfig()
{
    // Reload the background image named in the config (used on import).
    QImage img;
    if (!m_cfg.bgImagePath.isEmpty() && QFile::exists(m_cfg.bgImagePath))
        img = QImage(m_cfg.bgImagePath);
    m_bgImage = img;
    if (m_bgImage.isNull())
        m_cfg.bgImageEnabled = false;
    m_preview->setBgImage(m_bgImage);
}

void LabelDialog::updateBgImageLabel()
{
    const bool hasBg = !m_bgImage.isNull();
    if (hasBg) {
        const QString name = QFileInfo(m_cfg.bgImagePath).fileName();
        m_bgImageLabel->setText(name.isEmpty() ? tr("image loaded") : name);
        m_bgImageLabel->setStyleSheet(QString());
    } else {
        m_bgImageLabel->setText(tr("No image chosen"));
        m_bgImageLabel->setStyleSheet(QStringLiteral("color: #888;"));
    }
    m_bgFit->setEnabled(hasBg);
    for (QSlider *s : {m_bgFade, m_bgDesat, m_bgBlur})
        s->parentWidget()->setEnabled(hasBg);
}

// ---- Export ---------------------------------------------------------------------------------

void LabelDialog::saveLabel()
{
    QString safe = m_defaultName;
    safe.remove(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")));
    safe = safe.trimmed();
    if (safe.isEmpty())
        safe = QStringLiteral("cd_label");
    QString selected;
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save CD Label"), safe + QStringLiteral(".png"),
        tr("PNG image (*.png);;JPEG image (*.jpg)"), &selected);
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
        && !path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
        && !path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive))
        path += selected.contains(QStringLiteral("jpg"), Qt::CaseInsensitive)
                    ? QStringLiteral(".jpg")
                    : QStringLiteral(".png");

    const int side = int(std::lround(m_cfg.discMm / 25.4 * SAVE_DPI));
    RenderInput input;
    input.title = m_titleText;
    input.trackTitles = m_trackTitles;
    input.covers = m_covers;
    input.bgImage = m_bgImage;
    QImage image = renderLabelImage(input, m_cfg, side);

    if (path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive)) {
        // JPEG has no alpha — flatten transparency onto white.
        QImage flat(image.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, image);
        p.end();
        image = flat;
    }

    if (image.save(path)) {
        QMessageBox::information(
            this, tr("Label saved"),
            tr("Saved to:\n%1\n\n%2×%2 px — a %3 mm disc at %4 DPI.\n\n"
               "In your printer's CD-print tool, import this as the "
               "background and set the disc outer/inner diameters to match "
               "(%5 mm / %6 mm).")
                .arg(path)
                .arg(side)
                .arg(m_cfg.discMm)
                .arg(SAVE_DPI)
                .arg(m_cfg.printableOuterMm)
                .arg(m_cfg.printableInnerMm));
    } else {
        QMessageBox::critical(
            this, tr("Save failed"),
            tr("Could not write the image to:\n%1").arg(path));
    }
}
