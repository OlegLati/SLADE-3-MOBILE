#pragma once

#include "ArchiveSession.h"

namespace slade_mobile
{

class ArchiveOperations
{
public:
    bool rename(ArchiveSession& session, unsigned index, const char* newName) const;
    bool remove(ArchiveSession& session, unsigned index) const;
    bool move(ArchiveSession& session, unsigned index, unsigned newPosition) const;
    bool add(ArchiveSession& session, const char* name, const void* data, uint32_t size) const;
    bool replace(ArchiveSession& session, unsigned index, const void* data, uint32_t size) const;
};

} // namespace slade_mobile
