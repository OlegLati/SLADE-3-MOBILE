#include <jni.h>
#include <sstream>
#include <vector>
#include <unistd.h>


// SLADE convention: every .cpp file includes Main.h FIRST. It's the header
// that (via common.h) sets up unqualified `string`/`string_view`/etc. via
// `using` declarations -- the rest of the codebase's headers rely on that
// already being in place by the time they're processed, rather than being
// self-contained. Skipping this is exactly what broke the previous attempt.
#include "Main.h"

#include "NativeArchiveApi.h"
#include "FileDescriptorIO.h"
#include "Archive/ArchiveEntry.h"
#include "ArchivePreview.h"
#include "JniUtils.h"
#include "ArchiveEntryList.h"

namespace slade
{
std::string androidDetectEntryType(std::string_view upperName, uint32_t size, const uint8_t* data);
}

namespace
{
slade_mobile::NativeArchiveApi g_archiveApi;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {

    // Hand-built minimal valid WAD: 12-byte header, 0 lumps.
    // Layout: 4-byte magic ("PWAD"), int32 numlumps, int32 infotableofs.
    unsigned char header[12] = {
        'P', 'W', 'A', 'D',
        0, 0, 0, 0,   // numlumps = 0
        12, 0, 0, 0   // infotableofs = 12 (right after the header)
    };

    slade_mobile::NativeArchiveApi api;
    const bool ok = api.open(header, sizeof(header));

    std::ostringstream out;
    out << "SLADE core on Android\n";
    out << "WadArchive::open() -> " << (ok ? "OK" : "FAILED") << "\n";
    if (ok)
        out << "Entries: " << api.entryCount();

    std::string result = out.str();
    return env->NewStringUTF(result.c_str());
}

// Opens an fd-backed WAD and returns the current entry list.
// The fd is consumed by this JNI layer; the archive session owns its data after open().

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_openWadFileFd(
        JNIEnv* env,
        jobject /* this */,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);
    jclass stringClass = env->FindClass("java/lang/String");

    slade_mobile::MappedFile mapped;
    if (!slade_mobile::mapReadOnlyFd(fd, mapped))
    {
        jobjectArray result = env->NewObjectArray(1, stringClass, nullptr);
        env->SetObjectArrayElement(result, 0, env->NewStringUTF("ERROR: could not map file"));
        return result;
    }

    // Drop whatever archive was open before (if any) -- single-session
    // app, opening a new WAD replaces the old one entirely. g_archiveApi.open()
    // performs that lifecycle transition only after the new bytes are
    // available, so a failed mapping leaves the previous archive intact.
    const bool ok = g_archiveApi.open(
        reinterpret_cast<const unsigned char*>(mapped.data),
        static_cast<uint32_t>(mapped.size));
    mapped.reset(); // g_archiveApi.session() has its own copy now

    if (!ok)
    {
        std::string msg = "ERROR: " + slade::global::error;
        jobjectArray result = env->NewObjectArray(1, stringClass, nullptr);
        env->SetObjectArrayElement(result, 0, env->NewStringUTF(msg.c_str()));
        return result;
    }

    return slade_mobile::buildEntryListArray(env, g_archiveApi.session());
}

// Content preview JNI adapters. Keep format parsing/decoding in ArchivePreview;
// this layer only validates JNI arguments and marshals results.

extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryText(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t        size = 0;
    const uint8_t* ptr = g_archiveApi.entryData(index, &size, nullptr);
    if (!ptr)
        return nullptr;

    if (g_archiveApi.entryType(index) != "Text")
        return nullptr;

    constexpr uint32_t kMaxPreview = 262144; // 256 KB
    const uint32_t     previewLen  = size < kMaxPreview ? size : kMaxPreview;

    std::string text = slade_mobile::sanitizeAscii(ptr, previewLen);
    if (previewLen < size)
        text += "\n\n[... truncated, entry is " + std::to_string(size) + " bytes ...]";

    return env->NewStringUTF(text.c_str());
}

