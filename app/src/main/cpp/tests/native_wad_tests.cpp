#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "Archive/Formats/WadArchive.h"
#include "ArchiveSession.h"
#include "ArchiveSerializer.h"
#include "SaveCoordinator.h"
#include "ArchiveOperations.h"
#include "Utility/MemChunk.h"

using namespace slade;

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
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

std::vector<uint8_t> makeSingleEntryWad()
{
    std::vector<uint8_t> wad(12 + 4 + 16, 0);
    wad[0] = 'P'; wad[1] = 'W'; wad[2] = 'A'; wad[3] = 'D';
    putLE32(wad, 4, 1);
    putLE32(wad, 8, 16);
    wad[12] = 'A'; wad[13] = 'B'; wad[14] = 'C'; wad[15] = 'D';
    putLE32(wad, 16, 12);
    putLE32(wad, 20, 4);
    wad[24] = 'T'; wad[25] = 'E'; wad[26] = 'S'; wad[27] = 'T';
    return wad;
}

void testOpenAndRead()
{
    const auto bytes = makeSingleEntryWad();
    MemChunk input;
    check(input.importMem(bytes.data(), static_cast<uint32_t>(bytes.size())), "import test WAD");

    WadArchive wad;
    check(wad.open(input), "open valid WAD");
    check(wad.numEntries() == 1, "valid WAD has one entry");

    auto* entry = wad.entryAt(0);
    check(entry != nullptr, "entry 0 exists");
    check(entry->name() == "TEST", "entry name is TEST");
    check(entry->size() == 4, "entry size is 4");

    const auto* data = entry->rawData(false);
    check(data != nullptr, "entry data is available");
    check(data[0] == 'A' && data[1] == 'B' && data[2] == 'C' && data[3] == 'D',
          "entry data matches source bytes");
}

void testRejectInvalidWad()
{
    const std::vector<uint8_t> invalid = {'N', 'O', 'P', 'E', 0, 0, 0, 0, 0, 0, 0, 0};
    MemChunk input;
    check(input.importMem(invalid.data(), static_cast<uint32_t>(invalid.size())), "import invalid WAD");

    WadArchive wad;
    check(!wad.open(input), "invalid WAD is rejected");
}

void testRejectTruncatedWad()
{
    const auto valid = makeSingleEntryWad();
    std::vector<uint8_t> truncated(valid.begin(), valid.begin() + 20);

    MemChunk input;
    check(input.importMem(truncated.data(), static_cast<uint32_t>(truncated.size())),
          "import truncated WAD");

    WadArchive wad;
    check(!wad.open(input), "truncated WAD is rejected");
}

void testSerializeAndReopen()
{
    const auto bytes = makeSingleEntryWad();
    MemChunk input;
    check(input.importMem(bytes.data(), static_cast<uint32_t>(bytes.size())), "import source WAD");

    WadArchive wad;
    check(wad.open(input), "open source WAD");

    auto* entry = wad.entryAt(0);
    check(entry != nullptr, "entry exists before rename");
    check(wad.renameEntry(entry, "RENAMED"), "rename entry");

    MemChunk serialized;
    check(wad.write(serialized), "serialize modified WAD");
    check(serialized.size() > 12, "serialized WAD is non-empty");

    WadArchive reopened;
    check(reopened.open(serialized), "reopen serialized WAD");
    check(reopened.numEntries() == 1, "reopened WAD has one entry");

    auto* renamed = reopened.entryAt(0);
    check(renamed != nullptr, "renamed entry exists");
    check(renamed->name() == "RENAMED", "renamed entry survived serialization");
}

void testArchiveSessionLifecycle()
{
    const auto bytes = makeSingleEntryWad();
    slade_mobile::ArchiveSession session;

    check(!session.isOpen(), "new session is closed");
    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())), "session opens WAD");
    check(session.isOpen(), "session reports open");
    check(!session.isDirty(), "fresh session is clean");

    auto* entry = session.archive()->entryAt(0);
    check(entry != nullptr, "session entry exists");
    check(session.archive()->renameEntry(entry, "CHANGED"), "rename through session archive");
    session.markDirty();
    check(session.isDirty(), "session reports dirty after edit");

    check(session.discardChanges(), "discard changes succeeds");
    check(!session.isDirty(), "discard clears dirty state");
    check(session.archive()->entryAt(0)->name() == "TEST", "discard restores original entry");

    session.close();
    check(!session.isOpen(), "close shuts session");
}

