#include "FileDescriptorIO.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace slade_mobile
{
void MappedFile::reset()
{
    if (data && size)
        munmap(const_cast<void*>(data), size);

    data = nullptr;
    size = 0;
}

bool mapReadOnlyFd(int fd, MappedFile& out)
{
    out.reset();

    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size <= 0)
    {
        close(fd);
        return false;
    }

    const size_t size = static_cast<size_t>(st.st_size);
    void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED)
        return false;

    out.data = mapped;
    out.size = size;
    return true;
}

bool writeAllAndClose(int fd, const void* data, size_t size)
{
    const uint8_t* current = static_cast<const uint8_t*>(data);
    size_t remaining = size;

    while (remaining > 0)
    {
        const ssize_t written = write(fd, current, remaining);
        if (written <= 0)
        {
            close(fd);
            return false;
        }

        current += written;
        remaining -= static_cast<size_t>(written);
    }

    close(fd);
    return true;
}
}
