# cdlabel

The CD label editor for BinCue Studio — a Qt 6 / C++ application that renders a
circular label for inkjet-printable discs, with a live preview beside a
customisation sidebar. It runs standalone or is launched from BinCue Studio's
**Create Label…** button.

## The label model

The label is a **stack of independent, freely combinable layers** — there are no
exclusive "modes". The title and the track list each pick their own layout; the
cover art splits into a washed-back background mosaic and crisp feature covers;
and plates, panels, backdrop, overlay band, waveform, hub treatments and the disc
geometry are all orthogonal. Drawn back to front:

1. **Backdrop** — solid or gradient fill under everything.
2. **Background image** — your own image, fitted and optionally washed back.
3. **Cover mosaic** — every cover repeated (grid or spiral) and faded so text
   reads over it.
4. **Overlay band**, **waveform**, **hub** treatments — decoration over the art.
5. **Feature covers** — each distinct cover shown once, crisp and full-colour.
6. **Text panels** and **text plates** — readability treatments behind the text.
7. **Title** and **track list** — the text itself.

Everything round-trips through JSON presets (two are built in, **Poster** and
**Polaroid**).

## Build

Requires Qt 6 (Widgets) and CMake ≥ 3.21. TagLib ≥ 2.0 is optional but strongly
recommended — it enables embedded cover-art extraction from the audio files.

```sh
cmake -B build -G Ninja
cmake --build build
```

## Run

BinCue Studio's **Create Label…** button launches the editor automatically (it
looks for `cdlabel/build/cdlabel` next to the BinCue Studio binary, then `cdlabel`
on `PATH`; `CDLABEL_BIN` overrides). Standalone:

```sh
# Editor on its own (a demo album fills the preview):
./build/cdlabel [--preset preset.json]

# Editor on a real project (its title, tracks and cover art fill the label):
./build/cdlabel --project project.bincue.json [--preset preset.json]

# Headless render — no display server needed:
./build/cdlabel --project project.bincue.json --preset preset.json \
    --render out.png [--size 1400] [--matte]
```

### Command-line options

| Option | Meaning |
|---|---|
| `--project <path>` | A saved BinCue Studio project. `album_title`, `tracks[].title`, `tracks[].source_path` and `tracks[].include_cover` are read; tracks with `include_cover: false` still appear in the listing but contribute no cover art. |
| `--preset <path>` | A label preset JSON to load on start. |
| `--render <out.png>` | Render straight to this image file and exit (headless — no window). |
| `--size <px>` | Rendered image side in pixels. Without it, the export is the disc's physical size at 600 DPI. |
| `--matte` | Composite the transparent label onto a grey disc so the render reads like print. |
| `--name <name>` | Default file name offered in the editor's Save dialog. |

## Knob reference

The sidebar groups map to these settings. Every one is stored in the preset JSON
under the snake-case key shown; the field table in `src/labelconfig.h` is the
single source of truth and drives the JSON round-trip.

### Disc & printer geometry

The renderer is fully parametric — it works to whatever physical dimensions you
give it, so it can match any printer's tray spec. The **Media** dropdown pre-fills
these four numbers for common combinations (120 mm on the Epson XP-630 in safe or
maximum area, hub-printable discs, 80 mm mini CDs); pick **Custom** to type your
own.

| Knob | Key | Meaning |
|---|---|---|
| Disc size | `disc_mm` | Physical disc diameter — also the square canvas the label is rendered into. |
| Hole | `hole_mm` | Centre-hole diameter. |
| Print outer | `printable_outer_mm` | Outer edge of the printable area; artwork stays inside it (unless full bleed). |
| Print inner | `printable_inner_mm` | Inner edge of the printable area; artwork stays outside it. |

### Title

Curved along the rim (`arc`) or a straight banner in a top band (`straight`).

