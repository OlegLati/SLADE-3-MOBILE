#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace slade_mobile
{
std::vector<int32_t> decodeFlat(const uint8_t* data, uint32_t size, const uint8_t* pal);
std::vector<int32_t> decodeDoomGraphic(const uint8_t* data, uint32_t size, const uint8_t* pal, int* outW, int* outH);
std::string audioInfoFor(const std::string& type, const uint8_t* data, uint32_t size);
} // namespace slade_mobile
