#include "Main.h"
#include "ArchiveEntryList.h"
#include "ArchiveEntryReader.h"

#include <sstream>

namespace slade_mobile
{
jobjectArray buildEntryListArray(JNIEnv* env)
{
    jclass stringClass = env->FindClass("java/lang/String");

    auto*    wad   = session.archive();
    auto*    mc    = session.memChunk();
    unsigned count = wad->numEntries();
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), stringClass, nullptr);

    for (unsigned i = 0; i < count; ++i)
    {
        auto* entry = wad->entryAt(i);

        std::ostringstream line;
        if (entry)
        {
            const uint32_t size = entry->size();
            const uint8_t* ptr  = nullptr;
            // Phase 7 (Export/Import/Replace): same isLoaded()-first check
            // as reader.data(session, ) -- a replaced entry's real current bytes
            // live in entry->rawData(), not at its old offset in mc. See
            // reader.data(session, )'s comment for the full reasoning.
            if (entry->isLoaded())
            {
                if (size > 0)
                    ptr = entry->rawData(false);
            }
            else
            {
                const uint32_t offset = wad->getEntryOffset(entry);
                if (size > 0 && static_cast<uint64_t>(offset) + size <= mc->size())
                    ptr = mc->data() + offset;
            }

            line << entry->name() << "\t" << size << "\t" << androidDetectEntryType(entry->upperName(), size, ptr);
        }
        else
            line << "?\t0\t?";

        env->SetObjectArrayElement(
            result,
            static_cast<jsize>(i),
            env->NewStringUTF(line.str().c_str()));
    }

    return result;
}


}