| Knob | Key | Meaning |
|---|---|---|
| Layout | `title_layout` | `arc` (curved) or `straight` (banner). |
| Band height | `title_band` | *Straight only.* Band height as a fraction of the diameter. |
| Padding | `title_pad` | *Straight only.* Breathing room around auto-sized text (0 = grow to the edges, 2 = roomy). |
| Fill band to edge | `title_band_edge` | *Straight only.* Extend the band's panel up to the disc edge so an offset-down title still backs to the rim. |
| Bold / Italic / Underline | `title_bold` / `title_italic` / `title_underline` | Text styling. |
| Font | `title_font` | Font family (empty = default). |
| Size | `title_size` | Size multiplier. |
| Colour | `title_color` | Fill colour. |
| Outline | `title_outline` + `title_outline_color` / `title_outline_width` | Optional outline and its colour/width (width as a fraction of the glyph). |
| Offset X / Y | `title_offset_x` / `title_offset_y` | Nudge off the default spot (fractions of the outer radius). On `arc`, +X rotates around the rim and +Y slides toward the hub. |
| Override text | `title_override` | Replace the album title: one disc line per text line, with an optional leading `[1.5]` scaling that line. |

### Tracks

Curved concentric rings (`arc`), two rim-hugging columns (`columns`), or a
multi-column table in a bottom band (`table`).

| Knob | Key | Meaning |
|---|---|---|
| Layout | `track_layout` | `arc`, `columns`, or `table`. |
| Band height | `track_band` | *Table only.* Band height as a fraction of the diameter. |
| Padding | `track_pad` | *Table only.* Breathing room. |
| Numbers | `track_numbers` | Show track numbers. |
| Underline / Bold / Italic | `track_underline` / `track_bold` / `track_italic` | Text styling. |
| Font / Size | `track_font` / `track_size` | Font family and size multiplier. |
| Offset | `track_offset` | *Arc only.* Radial nudge of the track block (+ toward the hub). |
| Spacing | `track_spacing` | *Columns only.* Line-spacing multiplier (clamped to stay on the disc). |
| Colour / Outline | `track_color`, `track_outline` + `track_outline_color` / `track_outline_width` | Fill colour and optional outline. |

### Cover art

Two independent sub-layers sharing the same covers, extracted from the audio
files' tags (TagLib) or sidecar images.

| Knob | Key | Meaning |
|---|---|---|
| Enable | `covers_enabled` | Master switch for both cover sub-layers. |
| Order | `cover_order` | Which covers to use and in what order (indices into the extracted list; empty = all, as found). Edited by the dialog's drag-to-reorder / untick-to-drop list. |
| Sequence | `cover_sequence` | `false` = automatic anti-clumping spread of each cover's repeats; `true` = strict order (reading order for grid/scatter, along the arm for spiral, clockwise for ring). |

**Background mosaic** — covers repeated behind everything, washed back so text
stays readable:

| Knob | Key | Meaning |
|---|---|---|
| Style | `cover_bg` | `none`, `grid`, or `spiral`. |
| Fade | `cover_fade` + `cover_fade_color` | How far, and toward what colour, the mosaic is washed out (0–255). |
| Desaturate | `cover_desat` | Colour drain (0 = full colour, 1 = greyscale). |
| Scale | `cover_scale` | Tile size. |
| Overlap | `cover_overlap` | How much neighbouring tiles overlap. |
| Blur | `cover_blur` | Gaussian blur on the mosaic. |
| Tint | `cover_tint` + `cover_tint_strength` | Colour tint and its strength. |
| Jitter | `cover_jitter` | Random per-tile position jitter. |
| Frame | `cover_frame` | Draw a frame around each tile. |

**Feature covers** — each distinct cover shown once, crisp and full-colour, above
the background but under the text:

| Knob | Key | Meaning |
|---|---|---|
| Placement | `cover_feature` | `none`, `ring` (around the rim), or `scatter` (through the middle). |
| Scale | `feature_scale` | Cover size. |
| Tilt | `feature_tilt` | Max random rotation (degrees) for a scrapbook look. |
| Ring depth | `ring_depth` | *Ring only.* Band depth as a fraction of the printable annulus. |
| Ring title gap | `ring_title_gap` | *Ring only.* Extra clearance (degrees) each side of the title. |
| Avoid text | `scatter_avoid_text` | *Scatter only.* Keep clear of band-shaped text zones (straight title / table tracks). |

### Background image

Your own image under the cover layers.

