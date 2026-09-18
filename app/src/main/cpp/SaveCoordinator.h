#pragma once

#include "ArchiveSession.h"
#include "ArchiveSerializer.h"

namespace slade_mobile
{

class SaveCoordinator
{
public:
    const char* validateForSave(ArchiveSession& session) const;
};

} // namespace slade_mobile
