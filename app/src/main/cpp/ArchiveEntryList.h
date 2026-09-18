#pragma once

#include <jni.h>

#include "ArchiveSession.h"

namespace slade_mobile
{
jobjectArray buildEntryListArray(JNIEnv* env, ArchiveSession& session);
}