| Knob | Key | Meaning |
|---|---|---|
| Enable | `bg_image_enabled` | Use a custom background image. |
| Path | `bg_image_path` | Image file (remembered in the preset). |
| Fit | `bg_image_fit` | `cover` (fill the disc), `contain` (fit inside), or `stretch`. |
| Fade / Desaturate / Blur | `bg_image_fade` / `bg_image_desat` / `bg_image_blur` | Wash the image back so foreground reads over it. |

### Backdrop

Solid or gradient fill beneath everything.

| Knob | Key | Meaning |
|---|---|---|
| Enable | `backdrop_enabled` | Turn the backdrop on. |
| Colours | `backdrop_color` / `backdrop_color2` | Fill colours (the second is the gradient end). |
| Gradient | `backdrop_gradient` / `backdrop_gradient_radial` | Blend between the two colours; radial vs. linear. |

### Full bleed

| Knob | Key | Meaning |
|---|---|---|
| Full bleed | `bleed_edge` | Run the background layers past the printable outer ring to the disc edge, letting the printer clip — no white margin. |

### Overlay band

A feathered translucent band over the background layers.

| Knob | Key | Meaning |
|---|---|---|
| Enable | `band_enabled` | Turn the band on. |
| Style | `band_style` | `ring` (concentric) or `strip` (horizontal). |
| Colour / Alpha | `band_color` / `band_alpha` | Band colour and opacity. |
| Height | `band_height` | Band thickness. |
| Feather | `band_feather` | Softness of the band's edges. |

### Hub & hub ring

| Knob | Key | Meaning |
|---|---|---|
| Hub mode | `hub_mode` | `blank` (leave the inner area clear), `fill` (paint it), or `background` (let the background layers run into the hole). |
| Hub colours | `hub_color` / `hub_color2` | Fill colours. |
| Hub gradient | `hub_gradient` / `hub_gradient_radial` | Blend between them; radial vs. linear. |
| Hub ring | `hub_ring_enabled` | A metallic ring hugging the centre hole (wants a hub-printable disc or the `background` hub mode to show fully). |
| Ring look | `hub_ring_color` / `hub_ring_width` / `hub_ring_shine` | Colour, width and highlight strength of the ring. |

### Circular waveform

A faint waveform behind the track list, seeded by the track names — stable, but
unique to each album.

| Knob | Key | Meaning |
|---|---|---|
| Enable | `waveform_enabled` | Turn the waveform on. |
| Colour / Alpha | `waveform_color` / `waveform_alpha` | Colour and opacity. |
| Radius | `waveform_radius` | Radius of the waveform ring. |
| Amplitude | `waveform_amplitude` | Bar height. |
| Bars | `waveform_bars` | Number of bars around the ring. |

### Text plates

A rounded pill behind each individual text run — straight rows get a rounded
rectangle, curved text an arc-shaped pill on its ring — so lines read over busy
art.

| Knob | Key | Meaning |
|---|---|---|
| Enable | `text_plate_enabled` | Turn plates on. |
| Colour / Alpha | `text_plate_color` / `text_plate_alpha` | Pill colour and opacity. |
| Radius | `text_plate_radius` | Corner rounding. |
| Padding | `text_plate_pad` | Space between text and pill edge. |
| Outline | `text_plate_outline` / `text_plate_outline_color` | Optional pill outline. |

### Text panels

A treatment of the whole zone behind the title and/or the tracks (a band for
straight layouts, a ring for curved ones).

| Knob | Key | Meaning |
|---|---|---|
| Behind title / tracks | `panel_title` / `panel_tracks` | Which zones get a panel. |
| Mode | `panel_mode` | `blur` re-blurs the cover mosaic inside the zone; `solid` fills it opaquely. |
| Blur | `panel_blur` | Blur radius in `blur` mode. |
| Colour | `panel_color` | Fill colour in `solid` mode. |
| Tint | `panel_tint` / `panel_tint_strength` | Colour wash over the zone (either mode). |
| Fade | `panel_fade` | Lighten/darken wash over the zone (either mode). |

## Preset format

Presets are plain JSON with snake-case keys, meant to be human-editable. Each
carries a `"format": "BCSLv1"` tag; a file with no tag or a different major
version is rejected, so only genuine BinCue Studio Label configs load. Additive
fields keep the same major — unknown keys are ignored and missing ones default —
so presets stay forward- and backward-compatible as fields come and go.
