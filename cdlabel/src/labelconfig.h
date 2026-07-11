// LabelConfig: every knob the sidebar exposes, one flat value struct.
//
// The label is a stack of independent, freely combinable layers — there are
// no exclusive "modes". The title and the track list each pick their own
// layout; the cover art splits into a washed background mosaic and crisp
// "feature" covers (ring / scatter); plates, panels, backdrop, overlay band,
// waveform, hub treatments and the disc geometry are all orthogonal.
//
// The field table below is the single source of truth: it generates the
// members, the JSON round-trip (snake_case keys, human-editable presets) and
// equality (used to tell whether the live config still matches a built-in
// preset). QList<int> is used for the manual cover order.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVector>

// Format tag stamped into every saved preset so the program can tell whether a
// file it's asked to load is a compatible BinCue Studio Label config. "BCSL" =
// BinCue Studio Label; the trailing "v<major>" is bumped only for a breaking
// change. Additive fields keep the same major — fromJson ignores unknown keys
// and defaults missing ones, so a v1 reader and a v1 writer stay compatible
// even as fields come and go. A file with no tag, or a different major, is
// rejected: only genuine BCSLv1 configs load.
inline const QString LABEL_FORMAT_KEY = QStringLiteral("format");
inline constexpr int LABEL_FORMAT_MAJOR = 1;
inline const QString LABEL_FORMAT_ID = QStringLiteral("BCSLv1");

// Text layouts.
inline const QString TITLE_ARC = QStringLiteral("arc");            // curved along the rim
inline const QString TITLE_STRAIGHT = QStringLiteral("straight");  // banner in a top band
inline const QString TRACKS_ARC = QStringLiteral("arc");           // concentric rings
inline const QString TRACKS_COLUMNS = QStringLiteral("columns");   // two rim-hugging columns
inline const QString TRACKS_TABLE = QStringLiteral("table");       // table in a bottom band

// Cover sub-layers.
inline const QString COVER_BG_NONE = QStringLiteral("none");
inline const QString COVER_BG_GRID = QStringLiteral("grid");
inline const QString COVER_BG_SPIRAL = QStringLiteral("spiral");
inline const QString COVER_FEAT_NONE = QStringLiteral("none");
inline const QString COVER_FEAT_RING = QStringLiteral("ring");
inline const QString COVER_FEAT_SCATTER = QStringLiteral("scatter");

