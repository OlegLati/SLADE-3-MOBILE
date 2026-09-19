#include "Main.h"
#include "ArchiveEntryList.h"

#include <sstream>

namespace slade
{
std::string androidDetectEntryType(std::string_view upperName, uint32_t size, const uint8_t* data);
}

namespace slade_mobile
{
std::vector<ArchiveEntryInfo> buildEntryList(const ArchiveSession& session)
{
    std::vector<ArchiveEntryInfo> result;

    auto* wad = session.archive();
    auto* mc = session.memChunk();
    if (!wad || !mc)
        return result;

    const unsigned count = wad->numEntries();
    result.reserve(count);

    for (unsigned i = 0; i < count; ++i)
    {
        auto* entry = wad->entryAt(i);
        if (!entry)
        {
            result.push_back({"?", 0, "?"});
            continue;
        }

        const uint32_t size = entry->size();
        const uint8_t* ptr = nullptr;

        if (entry->isLoaded())
        {
            if (size > 0)
                ptr = entry->rawData(false);
        }
        else
        {
            const uint32_t offset = wad->getEntryOffset(entry);
            if (size > 0 && static_cast<uint64_t>(offset) + size <= mc->size())
                ptr = mc->data() + offset;
        }

        result.push_back({
            entry->name(),
            size,
            slade::androidDetectEntryType(entry->upperName(), size, ptr)
        });
    }

    return result;
}

} // namespace slade_mobile
