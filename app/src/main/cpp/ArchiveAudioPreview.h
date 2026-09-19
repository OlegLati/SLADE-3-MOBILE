#pragma once

#include <cstdint>
#include <string>

namespace slade_mobile
{
std::string audioInfoFor(const std::string& type, const uint8_t* data, uint32_t size);
} // namespace slade_mobile
