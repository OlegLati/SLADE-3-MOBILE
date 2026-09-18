#pragma once

#include <cstdint>
#include <memory>

#include "Archive/Formats/WadArchive.h"
#include "Utility/MemChunk.h"

namespace slade_mobile
{

class ArchiveSession
{
public:
    ~ArchiveSession();

    bool open(const unsigned char* data, uint32_t size);
    void close();

    bool isOpen() const;
    slade::WadArchive* archive() const;
    slade::MemChunk* memChunk() const;

    bool isDirty() const;
    void markDirty();
    void clearDirty();

    bool discardChanges();

    bool hasPendingSave() const;
    void setPendingSave(std::unique_ptr<slade::MemChunk> mc);
    slade::MemChunk* pendingSave() const;
    void clearPendingSave();

private:
    slade::MemChunk* mc_ = nullptr;
    slade::WadArchive* wad_ = nullptr;
    bool dirty_ = false;
    std::unique_ptr<slade::MemChunk> pendingSave_;
};

} // namespace slade_mobile
