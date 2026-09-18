#include "Main.h"

#include "ArchiveSerializer.h"

#include "General/CVar.h"

using namespace slade;

EXTERN_CVAR(Bool, iwad_lock)

namespace slade_mobile
{

bool ArchiveSerializer::serialize(ArchiveSession& session, MemChunk& out, bool allowIwadOverwrite) const
{
    auto* wad = session.archive();
    auto* mc = session.memChunk();

    if (!wad || !mc)
        return false;

    for (unsigned i = 0; i < wad->numEntries(); ++i)
    {
        auto* entry = wad->entryAt(i);
        if (!entry || entry->isLoaded() || entry->size() == 0)
            continue;

        const uint32_t offset = wad->getEntryOffset(entry);
        const uint32_t size = entry->size();
        if (static_cast<uint64_t>(offset) + size <= mc->size())
            entry->importMem(mc->data() + offset, size);
    }

    if (!allowIwadOverwrite)
        return wad->write(out);

    const bool previousIwadLock = iwad_lock;
    iwad_lock = false;
    const bool wrote = wad->write(out);
    iwad_lock = previousIwadLock;
    return wrote;
}

} // namespace slade_mobile
