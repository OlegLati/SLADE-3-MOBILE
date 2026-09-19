#pragma once

#include <jni.h>

#include "ArchiveEntryList.h"

namespace slade_mobile
{
jobjectArray buildEntryListArray(JNIEnv* env, const ArchiveSession& session);
} // namespace slade_mobile
