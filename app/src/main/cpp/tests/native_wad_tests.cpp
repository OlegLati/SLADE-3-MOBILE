#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "Archive/Formats/WadArchive.h"
#include "ArchiveSession.h"
#include "ArchiveSerializer.h"
#include "SaveCoordinator.h"
#include "ArchiveOperations.h"
#include "ArchiveEntryReader.h"
#include "ArchivePreview.h"
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

    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())),
          "reopen session for add/replace operations");

    const char replacement[] = "XYZ";
    session.clearDirty();
    check(operations.replace(session, 0, replacement, 3),
          "ArchiveOperations replace succeeds");
    check(session.isDirty(), "replace marks session dirty");
    auto* replaced = session.archive()->entryAt(0);
    check(replaced != nullptr && replaced->size() == 3, "replace updates entry size");
    const auto* replacedData = replaced->rawData(false);
    check(replacedData != nullptr && replacedData[0] == 'X' && replacedData[1] == 'Y'
              && replacedData[2] == 'Z',
          "replace updates entry data");

    const char addedData[] = "12345";
    session.clearDirty();
    check(operations.add(session, "ADDED", addedData, 5),
          "ArchiveOperations add succeeds");
    check(session.isDirty(), "add marks session dirty");
    check(session.archive()->numEntries() == 2, "add appends an entry");
    check(session.archive()->entryAt(1)->name() == "ADDED",
          "add preserves entry name");

    MemChunk serialized;
    slade_mobile::ArchiveSerializer serializer;
    check(serializer.serialize(session, serialized, true),
          "serialize add/replace operations");

    WadArchive reopened;
    check(reopened.open(serialized), "reopen add/replace output");
    check(reopened.numEntries() == 2, "reopened add/replace output has two entries");
    check(reopened.entryAt(0)->size() == 3, "replaced entry size survives serialization");
    check(reopened.entryAt(1)->name() == "ADDED", "added entry survives serialization");
    const auto* addedDataAgain = reopened.entryAt(1)->rawData(false);
    check(addedDataAgain != nullptr && addedDataAgain[0] == '1' && addedDataAgain[4] == '5',
          "added entry data survives serialization");
}

void testArchiveOperationBoundaries()
{
    const auto bytes = makeSingleEntryWad();
    slade_mobile::ArchiveSession session;
    slade_mobile::ArchiveOperations operations;

    check(!operations.rename(session, 0, "NOPE"), "rename rejects closed session");
    check(!operations.remove(session, 0), "remove rejects closed session");
    check(!operations.move(session, 0, 0), "move rejects closed session");
    check(!operations.add(session, "NOPE", "x", 1), "add rejects closed session");
    check(!operations.replace(session, 0, "x", 1), "replace rejects closed session");

    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())),
          "session opens for operation boundary test");

    check(!operations.rename(session, 99, "NOPE"), "rename rejects invalid index");
    check(!operations.remove(session, 99), "remove rejects invalid index");
    check(!operations.move(session, 99, 0), "move rejects invalid index");
    check(!operations.replace(session, 99, "x", 1), "replace rejects invalid index");
    check(!operations.rename(session, 0, nullptr), "rename rejects null name");
    check(!operations.add(session, "EMPTY", nullptr, 0), "add rejects empty payload");
    check(!operations.replace(session, 0, nullptr, 0), "replace rejects empty payload");
    check(!operations.replace(session, 0, "x", 0), "replace rejects zero-sized payload");
    check(!session.isDirty(), "rejected operations do not dirty the session");
}

void testArchiveEntryReaderBoundaries()
{
    const auto bytes = makeSingleEntryWad();
    slade_mobile::ArchiveSession session;
    slade_mobile::ArchiveEntryReader reader;

    check(!reader.data(session, 0, nullptr), "reader rejects closed session");

    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())),
          "session opens for reader boundary test");

    uint32_t size = 123;
    check(reader.data(session, 99, &size) == nullptr, "reader rejects invalid index");
    check(size == 123, "reader leaves size unchanged on invalid index");
    check(reader.data(session, 0, nullptr) == nullptr, "reader rejects null output size");
    check(reader.findPalette(session, nullptr) == nullptr, "palette reader rejects null output size");
}

void testArchivePreviewRejectsShortInput()
{
    std::vector<uint8_t> palette(256 * 3, 0);
    const uint8_t tiny[] = {0};

    check(slade_mobile::decodeFlat(tiny, 1, palette.data()).empty(),
          "flat decoder rejects short input");
    check(slade_mobile::decodeFlat(nullptr, 4096, palette.data()).empty(),
          "flat decoder rejects null data");
    check(slade_mobile::decodeDoomGraphic(tiny, 1, palette.data(), nullptr, nullptr).empty(),
          "patch decoder rejects missing dimensions");
    int width = -1;
    int height = -1;
    check(slade_mobile::decodeDoomGraphic(tiny, 1, palette.data(), &width, &height).empty(),
          "patch decoder rejects short input");
    check(width == 0 && height == 0, "patch decoder resets dimensions on rejected input");
    check(slade_mobile::audioInfoFor("WAV Sound", nullptr, 0).empty(),
          "audio preview rejects null input");
}

