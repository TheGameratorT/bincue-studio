#include "toolpaths.h"

#include <QCoreApplication>
#include <QStandardPaths>

QString resolveMediaTool(const QString &name)
{
    // findExecutable with an explicit search path appends the platform suffix
    // (.exe on Windows) for us, so the bundled copy is found by its bare name.
    const QString bundled = QStandardPaths::findExecutable(
        name, {QCoreApplication::applicationDirPath()});
    if (!bundled.isEmpty())
        return bundled;
    return QStandardPaths::findExecutable(name);
}
