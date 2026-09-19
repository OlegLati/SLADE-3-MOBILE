#include <jni.h>
#include <sstream>
#include <string>

#ifdef NATIVE_TEST_RUNNER_LIBRARY
int runNativeWadTests();
int runNativeAudioPreviewTests();
int runNativeEntryListTests();
#endif

namespace
{
void appendResult(std::ostringstream& out, const char* name, int result)
{
    out << name << ": " << (result == 0 ? "PASS" : "FAIL") << "\n";
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_runNativeTestsNative(
        JNIEnv* env,
        jobject /* this */)
{
    std::ostringstream out;
#ifdef NATIVE_TEST_RUNNER_LIBRARY
    out << "Native regression tests\n";

    const int wadResult = runNativeWadTests();
    appendResult(out, "native_wad_tests", wadResult);

    const int audioResult = runNativeAudioPreviewTests();
    appendResult(out, "native_audio_preview_tests", audioResult);

    const int entryListResult = runNativeEntryListTests();
    appendResult(out, "native_entry_list_tests", entryListResult);

    const bool allPassed =
            wadResult == 0 &&
            audioResult == 0 &&
            entryListResult == 0;

    out << "Overall: " << (allPassed ? "PASS" : "FAIL");
#else
    out << "Native regression tests disabled (BUILD_NATIVE_TESTS=OFF)";
#endif

    const std::string result = out.str();
    return env->NewStringUTF(result.c_str());
}
