#include "Main.h"
#include "NativeArchiveApi.h"

#include <unistd.h>

#include "FileDescriptorIO.h"
#include "Utility/MemChunk.h"

namespace slade_mobile
{

bool NativeArchiveApi::open(const unsigned char* data, uint32_t size)
{
    return session_.open(data, size);
}

bool NativeArchiveApi::rename(unsigned index, const char* newName)
{
    return operations_.rename(session_, index, newName);
}

bool NativeArchiveApi::remove(unsigned index)
{
    return operations_.remove(session_, index);
}

bool NativeArchiveApi::move(unsigned index, unsigned newPosition)
{
    return operations_.move(session_, index, newPosition);
}

bool NativeArchiveApi::isDirty() const
{
    return session_.isDirty();
}

bool NativeArchiveApi::saveToFd(int fd)
{
    if (!session_.isOpen())
    {
        close(fd);
        return false;
    }

    slade::MemChunk out;
    if (!serializer_.serialize(session_, out, true))
    {
        close(fd);
        return false;
    }

    if (!writeAllAndClose(fd, out.data(), out.size()))
        return false;

    session_.clearDirty();
    return true;
}

const char* NativeArchiveApi::validateForSave()
{
    return saveCoordinator_.validateForSave(session_);
}

bool NativeArchiveApi::commitSave(int fd)
{
    if (!session_.isOpen() || !session_.hasPendingSave())
    {
        close(fd);
        session_.clearPendingSave();
        return false;
    }

    slade::MemChunk* out = session_.pendingSave();
    if (!writeAllAndClose(fd, out->data(), out->size()))
    {
        session_.clearPendingSave();
        return false;
    }

    session_.clearPendingSave();
    session_.clearDirty();
    return true;
}

bool NativeArchiveApi::discardChanges()
{
    return session_.discardChanges();
}

bool NativeArchiveApi::exportEntry(unsigned index, int fd)
{
    uint32_t size = 0;
    const uint8_t* data = entryReader_.data(session_, index, &size);
    if (!data)
    {
        close(fd);
        return false;
    }

    return writeAllAndClose(fd, data, size);
}

bool NativeArchiveApi::addEntry(const char* name, int fd)
{
    if (!session_.isOpen() || !name)
    {
        close(fd);
        return false;
    }

    MappedFile mapped;
    if (!mapReadOnlyFd(fd, mapped))
        return false;

    const bool ok = operations_.add(
        session_, name, mapped.data, static_cast<uint32_t>(mapped.size));
    mapped.reset();
    return ok;
}

bool NativeArchiveApi::replaceEntry(unsigned index, int fd)
{
    if (!session_.isOpen())
    {
        close(fd);
        return false;
    }

    MappedFile mapped;
    if (!mapReadOnlyFd(fd, mapped))
        return false;

    const bool ok = operations_.replace(
        session_, index, mapped.data, static_cast<uint32_t>(mapped.size));
    mapped.reset();
    return ok;
}

ArchiveSession& NativeArchiveApi::session()
{
    return session_;
}

const ArchiveSession& NativeArchiveApi::session() const
{
    return session_;
}

ArchiveEntryReader& NativeArchiveApi::entryReader()
{
    return entryReader_;
}

} // namespace slade_mobile