void testArchiveSerializer()
{
    const auto bytes = makeSingleEntryWad();
    slade_mobile::ArchiveSession session;
    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())), "session opens for serializer test");

    auto* entry = session.archive()->entryAt(0);
    check(entry != nullptr, "serializer test entry exists");
    check(session.archive()->renameEntry(entry, "SERIAL"), "rename before serialization");

    slade::MemChunk serialized;
    slade_mobile::ArchiveSerializer serializer;
    check(serializer.serialize(session, serialized, true), "serializer writes modified WAD");

    slade::WadArchive reopened;
    check(reopened.open(serialized), "serializer output reopens");
    check(reopened.numEntries() == 1, "serializer output has one entry");
    check(reopened.entryAt(0)->name() == "SERIAL", "serializer preserves renamed entry");
}

void testSaveCoordinator()
{
    const auto bytes = makeSingleEntryWad();
    slade_mobile::ArchiveSession session;
    slade_mobile::SaveCoordinator coordinator;

    check(coordinator.validateForSave(session) != nullptr,
          "save validation rejects closed session");

    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())),
          "session opens for save coordinator test");

    auto* entry = session.archive()->entryAt(0);
    check(entry != nullptr, "save coordinator test entry exists");
    check(session.archive()->renameEntry(entry, "VALIDATE"),
          "rename before safe-save validation");

    check(coordinator.validateForSave(session) == nullptr,
          "save validation succeeds for valid modified WAD");
    check(session.hasPendingSave(), "successful validation stores pending save");

    WadArchive reopened;
    check(reopened.open(*session.pendingSave()), "pending save output reopens");
    check(reopened.numEntries() == 1, "validated output has one entry");
    check(reopened.entryAt(0)->name() == "VALIDATE",
          "validated output preserves modified entry");
}

void testArchiveOperations()
{
    const auto bytes = makeSingleEntryWad();
    slade_mobile::ArchiveSession session;
    slade_mobile::ArchiveOperations operations;

    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())),
          "session opens for archive operations test");

    check(operations.rename(session, 0, "EDITED"), "ArchiveOperations rename succeeds");
    check(session.isDirty(), "rename marks session dirty");
    check(session.archive()->entryAt(0)->name() == "EDITED",
          "rename changes entry name");

    session.clearDirty();
    check(operations.move(session, 0, 0), "ArchiveOperations move succeeds");
    check(session.isDirty(), "move marks session dirty");

    session.clearDirty();
    check(operations.remove(session, 0), "ArchiveOperations remove succeeds");
    check(session.isDirty(), "remove marks session dirty");
    check(session.archive()->numEntries() == 0, "remove deletes the entry");
}

void testArchiveSessionPendingSave()
{
    const auto bytes = makeSingleEntryWad();
    slade_mobile::ArchiveSession session;
    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())), "session opens for pending save test");
    check(!session.hasPendingSave(), "session starts without pending save");

    auto pending = std::make_unique<MemChunk>();
    check(pending->importMem(bytes.data(), static_cast<uint32_t>(bytes.size())), "prepare pending save");
    session.setPendingSave(std::move(pending));
    check(session.hasPendingSave(), "pending save is stored");
    check(session.pendingSave() != nullptr, "pending save is accessible");

    session.clearPendingSave();
    check(!session.hasPendingSave(), "pending save can be cleared");

    session.close();
}

void testAddAndSerialize()
{
    const auto bytes = makeSingleEntryWad();
    MemChunk input;
    check(input.importMem(bytes.data(), static_cast<uint32_t>(bytes.size())), "import source WAD");

    WadArchive wad;
    check(wad.open(input), "open source WAD");

    auto added = wad.addNewEntry("ADDED");
    check(added != nullptr, "add new entry");
    const char payload[] = "XYZ";
    check(added->importMem(payload, 3), "write added entry data");

    MemChunk serialized;
    check(wad.write(serialized), "serialize WAD with added entry");

    WadArchive reopened;
    check(reopened.open(serialized), "reopen WAD with added entry");
    check(reopened.numEntries() == 2, "added entry survived serialization");

    auto* addedAgain = reopened.entryAt(1);
    check(addedAgain != nullptr && addedAgain->name() == "ADDED", "added entry name survived");

    const auto* data = addedAgain->rawData(false);
    check(data != nullptr && data[0] == 'X' && data[1] == 'Y' && data[2] == 'Z',
          "added entry data survived serialization");
}
}

int main()
{
    testOpenAndRead();
    testRejectInvalidWad();
    testRejectTruncatedWad();
    testSerializeAndReopen();
    testAddAndSerialize();
    testArchiveSessionLifecycle();
    testArchiveSessionPendingSave();
    testArchiveSerializer();
    testSaveCoordinator();
    testArchiveOperations();

    std::cout << "native_wad_tests: PASS\n";
    return 0;
}
