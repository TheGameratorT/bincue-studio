#pragma once

#include <QString>

// Resolve a bundled media helper (ffmpeg / ffprobe) to an absolute path.
//
// The Windows installer ships these next to the app exe, which is NOT on PATH,
// so a bare QProcess::start("ffmpeg") fails there even though the binary is
// present. Prefer the copy next to our own executable, then fall back to PATH
// (the normal case on Linux, where they come from the system). Returns an empty
// string only when neither location has it.
QString resolveMediaTool(const QString &name);
