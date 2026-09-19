#pragma once

#include <jni.h>
#include <cstdint>
#include <string>
#include <vector>

namespace slade_mobile
{

std::string sanitizeAscii(const uint8_t* data, uint32_t len);

jintArray packImage(JNIEnv* env, int width, int height, const std::vector<jint>& pixels);

} // namespace slade_mobile
