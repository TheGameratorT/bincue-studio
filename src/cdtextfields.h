#pragma once

#include <QString>

#include "exportworker.h"
#include "track.h"

// One shared model of the CD-Text pack types and cdrdao's all-or-nothing rule.
// Both the burn/export completion prompt (cdtextcompletion) and the main-window
// warning badges judge "this field is incomplete" through the functions here, so
// they can never disagree about what needs fixing — every pack type is treated
// the same way everywhere.
namespace cdtext {

// The editable text pack types, in display order. (DISC_ID is disc-only and can
// never be "partial", so it isn't modelled here — it only counts toward
// anyPresent() below, which decides whether CD-Text exists at all.)
enum class Pack { Title, Performer, Songwriter, Composer, Arranger, Message };
inline constexpr int PackCount = 6;

struct Field {
    const char *label;    // QT_TR_NOOP'd; translate at the point of use
    const char *unknown;  // default fill, e.g. "Unknown Artist"
    QString ExportWorker::Params::*disc;  // disc-level source
    QString Track::*track;                // per-track source
    bool fallback;   // a blank track inherits the disc value (performer/songwriter)
    bool mandatory;  // required the moment any CD-Text exists at all (title/performer)
};

// The descriptor for a pack. Index order matches the Pack enum.
const Field &field(Pack p);

// Any CD-Text at all is present — any pack on the disc or a track, plus the
// disc-only DISC_ID. This is what makes Title and Performer mandatory.
bool anyPresent(const ExportWorker::Params &p);

// The pack carries a value somewhere (disc or any track).
bool present(const ExportWorker::Params &p, const Field &f);
// The pack is being written and so needs completing: present anywhere, or one
// of the two mandatory packs once any CD-Text exists at all.
bool inUse(const ExportWorker::Params &p, const Field &f);

// The disc-level value is set / its trimmed text.
bool discSet(const ExportWorker::Params &p, const Field &f);
QString discValue(const ExportWorker::Params &p, const Field &f);
// Some track lacks its own value / how many do.
bool anyTrackBlank(const ExportWorker::Params &p, const Field &f);
int blankTrackCount(const ExportWorker::Params &p, const Field &f);

bool complete(const ExportWorker::Params &p, const Field &f);
// "cdrdao would reject this": in use yet not complete on the disc and every
// track (for fallback packs a disc value alone is complete). The warning badges
// flag exactly this.
bool needsAttention(const ExportWorker::Params &p, const Field &f);

// Write v onto every track that has no value of its own — the explicit per-track
// completion. Leaves the disc value and any track that already has one alone.
void applyTrackFill(ExportWorker::Params &p, const Field &f, const QString &v);

// Clear the pack entirely — the disc value and every track — so it drops out of
// the CD-Text altogether (an absent pack is the other half of cdrdao's
// all-or-nothing rule). Never valid for a mandatory pack while any CD-Text
// remains; callers gate on that.
void dropField(ExportWorker::Params &p, const Field &f);

}  // namespace cdtext
