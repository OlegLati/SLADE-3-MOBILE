#include <jni.h>

#include "Main.h"
#include "JniUtils.h"

namespace slade_mobile
{

std::string sanitizeAscii(const uint8_t* data, uint32_t len)
{
    std::string out;
    out.reserve(len);

    for (uint32_t i = 0; i < len; ++i)
    {
        const uint8_t c = data[i];
        if (c == 0)
            break;

        if (c == '\t' || c == '\n' || c == '\r' || (c >= 0x20 && c < 0x7F))
            out.push_back(static_cast<char>(c));
        else
            out.push_back('.');
    }

    return out;
}

jintArray packImage(JNIEnv* env, int width, int height, const std::vector<jint>& pixels)
{
    jintArray result = env->NewIntArray(2 + static_cast<jsize>(pixels.size()));
    if (!result)
        return nullptr;

    const jint header[2] = { width, height };
    env->SetIntArrayRegion(result, 0, 2, header);
    env->SetIntArrayRegion(
        result,
        2,
        static_cast<jsize>(pixels.size()),
        pixels.data());

    return result;
}

} // namespace slade_mobile
