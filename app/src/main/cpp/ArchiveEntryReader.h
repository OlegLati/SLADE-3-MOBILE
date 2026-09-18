#pragma once

#include <cstdint>

#include "ArchiveSession.h"

namespace slade_mobile
{
class ArchiveEntryReader
{
public:
    const uint8_t* data(ArchiveSession& session, unsigned index, uint32_t* outSize,
                         slade::ArchiveEntry** outEntry = nullptr) const;

    const uint8_t* findPalette(ArchiveSession& session, uint32_t* outSize) const;
};
}