// Returns a packed jintArray: [width, height, pixel0, pixel1, ...] where
// pixels are ARGB_8888 (matches android.graphics.Bitmap.Config.ARGB_8888
// directly, no conversion needed on the Kotlin side), rendered as a
// 16-column grid of color swatches. Returns null under the same
// conditions as getEntryText -- plus if the entry isn't "Palette", or its
// size isn't a clean multiple of 3 (each color is one RGB triple; PLAYPAL-
// style lumps have no alpha byte of their own, so alpha is forced to
// fully opaque here).
extern "C" JNIEXPORT jintArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryPalette(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t       size  = 0;
    const uint8_t* ptr = g_archiveApi.entryData(index, &size, nullptr);
    if (!ptr)
        return nullptr;

    if (g_archiveApi.entryType(index) != "Palette")
        return nullptr;

    const uint32_t numColors = size / 3;
    if (numColors == 0)
        return nullptr;

    constexpr int kCols = 16;
    const int     rows  = static_cast<int>((numColors + kCols - 1) / kCols);

    std::vector<jint> pixels(static_cast<size_t>(kCols) * rows, 0xFF000000); // opaque black padding
    for (uint32_t i = 0; i < numColors; ++i)
    {
        const uint8_t r = ptr[i * 3 + 0];
        const uint8_t g = ptr[i * 3 + 1];
        const uint8_t b = ptr[i * 3 + 2];
        pixels[i] = static_cast<jint>(0xFF000000u | (static_cast<uint32_t>(r) << 16)
                                       | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b));
    }

    return slade_mobile::packImage(env, kCols, rows, pixels);
}

// Returns a packed jintArray in the same [width, height, pixels...]
// convention as getEntryPalette(), decoded via slade_mobile::decodeFlat()/
// slade_mobile::decodeDoomGraphic() above. Returns null if: the archive isn't open, the
// index is invalid, the entry isn't classified as "Flat" or "Doom
// Graphic", the archive has no usable palette (findPalette() found
// nothing -- some PWADs genuinely don't carry their own PLAYPAL), or (for
// Doom Graphic specifically) the header claims a width/height that
// doesn't actually fit inside the entry's declared size.
extern "C" JNIEXPORT jintArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryImage(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t       size  = 0;
    slade::ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = g_archiveApi.entryData(index, &size, &entry);
    if (!ptr)
        return nullptr;

    const std::string type = slade::androidDetectEntryType(entry->upperName(), size, ptr);
    if (type != "Flat" && type != "Doom Graphic")
        return nullptr;

    uint32_t       palSize = 0;
    const uint8_t* pal     = g_archiveApi.paletteData(&palSize);
    if (!pal)
        return nullptr; // no PLAYPAL (or equivalent) anywhere in this archive

    if (type == "Flat")
    {
        // slade::androidDetectEntryType() only returns "Flat" for exactly 4096 or
        // 4160 bytes -- both comfortably cover the 64x64 = 4096 pixels
        // slade_mobile::decodeFlat() reads, so no further size check needed here.
        std::vector<jint> pixels = slade_mobile::decodeFlat(ptr, size, pal);
        return slade_mobile::packImage(env, 64, 64, pixels);
    }

    // Doom Graphic: slade::androidDetectEntryType() already sanity-checked that
    // size >= 8 + 4*width and that the first column offset lands inside
    // the entry, but slade_mobile::decodeDoomGraphic() re-derives width from the header
    // itself and bounds-checks every column offset independently, so
    // there's no need to re-validate here.
    int w = 0, h = 0;
    std::vector<jint> pixels = slade_mobile::decodeDoomGraphic(ptr, size, pal, &w, &h);
    return slade_mobile::packImage(env, w, h, pixels);
}

// Returns the entry's raw bytes as-is, for entries classified "PNG
// Image" only -- standard PNG bytes decode directly via Android's
// BitmapFactory on the Kotlin side, so there's no need to decode them
// here the way Doom Graphic/Flat need to be (they're not a format any
// Android API understands on its own). Capped at 16 MB: generous for any
// texture a WAD realistically embeds, but a guard against handing a
// pathologically large buffer across the JNI boundary and into a
// JVM-heap Bitmap decode on a 3GB-RAM device with no way to recover from
// an OOM mid-decode.
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryPng(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t       size  = 0;
    slade::ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = g_archiveApi.entryReader().data(g_archiveApi.session(), index, &size, &entry);
    if (!ptr)
        return nullptr;

    if (slade::androidDetectEntryType(entry->upperName(), size, ptr) != "PNG Image")
        return nullptr;

    constexpr uint32_t kMaxPngBytes = 16 * 1024 * 1024; // 16 MB
    if (size > kMaxPngBytes)
        return nullptr;

    jbyteArray result = env->NewByteArray(static_cast<jsize>(size));
    if (!result)
        return nullptr;
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte*>(ptr));
    return result;
}

// Returns a formatted multi-line info string for any of the nine audio
// lump types slade::androidDetectEntryType() recognizes (see slade_mobile::audioInfoFor()
// above), or null if the entry isn't audio, the archive isn't open, or
// the index is invalid. Deliberately info-only, not playback -- see the
// comment above the parser block for why.
extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryAudioInfo(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t       size  = 0;
    slade::ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = g_archiveApi.entryReader().data(g_archiveApi.session(), index, &size, &entry);
    if (!ptr)
        return nullptr;

    const std::string type = slade::androidDetectEntryType(entry->upperName(), size, ptr);
    const std::string info = slade_mobile::audioInfoFor(type, ptr, size);
    if (info.empty())
        return nullptr;

    return env->NewStringUTF(info.c_str());
}