// F(type, member, jsonKey, default)
#define LABEL_CONFIG_FIELDS(F)                                                 \
    /* ---- Disc geometry (fully parametric; see discspec.h presets) ---- */   \
    F(double, discMm, "disc_mm", 120.0)                                        \
    F(double, holeMm, "hole_mm", 15.0)                                         \
    F(double, printableOuterMm, "printable_outer_mm", 116.0)                   \
    F(double, printableInnerMm, "printable_inner_mm", 43.0)                    \
    /* ---- Title ----                                                         \
       "arc" curves the title along the top of the rim; "straight" sets it as  \
       a centred banner inside a band across the top of the disc. */           \
    F(QString, titleLayout, "title_layout", TITLE_ARC)                         \
    /* Straight only: band height (fraction of the diameter) + breathing-room  \
       multiplier (0 lets the auto-sized text grow to the edges, 2 = roomy). */\
    F(double, titleBand, "title_band", 0.28)                                   \
    F(double, titlePad, "title_pad", 1.0)                                      \
    /* Straight only: grow the band's panel up past its top so it reaches the  \
       disc edge (the bleed edge when a full bleed is on). Lets an offset-down  \
       title still back-fill the gap to the rim instead of floating. */        \
    F(bool, titleBandEdge, "title_band_edge", false)                           \
    F(bool, titleBold, "title_bold", true)                                     \
    F(bool, titleItalic, "title_italic", false)                                \
    F(bool, titleUnderline, "title_underline", true)                           \
    F(QString, titleFont, "title_font", QString())                             \
    F(double, titleSize, "title_size", 1.0)                                    \
    F(QString, titleColor, "title_color", "#000000")                           \
    F(bool, titleOutline, "title_outline", false)                              \
    F(QString, titleOutlineColor, "title_outline_color", "#ffffff")            \
    F(double, titleOutlineWidth, "title_outline_width", 0.08)                  \
    /* Nudges off the default spot (+x right, +y down, fractions of the outer  \
       radius). Arc layout: +x rotates around the rim, +y slides hub-ward. */  \
    F(double, titleOffsetX, "title_offset_x", 0.0)                             \
    F(double, titleOffsetY, "title_offset_y", 0.0)                             \
    /* Rich override: empty = album title; else one disc line per text line,   \
       an optional leading "[1.5]" scaling that line. */                       \
    F(QString, titleOverride, "title_override", QString())                     \
    /* ---- Tracks ---- */                                                     \
    F(QString, trackLayout, "track_layout", TRACKS_ARC)                        \
    /* Table only: band height (fraction of the diameter) + breathing room. */ \
    F(double, trackBand, "track_band", 0.34)                                   \
    F(double, trackPad, "track_pad", 1.0)                                      \
    F(bool, trackNumbers, "track_numbers", true)                               \
    F(bool, trackUnderline, "track_underline", true)                           \
    F(bool, trackBold, "track_bold", false)                                    \
    F(bool, trackItalic, "track_italic", false)                                \
    F(QString, trackFont, "track_font", QString())                             \
    F(double, trackSize, "track_size", 1.0)                                    \
    /* Arc: radial nudge of the track block (+ toward the hub). */             \
    F(double, trackOffset, "track_offset", 0.0)                                \
    /* Columns: line-spacing multiplier (clamped to stay on the disc). */      \
    F(double, trackSpacing, "track_spacing", 1.0)                              \
    F(QString, trackColor, "track_color", "#000000")                           \
    F(bool, trackOutline, "track_outline", false)                              \
    F(QString, trackOutlineColor, "track_outline_color", "#ffffff")            \
    F(double, trackOutlineWidth, "track_outline_width", 0.08)                  \
    /* ---- Text plates: a rounded pill behind each text run (straight rows    \
       get rounded rects, curved text an arc-shaped pill on its ring). ---- */ \
    F(bool, plateEnabled, "text_plate_enabled", false)                         \
    F(QString, plateColor, "text_plate_color", "#000000")                      \
    F(int, plateAlpha, "text_plate_alpha", 150)                                \
    F(double, plateRadius, "text_plate_radius", 0.5)                           \
    F(double, platePad, "text_plate_pad", 0.35)                                \
    F(bool, plateOutline, "text_plate_outline", false)                         \
    F(QString, plateOutlineColor, "text_plate_outline_color", "#ffffff")       \
    /* ---- Text panels: a treatment of the whole zone behind the title       \
       and/or the tracks (a band for straight layouts, a ring for curved      \
       ones). "blur" re-blurs the cover mosaic inside the zone; "solid" fills \
       it opaquely. Tint and fade wash over the zone in either mode. ---- */   \
    F(bool, panelTitle, "panel_title", false)                                  \
    F(bool, panelTracks, "panel_tracks", false)                                \
    F(QString, panelMode, "panel_mode", "blur")                                \
    F(double, panelBlur, "panel_blur", 9.0)                                    \
    F(QString, panelColor, "panel_color", "#0a0d16")                           \
    F(QString, panelTint, "panel_tint", "#000000")                             \
    F(int, panelTintStrength, "panel_tint_strength", 0)                        \
    F(int, panelFade, "panel_fade", 0)                                         \
    /* ---- Cover art ---- */                                                  \
    F(bool, coversEnabled, "covers_enabled", false)                            \
    /* Which covers to use, in what order: indices into the extracted list.    \
       Empty = all, in the order found. Edited by the dialog's order list. */  \
    F(QList<int>, coverOrder, "cover_order", QList<int>())                     \
    /* false: automatic anti-clumping spread of each cover's repeats.          \
       true: strict order — reading order (grid/scatter), along the arm        \
       (spiral), clockwise (ring). */                                          \
    F(bool, coverSequence, "cover_sequence", false)                            \
    /* Background mosaic: repeated covers behind everything, washed back. */   \
    F(QString, coverBg, "cover_bg", COVER_BG_GRID)                             \
    F(int, coverFade, "cover_fade", 190)                                       \
    F(QString, coverFadeColor, "cover_fade_color", "#ffffff")                  \
    F(double, coverDesat, "cover_desat", 0.45)                                 \
    F(double, coverScale, "cover_scale", 0.66)                                 \
    F(double, coverOverlap, "cover_overlap", 0.5)                              \
    F(double, coverBlur, "cover_blur", 0.0)                                    \
    F(QString, coverTint, "cover_tint", "#000000")                             \
    F(int, coverTintStrength, "cover_tint_strength", 0)                        \
    F(double, coverJitter, "cover_jitter", 16.0)                               \
    F(bool, coverFrame, "cover_frame", false)                                  \
    /* Feature covers: each distinct cover once, crisp and full-colour, on     \
       top of the background layers but under the text. */                     \
    F(QString, coverFeature, "cover_feature", COVER_FEAT_NONE)                 \
    F(double, featureScale, "feature_scale", 0.92)                             \
    F(double, featureTilt, "feature_tilt", 4.0)                                \
    /* Ring: outer-band depth (fraction of the printable annulus) + extra      \
       clearance (degrees) each side of the title. */                          \
    F(double, ringDepth, "ring_depth", 0.40)                                   \
    F(double, ringTitleGap, "ring_title_gap", 4.0)                             \
    /* Scatter: keep clear of band-shaped text zones (straight title / table   \
       tracks). */                                                             \
    F(bool, scatterAvoidText, "scatter_avoid_text", true)                      \
    /* ---- Custom background image (path remembered for presets). ---- */     \
    F(bool, bgImageEnabled, "bg_image_enabled", false)                         \
    F(QString, bgImagePath, "bg_image_path", QString())                        \
    F(QString, bgImageFit, "bg_image_fit", "cover")                            \
    F(int, bgImageFade, "bg_image_fade", 0)                                    \
    F(double, bgImageDesat, "bg_image_desat", 1.0)                             \
    F(double, bgImageBlur, "bg_image_blur", 0.0)                               \
    /* ---- Hub: "blank" leaves the inner area clear, "fill" paints it,        \
       "background" lets the background layers run in to the hole. ---- */     \
    F(QString, hubMode, "hub_mode", "blank")                                   \
    F(QString, hubColor, "hub_color", "#ffffff")                               \
    F(QString, hubColor2, "hub_color2", "#334155")                             \
    F(bool, hubGradient, "hub_gradient", false)                                \
    F(bool, hubGradientRadial, "hub_gradient_radial", true)                    \
    /* Full bleed: run the background layers past the printable outer ring     \
       to the disc edge, letting the printer clip. */                          \
    F(bool, bleedEdge, "bleed_edge", false)                                    \
    /* ---- Backdrop: solid / gradient fill under everything. ---- */          \
    F(bool, backdropEnabled, "backdrop_enabled", false)                        \
    F(QString, backdropColor, "backdrop_color", "#ffffff")                     \
    F(QString, backdropColor2, "backdrop_color2", "#cccccc")                   \
    F(bool, backdropGradient, "backdrop_gradient", false)                      \
    F(bool, backdropGradientRadial, "backdrop_gradient_radial", false)         \
    /* ---- Overlay band: a feathered translucent band over the background     \
       layers — a concentric ring or a straight horizontal strip. ---- */      \
    F(bool, bandEnabled, "band_enabled", false)                                \
    F(QString, bandStyle, "band_style", "ring")                                \
    F(QString, bandColor, "band_color", "#ffffff")                             \
    F(int, bandAlpha, "band_alpha", 200)                                       \
    F(double, bandHeight, "band_height", 0.5)                                  \
    F(double, bandFeather, "band_feather", 0.18)                               \
    /* ---- Metallic ring hugging the centre hole (wants a hub-printable       \
       disc, or the "background" hub mode, to show fully). ---- */             \
    F(bool, hubRingEnabled, "hub_ring_enabled", false)                         \
    F(QString, hubRingColor, "hub_ring_color", "#b9bcc4")                      \
    F(double, hubRingWidth, "hub_ring_width", 0.6)                             \
    F(int, hubRingShine, "hub_ring_shine", 110)                                \
    /* ---- Faint circular waveform behind the track list (seeded by the       \
       track names, so it's stable but album-specific). ---- */                \
    F(bool, waveformEnabled, "waveform_enabled", false)                        \
    F(QString, waveformColor, "waveform_color", "#ffffff")                     \
    F(int, waveformAlpha, "waveform_alpha", 40)                                \
    F(double, waveformRadius, "waveform_radius", 0.62)                         \
    F(double, waveformAmplitude, "waveform_amplitude", 0.10)                   \
    F(int, waveformBars, "waveform_bars", 140)

struct LabelConfig {
#define DECLARE_FIELD(type, member, key, def) type member = def;
    LABEL_CONFIG_FIELDS(DECLARE_FIELD)
#undef DECLARE_FIELD

    bool operator==(const LabelConfig &) const = default;

    QJsonObject toJson() const;
    static LabelConfig fromJson(const QJsonObject &obj);

    // True when `obj` is a config this build can load: it must carry a matching
    // major-version format tag. A missing tag or a different major fails with a
    // human-readable reason in `error`.
    static bool formatCompatible(const QJsonObject &obj, QString &error);
};

// Built-in presets, listed by the dialog's combo in this order.
const QVector<QPair<QString, LabelConfig>> &builtinPresets();
