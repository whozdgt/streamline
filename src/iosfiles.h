#pragma once
#include <functional>
#include <QString>

// iPhone and iPad: an app may only read its own folder, so a folder of documents for a pod is brought in from the
// Files app (iCloud Drive, OneDrive, On My iPhone…): the person picks it, and a copy goes into the app's own folder
// (Files › On My iPhone › Streamline › <name>). Picking the same folder again brings the copy up to date.
namespace iosfiles {
// done(dir, error): the copy's path, or an error to show, or both empty if cancelled; called on the main thread
void importFolder(std::function<void(const QString &dir, const QString &error)> done);
}