void testArchivePreviewBoundaries()
{
    const uint8_t malformedPatch[] = {2, 0, 2, 0, 0, 0, 0, 0};
    std::vector<uint8_t> palette(256 * 3, 0);
    int width = -1;
    int height = -1;

    const auto pixels = slade_mobile::decodeDoomGraphic(
            malformedPatch, static_cast<uint32_t>(sizeof(malformedPatch)), palette.data(), &width, &height);
    check(width == 2 && height == 2, "malformed patch still reports decoded dimensions");
    check(pixels.size() == 4, "malformed patch keeps bounded pixel output");
    check(pixels[0] == 0 && pixels[3] == 0, "malformed patch does not invent pixel data");

    const uint8_t shortWav[] = {'R', 'I', 'F', 'F'};
    const std::string info = slade_mobile::audioInfoFor(
            "WAV Sound", shortWav, static_cast<uint32_t>(sizeof(shortWav)));
    check(info.find("Заголовок обрезан.") != std::string::npos,
          "WAV preview reports truncated header");

    check(slade_mobile::audioInfoFor("Unknown", shortWav, sizeof(shortWav)).empty(),
          "preview dispatch returns empty string for unknown type");
}

void testArchiveEntryReader()
{
    const auto bytes = makeSingleEntryWad();
    slade_mobile::ArchiveSession session;
    slade_mobile::ArchiveEntryReader reader;

    check(session.open(bytes.data(), static_cast<uint32_t>(bytes.size())),
          "session opens for archive entry reader test");

    uint32_t size = 0;
    slade::ArchiveEntry* entry = nullptr;
    const uint8_t* data = reader.data(session, 0, &size, &entry);
    check(data != nullptr, "reader returns entry data");
    check(size == 4, "reader returns entry size");
    check(entry != nullptr && entry->name() == "TEST", "reader returns entry");
    check(data[0] == 'A' && data[3] == 'D', "reader returns original bytes");

    const char replacement[] = "XYZ";
    slade_mobile::ArchiveOperations operations;
    check(operations.replace(session, 0, replacement, 3),
          "reader test replaces entry");
    data = reader.data(session, 0, &size, &entry);
    check(data != nullptr && size == 3, "reader returns replaced entry");
    check(data[0] == 'X' && data[2] == 'Z', "reader returns modified bytes");

    check(reader.findPalette(session, &size) == nullptr,
          "reader returns no palette when archive has none");
}

void testArchivePreview()
{
    std::vector<uint8_t> palette(256 * 3, 0);
    palette[3] = 255; // palette index 1 = red

    std::vector<uint8_t> flat(64 * 64, 1);
    const auto pixels = slade_mobile::decodeFlat(flat.data(), static_cast<uint32_t>(flat.size()), palette.data());
    check(pixels.size() == 64 * 64, "flat preview decodes 64x64 pixels");
    check(pixels[0] == static_cast<int32_t>(0xFFFF0000u), "flat preview applies palette color");

    std::vector<uint8_t> wav(44, 0);
    wav[0] = 'R'; wav[1] = 'I'; wav[2] = 'F'; wav[3] = 'F';
    wav[8] = 'W'; wav[9] = 'A'; wav[10] = 'V'; wav[11] = 'E';
    wav[12] = 'f'; wav[13] = 'm'; wav[14] = 't'; wav[15] = ' ';
    putLE32(wav, 16, 16);
    wav[20] = 1; // PCM
    wav[22] = 1; // mono
    putLE32(wav, 24, 8000);
    putLE32(wav, 28, 8000);
    wav[32] = 1;
    wav[34] = 8;
    wav[36] = 'd'; wav[37] = 'a'; wav[38] = 't'; wav[39] = 'a';
    putLE32(wav, 40, 0);

    const std::string info = slade_mobile::audioInfoFor("WAV Sound", wav.data(), static_cast<uint32_t>(wav.size()));
    check(info.find("8000 Hz") != std::string::npos, "WAV preview reports sample rate");
    check(info.find("PCM") != std::string::npos, "WAV preview reports PCM codec");
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

int runNativeWadTests()
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
    testArchiveOperationBoundaries();
    testArchiveEntryReader();
    testArchiveEntryReaderBoundaries();
    testArchivePreview();
    testArchivePreviewBoundaries();
    testArchivePreviewRejectsShortInput();
    return 0;
}

#ifndef NATIVE_TEST_RUNNER_LIBRARY
int main()
{
    const int result = runNativeWadTests();
    std::cout << "native_wad_tests: " << (result == 0 ? "PASS" : "FAIL") << "\n";
    return result;
}
#endif