// Archive edit JNI adapters. Business logic lives in NativeArchiveApi.

// Archive edit operations are implemented by ArchiveOperations; the JNI layer
// only converts Android/Kotlin arguments and return values.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeRenameEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index,
        jstring newName) {

    if (index < 0 || !newName)
        return JNI_FALSE;

    const char* nameChars = env->GetStringUTFChars(newName, nullptr);
    if (!nameChars)
        return JNI_FALSE;

    const bool ok = g_archiveApi.rename(static_cast<unsigned>(index), nameChars);
    env->ReleaseStringUTFChars(newName, nameChars);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeDeleteEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    if (index < 0)
        return JNI_FALSE;

    return g_archiveApi.remove(static_cast<unsigned>(index))
            ? JNI_TRUE
            : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeMoveEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index,
        jint newPosition) {

    if (index < 0 || newPosition < 0)
        return JNI_FALSE;

    return g_archiveApi.move(static_cast<unsigned>(index), static_cast<unsigned>(newPosition))
            ? JNI_TRUE
            : JNI_FALSE;
}

// Re-lists the currently-open session's entries in the same format as
// openWadFileFd() -- see buildEntryListArray()'s comment for why this
// needs to be a full re-list rather than an incremental patch. Returns
// an empty array (not null) if no archive is open, since Kotlin's
// existing parser (MainActivity.handleWadResult) only special-cases a
// single "ERROR: "-prefixed element, not null/empty.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeListEntries(
        JNIEnv* env,
        jobject /* this */) {

    if (!g_archiveApi.isOpen())
        return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    return slade_mobile::buildEntryListArray(env, g_archiveApi.session());
}

// Whether the session has unsaved changes (Phase 6/9 dirty-state
// tracking). Kotlin polls this after every edit to update a "*" in the
// title/status rather than the native side pushing state -- there's no
// JNI-to-Kotlin callback path set up, and dirty state only ever changes
// as the direct result of a Kotlin-initiated call anyway (rename/delete/
// save), so polling right after each one is enough.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeIsDirty(
        JNIEnv* env,
        jobject /* this */) {

    return g_archiveApi.isDirty() ? JNI_TRUE : JNI_FALSE;
}

// Save As: serializes the current session to a MemChunk via WadArchive::
// write(MemChunk&) (rebuilds the WAD directory from the current in-memory
// entry list, per SLADE's own convention -- not a byte-for-byte copy of
// the original file), then writes that MemChunk's bytes to `fd` in one
// shot. `fd` is expected to come from a SAF ACTION_CREATE_DOCUMENT result
// opened "rwt" (the Kotlin side truncates/creates fresh, same detachFd()
// ownership-transfer contract as the read path in openWadFileFd) -- this
// function closes fd itself once done, on both success and failure paths.
// Clears the session's dirty flag on success only, so a failed save
// leaves the "*" up rather than silently discarding the fact that the
// save didn't actually happen.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeSaveToFd(JNIEnv*, jobject, jint fdRaw) {
    return g_archiveApi.saveToFd(static_cast<int>(fdRaw)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeValidateForSave(JNIEnv* env, jobject) {
    const char* error = g_archiveApi.validateForSave();
    return error ? env->NewStringUTF(error) : nullptr;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeCommitSave(JNIEnv*, jobject, jint fdRaw) {
    return g_archiveApi.commitSave(static_cast<int>(fdRaw)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeDiscardChanges(JNIEnv*, jobject) {
    return g_archiveApi.discardChanges() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeExportEntry(JNIEnv*, jobject, jint index, jint fdRaw) {
    if (index < 0) { close(static_cast<int>(fdRaw)); return JNI_FALSE; }
    return g_archiveApi.exportEntry(static_cast<unsigned>(index), static_cast<int>(fdRaw)) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeAddEntry(JNIEnv* env, jobject, jstring name, jint fdRaw) {
    const int fd = static_cast<int>(fdRaw);
    if (!name) { close(fd); return JNI_FALSE; }
    const char* nameChars = env->GetStringUTFChars(name, nullptr);
    if (!nameChars) { close(fd); return JNI_FALSE; }
    const bool ok = g_archiveApi.addEntry(nameChars, fd);
    env->ReleaseStringUTFChars(name, nameChars);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeReplaceEntry(JNIEnv*, jobject, jint index, jint fdRaw) {
    const int fd = static_cast<int>(fdRaw);
    if (index < 0) { close(fd); return JNI_FALSE; }
    return g_archiveApi.replaceEntry(static_cast<unsigned>(index), fd) ? JNI_TRUE : JNI_FALSE;
}

