#pragma once

#include "ArchiveSession.h"

namespace slade_mobile
{

class ArchiveSerializer
{
public:
    bool serialize(ArchiveSession& session, slade::MemChunk& out, bool allowIwadOverwrite) const;
};

} // namespace slade_mobile
