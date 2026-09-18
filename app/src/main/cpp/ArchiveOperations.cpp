#include "Main.h"

#include "ArchiveOperations.h"

namespace slade_mobile
{

bool ArchiveOperations::rename(ArchiveSession& session, unsigned index, const char* newName) const
{
    if (!session.isOpen() || !newName)
        return false;

    auto* wad = session.archive();
    auto* entry = wad->entryAt(index);
    if (!entry)
        return false;

    const bool ok = wad->renameEntry(entry, newName);
    if (ok)
        session.markDirty();
    return ok;
}

bool ArchiveOperations::remove(ArchiveSession& session, unsigned index) const
{
    if (!session.isOpen())
        return false;

    auto* wad = session.archive();
    auto* entry = wad->entryAt(index);
    if (!entry)
        return false;

    const bool ok = wad->removeEntry(entry);
    if (ok)
        session.markDirty();
    return ok;
}

bool ArchiveOperations::move(ArchiveSession& session, unsigned index, unsigned newPosition) const
{
    if (!session.isOpen())
        return false;

    auto* wad = session.archive();
    auto* entry = wad->entryAt(index);
    if (!entry)
        return false;

    const bool ok = wad->moveEntry(entry, newPosition);
    if (ok)
        session.markDirty();
    return ok;
}

} // namespace slade_mobile
