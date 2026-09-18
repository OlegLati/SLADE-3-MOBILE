#include "Main.h"
#include "ArchiveEntryReader.h"

#include "Archive/Formats/WadArchive.h"
#include "Utility/MemChunk.h"

namespace slade_mobile
{
const uint8_t* ArchiveEntryReader::data(ArchiveSession& session, unsigned index,
                                        uint32_t* outSize,
                                        slade::ArchiveEntry** outEntry) const
{
    if (!session.isOpen() || !outSize)
        return nullptr;

    auto* wad = session.archive();
    auto* mc = session.memChunk();
    auto* entry = wad->entryAt(index);
    if (!entry)
        return nullptr;

    if (outEntry)
        *outEntry = entry;

    if (entry->isLoaded())
    {
        const uint32_t size = entry->size();
        if (size == 0)
            return nullptr;
        *outSize = size;
        return entry->rawData(false);
    }

    const uint32_t offset = wad->getEntryOffset(entry);
    const uint32_t size = entry->size();
    if (size == 0 || static_cast<uint64_t>(offset) + size > mc->size())
        return nullptr;

    *outSize = size;
    return mc->data() + offset;
}

const uint8_t* ArchiveEntryReader::findPalette(ArchiveSession& session, uint32_t* outSize) const
{
    if (!session.isOpen() || !outSize)
        return nullptr;

    auto* wad = session.archive();
    auto* mc = session.memChunk();

    const unsigned count = wad->numEntries();
    const uint8_t* fallbackPtr = nullptr;
    uint32_t fallbackSize = 0;

    for (unsigned i = 0; i < count; ++i)
    {
        auto* entry = wad->entryAt(i);
        if (!entry)
            continue;

        const uint32_t offset = wad->getEntryOffset(entry);
        const uint32_t size = entry->size();
        if (size < 768 || static_cast<uint64_t>(offset) + size > mc->size())
            continue;

        const uint8_t* ptr = mc->data() + offset;
        if (slade::androidDetectEntryType(entry->upperName(), size, ptr) != "Palette")
            continue;

        if (entry->upperName() == "PLAYPAL")
        {
            *outSize = size;
            return ptr;
        }

        if (!fallbackPtr)
        {
            fallbackPtr = ptr;
            fallbackSize = size;
        }
    }

    if (fallbackPtr)
    {
        *outSize = fallbackSize;
        return fallbackPtr;
    }

    return nullptr;
}
}
