#pragma once

// Open an audio file with TagLib from a Qt path, correctly on every platform.
//
// On Windows TagLib's narrow-char FileRef constructor opens the path in the
// process's local ANSI codepage, so any path with non-ASCII characters (common
// with Japanese/Cyrillic album folders) fails to open and yields no tags or
// cover art. TagLib's wchar_t overload takes UTF-16 instead; elsewhere the byte
// path is already the filesystem encoding.
//
// Header-only so both executables — bincue-studio's tag reader and cdlabel's
// cover extractor — share one definition without a common library. Include only
// from translation units built with HAVE_TAGLIB.

#include <taglib/fileref.h>

#include <QFile>
#include <QString>

inline TagLib::FileRef taglibOpen(const QString &path)
{
#ifdef Q_OS_WIN
    return TagLib::FileRef(reinterpret_cast<const wchar_t *>(path.utf16()));
#else
    return TagLib::FileRef(QFile::encodeName(path).constData());
#endif
}
