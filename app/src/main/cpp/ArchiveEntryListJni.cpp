#include "Main.h"
#include "ArchiveEntryList.h"

namespace slade_mobile
{
jobjectArray buildEntryListArray(JNIEnv* env, const ArchiveSession& session)
{
    jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass)
        return nullptr;

    const auto entries = buildEntryList(session);
    jobjectArray result = env->NewObjectArray(
        static_cast<jsize>(entries.size()), stringClass, nullptr);
    if (!result)
        return nullptr;

    for (jsize i = 0; i < static_cast<jsize>(entries.size()); ++i)
    {
        const auto& entry = entries[static_cast<size_t>(i)];
        const std::string line =
            entry.name + "\t" + std::to_string(entry.size) + "\t" + entry.type;

        jstring value = env->NewStringUTF(line.c_str());
        if (!value)
            return nullptr;

        env->SetObjectArrayElement(result, i, value);
        env->DeleteLocalRef(value);
    }

    return result;
}
} // namespace slade_mobile
