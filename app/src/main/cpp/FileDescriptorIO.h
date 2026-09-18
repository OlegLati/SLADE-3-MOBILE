#pragma once

#include <cstddef>

namespace slade_mobile
{
struct MappedFile
{
    const void* data = nullptr;
    size_t size = 0;

    void reset();
};

bool mapReadOnlyFd(int fd, MappedFile& out);
bool writeAllAndClose(int fd, const void* data, size_t size);
}
