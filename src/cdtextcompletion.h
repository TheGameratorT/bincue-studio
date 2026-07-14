#pragma once

#include "exportworker.h"

class QWidget;

// cdrdao (1.2.x) rejects a TOC whose CD-Text uses a pack type on only some of
// {disc + each track}: each field must be non-empty on the disc AND every
// track, or be absent entirely — an empty "" reads as absent, not as a filled
// slot. It also insists that TITLE and PERFORMER exist the moment any CD-Text
// block does. So a disc that (say) has per-track performers but a blank album
// performer, or a composer on only some tracks, produces a TOC cdrdao refuses.
//
// ensureCdTextComplete() finds those partially used pack types (and the
// Title/Performer that become mandatory once any CD-Text exists) and, if any,
// asks the user to complete them before the burn/export runs. Each field gets
// an editable value defaulting to "Unknown …" (or the disc/common value when
// there is one); the dialog only ever *fills* the missing slots, so nothing the
// user already entered is discarded, and every field must be non-empty to
// proceed (Ok is disabled otherwise). The choices are applied to `params` in
// place — its own tracks copy, never the user's project — and the disc-level
// composer/arranger/message land in the Params fields buildToc reads.
//
// Returns true to proceed (including the no-op case where nothing needed
// fixing, when no dialog is shown), or false if the user cancelled.
bool ensureCdTextComplete(ExportWorker::Params &params, QWidget *parent);
