#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ArchiveSession.h"

namespace slade_mobile
{

struct ArchiveEntryInfo
{
    std::string name;
    uint32_t size = 0;
    std::string type;
};

std::vector<ArchiveEntryInfo> buildEntryList(const ArchiveSession& session);

} // namespace slade_mobile
