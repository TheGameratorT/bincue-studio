#pragma once

// Pick a widget style that honours the system light/dark colour scheme on every
// supported platform.
//
// Qt's colorScheme() already follows the OS appearance, but on Windows 10 the
// default (legacy) Windows style draws its chrome from UxTheme, which stays
// light regardless of that setting — so a dark-mode desktop still renders a
// light app. Windows 11 uses the windows11 style, which does track the scheme,
// so we leave it alone (and a Windows 10 box shouldn't be made to wear the
// Windows 11 look). For Windows 10 and earlier we switch to Fusion, which is
// painted entirely from the palette and therefore goes dark with the system.
// Other platforms keep their native style untouched.
//
// Header-only so both executables share one definition. Call once, right after
// the QApplication is constructed and before any windows are shown.

#include <QApplication>

#ifdef Q_OS_WIN
#include <QOperatingSystemVersion>
#include <QString>
#endif

inline void applyPlatformStyle()
{
#ifdef Q_OS_WIN
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows11)
        QApplication::setStyle(QStringLiteral("Fusion"));
#endif
}
