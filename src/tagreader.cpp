#include "tagreader.h"

#ifdef HAVE_TAGLIB
#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>
#endif

#ifdef HAVE_TAGLIB
namespace {

// First non-empty value among the given tag keys, or "". TagLib's
// PropertyMap presents all formats' tags under unified upper-case keys.
QString firstTag(const TagLib::PropertyMap &props,
                 std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        for (const TagLib::String &value : props.value(key)) {
            const QString text =
                QString::fromUtf8(value.toCString(true)).trimmed();
            if (!text.isEmpty())
                return text;
        }
    }
    return {};
}

} // namespace
#endif

bool tagReaderAvailable()
{
#ifdef HAVE_TAGLIB
    return true;
#else
    return false;
#endif
}

TrackTags readTrackTags(const QString &path)
{
    TrackTags tags;
#ifdef HAVE_TAGLIB
    const TagLib::FileRef file(path.toUtf8().constData());
    if (file.isNull())
        return tags;
    const TagLib::PropertyMap props = file.properties();
    tags.title = firstTag(props, {"TITLE"});
    tags.performer = firstTag(props, {"ARTIST"});
    tags.songwriter = firstTag(props, {"SONGWRITER", "COMPOSER"});
    tags.isrc = firstTag(props, {"ISRC"});
#else
    Q_UNUSED(path);
#endif
    return tags;
}

AlbumTags readAlbumTags(const QString &path)
{
    AlbumTags tags;
#ifdef HAVE_TAGLIB
    const TagLib::FileRef file(path.toUtf8().constData());
    if (file.isNull())
        return tags;
    const TagLib::PropertyMap props = file.properties();
    tags.title = firstTag(props, {"ALBUM"});
    tags.performer = firstTag(props, {"ALBUMARTIST", "ALBUM ARTIST"});
    tags.songwriter = firstTag(props, {"SONGWRITER", "COMPOSER"});
    tags.genre = firstTag(props, {"GENRE"});
    tags.year = firstTag(props, {"DATE", "YEAR"}).left(4);
    tags.catalog =
        firstTag(props, {"CATALOGNUMBER", "CATALOG", "BARCODE", "UPC"});
#else
    Q_UNUSED(path);
#endif
    return tags;
}
