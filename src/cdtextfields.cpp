#include "cdtextfields.h"

#include <QCoreApplication>

namespace cdtext {
namespace {

using Params = ExportWorker::Params;

// Performer and songwriter fall back to the disc value on a track that lacks
// their own; title/composer/arranger/message do not and must be set per track.
// Title and performer are the two cdrdao insists on once any CD-Text exists.
const Field kFields[PackCount] = {
    {QT_TR_NOOP("Title"), QT_TR_NOOP("Unknown Title"), &Params::albumTitle,
     &Track::title, false, true},
    {QT_TR_NOOP("Performer"), QT_TR_NOOP("Unknown Artist"),
     &Params::albumPerformer, &Track::performer, true, true},
    {QT_TR_NOOP("Songwriter"), QT_TR_NOOP("Unknown Songwriter"),
     &Params::albumSongwriter, &Track::songwriter, true, false},
    {QT_TR_NOOP("Composer"), QT_TR_NOOP("Unknown Composer"),
     &Params::albumComposer, &Track::composer, false, false},
    {QT_TR_NOOP("Arranger"), QT_TR_NOOP("Unknown Arranger"),
     &Params::albumArranger, &Track::arranger, false, false},
    {QT_TR_NOOP("Message"), QT_TR_NOOP("Unknown Message"), &Params::albumMessage,
     &Track::message, false, false},
};

QString tr(const char *s)
{
    return QCoreApplication::translate("CdText", s);
}

QString discOf(const Params &p, const Field &f)
{
    return (p.*(f.disc)).trimmed();
}

QString trackOf(const Track &t, const Field &f)
{
    return (t.*(f.track)).trimmed();
}

}  // namespace

const Field &field(Pack p)
{
    return kFields[int(p)];
}

bool present(const Params &p, const Field &f)
{
    if (!discOf(p, f).isEmpty())
        return true;
    for (const Track &t : p.tracks)
        if (!trackOf(t, f).isEmpty())
            return true;
    return false;
}

bool anyPresent(const Params &p)
{
    for (const Field &f : kFields)
        if (present(p, f))
            return true;
    // DISC_ID is disc-only, so it's not a Field, but setting it still means a
    // CD-Text block exists.
    return !p.albumDiscId.trimmed().isEmpty();
}

bool inUse(const Params &p, const Field &f)
{
    return present(p, f) || (f.mandatory && anyPresent(p));
}

bool discSet(const Params &p, const Field &f)
{
    return !discOf(p, f).isEmpty();
}

QString discValue(const Params &p, const Field &f)
{
    return discOf(p, f);
}

int blankTrackCount(const Params &p, const Field &f)
{
    int blank = 0;
    for (const Track &t : p.tracks)
        if (trackOf(t, f).isEmpty())
            ++blank;
    return blank;
}

bool anyTrackBlank(const Params &p, const Field &f)
{
    return blankTrackCount(p, f) > 0;
}

bool complete(const Params &p, const Field &f)
{
    if (!discSet(p, f))
        return false;
    if (f.fallback)
        return true;  // disc set ⇒ every empty track inherits it
    return !anyTrackBlank(p, f);
}

bool needsAttention(const Params &p, const Field &f)
{
    return inUse(p, f) && !complete(p, f);
}

void applyTrackFill(Params &p, const Field &f, const QString &v)
{
    for (Track &t : p.tracks)
        if (trackOf(t, f).isEmpty())
            t.*(f.track) = v;
}

void dropField(Params &p, const Field &f)
{
    p.*(f.disc) = QString();
    for (Track &t : p.tracks)
        t.*(f.track) = QString();
}

}  // namespace cdtext
