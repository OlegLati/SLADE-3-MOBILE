#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ArchiveEntryList.h"

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\\n';
        std::exit(1);
    }
}

void putLE32(std::vector<uint8_t>& data, size_t offset, uint32_t value)
{
    data[offset + 0] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
    data[offset + 2] = static_cast<uint8_t>(value >> 16);
    data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

std::vector<uint8_t> makeWad()
{
    std::vector<uint8_t> wad(12 + 4 + 16 + 3 + 16, 0);
    wad[0] = 'P'; wad[1] = 'W'; wad[2] = 'A'; wad[3] = 'D';
    putLE32(wad, 4, 2);
    putLE32(wad, 8, 19);

    wad[12] = 'A'; wad[13] = 'B'; wad[14] = 'C'; wad[15] = 'D';
    wad[16] = 'H'; wad[17] = 'E'; wad[18] = 'L'; wad[19] = 'L'; wad[20] = 'O';

    putLE32(wad, 21, 12);
    putLE32(wad, 25, 4);
    wad[29] = 'T'; wad[30] = 'E'; wad[31] = 'S'; wad[32] = 'T';

    putLE32(wad, 37, 16);
    putLE32(wad, 41, 3);
    wad[45] = 'D'; wad[46] = 'A'; wad[47] = 'T';

    return wad;
}

void testClosedSession()
{
    slade_mobile::ArchiveSession session;
    const auto entries = slade_mobile::buildEntryList(session);
    check(entries.empty(), "closed session returns empty list");
}

void testEntryMetadata()
{
    const auto bytes = makeWad();
    slade_mobile::ArchiveSession session;
    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())),
          "session opens test WAD");

    const auto entries = slade_mobile::buildEntryList(session);
    check(entries.size() == 2, "list contains two entries");

    check(entries[0].name == "TEST", "first entry name is TEST");
    check(entries[0].size == 4, "first entry size is 4");
    check(entries[0].type == "Text", "first entry type is Text");

    check(entries[1].name == "DAT", "second entry name is DAT");
    check(entries[1].size == 3, "second entry size is 3");
    check(!entries[1].type.empty(), "second entry type is classified");
}

void testModifiedEntryUsesCurrentData()
{
    const auto bytes = makeWad();
    slade_mobile::ArchiveSession session;
    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())),
          "session opens for modified-entry test");

    auto* entry = session.archive()->entryAt(0);
    check(entry != nullptr, "entry exists");

    const char replacement[] = "NOT_TEXT";
    check(entry->importMem(replacement, sizeof(replacement) - 1),
          "entry accepts replacement data");

    const auto entries = slade_mobile::buildEntryList(session);
    check(entries.size() == 2, "modified list keeps entry count");
    check(entries[0].size == sizeof(replacement) - 1,
          "modified entry reports current size");
}

} // namespace

int main()
{
    testClosedSession();
    testEntryMetadata();
    testModifiedEntryUsesCurrentData();

    std::cout << "native_entry_list_tests: PASS\\n";
    return 0;
}
