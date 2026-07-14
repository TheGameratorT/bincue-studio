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
// asks the user to settle them before the burn/export runs. For each field the
// user either fills the missing slots (editable value, defaulting to
// "Unknown …") or, for a non-mandatory field, drops it entirely so it isn't
// burned at all — the two halves of cdrdao's all-or-nothing rule. The choices
// are applied to `params` in place (its own tracks copy) and the disc-level
// composer/arranger/message land in the Params fields buildToc reads.
//
// By default nothing touches the user's project — the edits ride only on the
// burn/export copy. If the user ticks "apply to the project" the same edits
// should be written back to the real model; the result reports that so the
// caller can do it (see MainWindow::applyCdTextToProject).
struct CdTextChoice {
    bool proceed = false;         // false ⇒ the user cancelled; abandon the run
    bool applyToProject = false;  // true ⇒ also persist the edits to the project
};

// Never shows a dialog when nothing needs fixing — returns {proceed=true} so the
// run continues untouched.
CdTextChoice ensureCdTextComplete(ExportWorker::Params &params, QWidget *parent);
