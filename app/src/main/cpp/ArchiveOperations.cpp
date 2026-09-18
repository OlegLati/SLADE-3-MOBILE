#include "Main.h"

#include <memory>

#include "Archive/ArchiveEntry.h"
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

bool ArchiveOperations::add(ArchiveSession& session, const char* name, const void* data, uint32_t size) const
{
    if (!session.isOpen() || !name || !data || size == 0)
        return false;

    auto entry = std::make_shared<ArchiveEntry>(name, size);
    if (!entry->importMem(data, size))
        return false;

    auto* wad = session.archive();
    const bool ok = static_cast<bool>(wad->addEntry(entry, wad->numEntries(), nullptr));
    if (ok)
        session.markDirty();
    return ok;
}

bool ArchiveOperations::replace(ArchiveSession& session, unsigned index, const void* data, uint32_t size) const
{
    if (!session.isOpen() || !data || size == 0)
        return false;

    auto* wad = session.archive();
    auto* entry = wad->entryAt(index);
    if (!entry)
        return false;

    const bool ok = entry->importMem(data, size);
    if (ok)
        session.markDirty();
    return ok;
}

} // namespace slade_mobile
