#include "Main.h"

#include "ArchiveSession.h"

namespace slade_mobile
{

ArchiveSession::~ArchiveSession()
{
    close();
}

bool ArchiveSession::open(const unsigned char* data, uint32_t size)
{
    close();

    mc_ = new slade::MemChunk();
    mc_->importMem(data, size);

    wad_ = new slade::WadArchive();
    if (!wad_->open(*mc_))
    {
        close();
        return false;
    }

    return true;
}

void ArchiveSession::close()
{
    delete wad_;
    delete mc_;
    wad_ = nullptr;
    mc_ = nullptr;
    dirty_ = false;
    pendingSave_.reset();
}

bool ArchiveSession::isOpen() const
{
    return wad_ != nullptr && mc_ != nullptr;
}

slade::WadArchive* ArchiveSession::archive() const
{
    return wad_;
}

slade::MemChunk* ArchiveSession::memChunk() const
{
    return mc_;
}

bool ArchiveSession::isDirty() const
{
    return dirty_;
}

void ArchiveSession::markDirty()
{
    dirty_ = true;
}

void ArchiveSession::clearDirty()
{
    dirty_ = false;
}

bool ArchiveSession::discardChanges()
{
    if (!isOpen())
        return false;

    delete wad_;
    wad_ = new slade::WadArchive();

    if (!wad_->open(*mc_))
    {
        delete wad_;
        wad_ = nullptr;
        delete mc_;
        mc_ = nullptr;
        dirty_ = false;
        pendingSave_.reset();
        return false;
    }

    dirty_ = false;
    pendingSave_.reset();
    return true;
}

bool ArchiveSession::hasPendingSave() const
{
    return pendingSave_ != nullptr;
}

void ArchiveSession::setPendingSave(std::unique_ptr<slade::MemChunk> mc)
{
    pendingSave_ = std::move(mc);
}

slade::MemChunk* ArchiveSession::pendingSave() const
{
    return pendingSave_.get();
}

void ArchiveSession::clearPendingSave()
{
    pendingSave_.reset();
}

} // namespace slade_mobile
