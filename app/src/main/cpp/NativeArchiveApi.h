#pragma once

#include <cstdint>
#include <string>

#include "ArchiveSession.h"
#include "ArchiveSerializer.h"
#include "SaveCoordinator.h"
#include "ArchiveOperations.h"
#include "ArchiveEntryReader.h"

namespace slade_mobile
{

class NativeArchiveApi
{
public:
    bool open(const unsigned char* data, uint32_t size);
    bool rename(unsigned index, const char* newName);
    bool remove(unsigned index);
    bool move(unsigned index, unsigned newPosition);

    bool isDirty() const;

    bool saveToFd(int fd);
    const char* validateForSave();
    bool commitSave(int fd);
    bool discardChanges();

    bool exportEntry(unsigned index, int fd);
    bool addEntry(const char* name, int fd);
    bool replaceEntry(unsigned index, int fd);

    const uint8_t* entryData(unsigned index, uint32_t* outSize,
                             slade::ArchiveEntry** outEntry = nullptr);
    std::string entryType(unsigned index);
    const uint8_t* paletteData(uint32_t* outSize);

    ArchiveSession& session();
    const ArchiveSession& session() const;
    ArchiveEntryReader& entryReader();

private:
    ArchiveSession session_;
    ArchiveSerializer serializer_;
    SaveCoordinator saveCoordinator_;
    ArchiveOperations operations_;
    ArchiveEntryReader entryReader_;
};

} // namespace slade_mobile
