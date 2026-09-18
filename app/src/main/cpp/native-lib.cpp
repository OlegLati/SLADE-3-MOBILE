#include <jni.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <memory>


// SLADE convention: every .cpp file includes Main.h FIRST. It's the header
// that (via common.h) sets up unqualified `string`/`string_view`/etc. via
// `using` declarations -- the rest of the codebase's headers rely on that
// already being in place by the time they're processed, rather than being
// self-contained. Skipping this is exactly what broke the previous attempt.
#include "Main.h"

#include "Archive/Formats/WadArchive.h"
#include "Utility/MemChunk.h"
#include "ArchiveSession.h"
#include "ArchiveSerializer.h"
#include "SaveCoordinator.h"
#include "ArchiveOperations.h"
#include "ArchiveEntryReader.h"
#include "FileDescriptorIO.h"
using namespace slade;

// WadArchive::write() refuses to serialize an IWAD (e.g. DOOM.WAD) at all
// while this is true -- SLADE's own built-in safeguard against clobbering
// someone's original game IWAD in place. Declared here (defined in
// WadArchive.cpp) the same way WadJArchive.cpp does it, so saveToFd() can
// toggle it off for the one call where it doesn't apply -- see the
// comment there for why.
// Defined in compat/slade_shims.cpp -- a real (not EntryType-based) byte-
// signature + name classifier. Takes the entry's uppercase name, size, and
// a raw pointer to its bytes directly, rather than an ArchiveEntry& --
// deliberately reads straight out of the shared WAD MemChunk (mc, below)
// by offset instead of going through ArchiveEntry::data()/rawData() at
// all. See the comment above its definition for the full story (in short:
// WadArchive::loadEntryData() only works from a file on disk, which we
// don't have, and an earlier per-entry-copy workaround risked tripling
// memory use on large WADs).
namespace slade
{
std::string androidDetectEntryType(std::string_view upperName, uint32_t size, const uint8_t* data);
}

// -----------------------------------------------------------------------
// ArchiveSession (Phase 6, ROADMAP.md): replaces the previous g_mc/g_wad
// raw-pointer pair with one owning object. Doesn't change behavior on its
// own -- same single-archive-at-a-time assumption as before, same "not
// thread-safe, but only ever touched from one native-dispatcher thread at
// a time" invariant (see SladeNative.kt's comment on why that's true) --
// it just gives the app a real open/close lifecycle (open()/close()
// instead of only ever implicitly replacing the previous archive when a
// new one loads) and one place for Phase 7's future edit operations to
// call markDirty() from, instead of another round of global-state
// plumbing when that lands.
//
// IMPORTANT (unchanged from the old g_mc/g_wad contract): WadArchive::
// open(MemChunk&) does NOT copy entry data into itself -- per HANDOFF
// notes, loadEntryData() only works from a file on disk (filename_ never
// set here), so entries are read by offset directly out of the session's
// MemChunk for as long as the archive is open. That means the MemChunk
// must outlive the WadArchive, and both must stay alive for the whole
// "list then click an entry" lifetime, not just for one JNI call --
// that's why they're owned together by one object with one lifetime,
// rather than as two independently-managed globals.
// -----------------------------------------------------------------------
namespace
{
slade_mobile::ArchiveSession g_session;
}

// Phase 7/8 shared helper: primes every entry's data (see the long-
// standing "IMPORTANT" file-header comment and nativeSaveToFd's own
// comment for why rawData()/loadEntryData() can't be trusted here) and
// serializes the current session via WadArchive::write(). Shared by Save
// As (nativeSaveToFd) and Safe Save's validation step
// (nativeValidateForSave) -- the two differ only in whether iwad_lock
// should apply for this particular write, hence the bool parameter
// rather than each duplicating the priming loop with its own iwad_lock
// handling bolted on.
//
// allowIwadOverwrite=true (Save As): never writes to the file the
// archive was opened from, so iwad_lock's protection doesn't apply here
// and is toggled off for the call, same as before this was factored out.
// allowIwadOverwrite=false (Safe Save / in-place): DOES eventually
// overwrite the original file the archive came from, which is exactly
// the scenario iwad_lock exists to guard against -- left untouched, so
// write() can refuse a real IWAD on its own and report why via
// global::error (checked by the caller).
namespace
{
slade_mobile::ArchiveSerializer g_serializer;
slade_mobile::SaveCoordinator g_saveCoordinator;
slade_mobile::ArchiveOperations g_archiveOperations;
slade_mobile::ArchiveEntryReader g_entryReader;
}


// Text lumps are, per androidDetectEntryType()'s own check, printable
// ASCII for at least the first 512 bytes -- but a longer lump isn't
// guaranteed ASCII-only past that prefix, and NewStringUTF() expects
// valid modified UTF-8. Rather than risk a JNI-level crash we can't debug
// without Logcat, replace anything outside safe printable ASCII with '.'
// and stop at the first embedded NUL (NewStringUTF would silently
// truncate there anyway, since it reads a C string).
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

// -----------------------------------------------------------------------
// Packs [width, height, pixel0, pixel1, ...] into a jintArray -- the same
// convention getEntryPalette() already uses, so the Kotlin side can share
// one unpacking code path for palettes, flats, and graphics alike.
jintArray packImage(JNIEnv* env, int width, int height, const std::vector<jint>& pixels)
{
    jintArray result = env->NewIntArray(2 + static_cast<jsize>(pixels.size()));
    if (!result)
        return nullptr;

    const jint header[2] = { width, height };
    env->SetIntArrayRegion(result, 0, 2, header);
    env->SetIntArrayRegion(result, 2, static_cast<jsize>(pixels.size()), pixels.data());
    return result;
}

#include "ArchivePreview.h"

// NOTE: method names below (MemChunk::importMem, WadArchive::open(MemChunk&),
// Archive::numEntries) are best guesses based on the SLADE conventions we've
// seen so far in Archive.cpp/WadArchive.cpp/MemChunk.cpp -- not confirmed
// against the actual header. If this doesn't compile, send the error same
// as always and we'll fix the exact names.
// Phase 5 (background processing): these were originally
// Java_..._MainActivity_* -- the `external fun` declarations lived
// directly on MainActivity. They were moved to a dedicated `SladeNative`
// Kotlin object (SladeNative.kt) so every native call is forced through
// one suspend-function wrapper on a single dedicated background thread,
// keeping native calls off the UI thread while still serializing them
// (native side keeps non-thread-safe global archive state, see the
// "Persistent archive holder" comment below). JNI resolves native methods
// by fully-qualified class name, so the exported symbols had to be
// renamed to match the object they now actually live on -- `jobject thiz`
// is unused in all of these (just `this`/the singleton instance), so the
// rename is the only change needed here; none of the logic below cares
// which Kotlin class owns the native method.
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

    MemChunk mc;
    mc.importMem(header, sizeof(header));

    WadArchive wad;
    bool ok = wad.open(mc);

    std::ostringstream out;
    out << "SLADE core on Android\n";
    out << "WadArchive::open() -> " << (ok ? "OK" : "FAILED") << "\n";
    if (ok)
        out << "Entries: " << wad.numEntries();

    std::string result = out.str();
    return env->NewStringUTF(result.c_str());
}

// CHANGED (RecyclerView support): now returns a jobjectArray of jstring
// instead of a single formatted jstring, so the Kotlin side can bind it to
// a RecyclerView adapter instead of dumping everything into one TextView.
//
// CHANGED (real EntryType detection): each entry is now run through
// androidDetectEntryType() (compat/slade_shims.cpp) before being reported,
// so the type name reflects real byte-signature/name-based detection
// rather than always being "unknown". This is a standalone classifier, not
// the real EntryType engine -- see the comment above androidDetectEntryType's
// definition for why (it depends on slade.pk3's ZIP-based resource archive,
// which isn't ported yet).
//
// CHANGED (memory safety, takes an fd instead of a byte[]): used to take a
// jbyteArray that Kotlin filled via InputStream.readBytes() -- reading the
// WHOLE file into a JVM-heap ByteArray, then GetByteArrayElements() below
// potentially copying it AGAIN into native memory, before MemChunk::
// importMem() made a third copy. Fine for a small IWAD, but Sigil/Sigil2's
// soundtrack editions run 50-150MB, and tripling that was almost certainly
// what was crashing the app. Now Kotlin hands us a raw file descriptor
// (MainActivity.kt: ParcelFileDescriptor.detachFd()) and we mmap() it
// directly -- MemChunk still makes its own one copy (it owns its buffer,
// unchanged from before), but the JVM-heap and JNI-array copies are gone
// entirely. fd ownership is ours from here on: we close() it ourselves
// once mmap() has been called (the mapping stays valid independently of
// the descriptor after that, per standard POSIX semantics).
//
// Format:
//   - success: one array element per entry, each "name\tsize\ttype" (tab-
//     separated, parsed on the Kotlin side).
//   - failure: a single-element array whose entry starts with "ERROR: "
//     followed by an error message.
// Builds the "name\tsize\ttype" array shared by openWadFileFd() (after a
// fresh open) and listEntries() (Phase 7, after an edit changes the
// index/entry-count of an already-open session -- rename/delete need the
// Kotlin-side adapter to reload from scratch rather than patch its cached
// list locally, since removeEntry() shifts every later index down by one).
// Assumes g_session is already open; callers are responsible for that.
#include "ArchiveEntryList.h"

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
    // app, opening a new WAD replaces the old one entirely. g_session.open()
    // performs that lifecycle transition only after the new bytes are
    // available, so a failed mapping leaves the previous archive intact.
    const bool ok = g_session.open(
        reinterpret_cast<const unsigned char*>(mapped.data),
        static_cast<uint32_t>(mapped.size));
    mapped.reset(); // g_session has its own copy now

    if (!ok)
    {
        std::string msg = "ERROR: " + global::error;
        jobjectArray result = env->NewObjectArray(1, stringClass, nullptr);
        env->SetObjectArrayElement(result, 0, env->NewStringUTF(msg.c_str()));
        return result;
    }

    return slade_mobile::buildEntryListArray(env, g_session);
}

// -----------------------------------------------------------------------
// getEntryText / getEntryPalette -- content viewers for the first two
// entry types (see HANDOFF discussion). Both look the entry up again by
// index in the persistent g_wad/g_mc pair rather than trusting anything
// cached on the Kotlin side, and both independently re-run
// androidDetectEntryType() rather than trusting the type string Kotlin
// already has -- cheap, and it means these functions are self-contained
// and can't be tricked into misreading an entry as the wrong type by a
// stale adapter list.
// -----------------------------------------------------------------------

// Returns the entry's bytes as a sanitized-ASCII jstring, or null if the
// archive isn't open, the index is invalid, or the entry isn't classified
// as "Text". Capped at 256 KB so a large ACS-source or SNDINFO lump
// doesn't hand a multi-megabyte string across the JNI boundary just for a
// preview dialog.
extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryText(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t        size = 0;
    ArchiveEntry*    entry = nullptr;
    const uint8_t*   ptr  = g_entryReader.data(g_session, index, &size, &entry);
    if (!ptr)
        return nullptr;

    if (androidDetectEntryType(entry->upperName(), size, ptr) != "Text")
        return nullptr;

    constexpr uint32_t kMaxPreview = 262144; // 256 KB
    const uint32_t     previewLen  = size < kMaxPreview ? size : kMaxPreview;

    std::string text = sanitizeAscii(ptr, previewLen);
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
    ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = g_entryReader.data(g_session, index, &size, &entry);
    if (!ptr)
        return nullptr;

    if (androidDetectEntryType(entry->upperName(), size, ptr) != "Palette")
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

    jintArray result = env->NewIntArray(2 + static_cast<jsize>(pixels.size()));
    if (!result)
        return nullptr;

    const jint header[2] = { kCols, rows };
    env->SetIntArrayRegion(result, 0, 2, header);
    env->SetIntArrayRegion(result, 2, static_cast<jsize>(pixels.size()), pixels.data());
    return result;
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
    ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = g_entryReader.data(g_session, index, &size, &entry);
    if (!ptr)
        return nullptr;

    const std::string type = androidDetectEntryType(entry->upperName(), size, ptr);
    if (type != "Flat" && type != "Doom Graphic")
        return nullptr;

    uint32_t       palSize = 0;
    const uint8_t* pal     = g_entryReader.findPalette(g_session, &palSize);
    if (!pal)
        return nullptr; // no PLAYPAL (or equivalent) anywhere in this archive

    if (type == "Flat")
    {
        // androidDetectEntryType() only returns "Flat" for exactly 4096 or
        // 4160 bytes -- both comfortably cover the 64x64 = 4096 pixels
        // slade_mobile::decodeFlat() reads, so no further size check needed here.
        std::vector<jint> pixels = slade_mobile::decodeFlat(ptr, size, pal);
        return packImage(env, 64, 64, pixels);
    }

    // Doom Graphic: androidDetectEntryType() already sanity-checked that
    // size >= 8 + 4*width and that the first column offset lands inside
    // the entry, but slade_mobile::decodeDoomGraphic() re-derives width from the header
    // itself and bounds-checks every column offset independently, so
    // there's no need to re-validate here.
    int w = 0, h = 0;
    std::vector<jint> pixels = slade_mobile::decodeDoomGraphic(ptr, size, pal, &w, &h);
    return packImage(env, w, h, pixels);
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
    ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = g_entryReader.data(g_session, index, &size, &entry);
    if (!ptr)
        return nullptr;

    if (androidDetectEntryType(entry->upperName(), size, ptr) != "PNG Image")
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
// lump types androidDetectEntryType() recognizes (see slade_mobile::audioInfoFor()
// above), or null if the entry isn't audio, the archive isn't open, or
// the index is invalid. Deliberately info-only, not playback -- see the
// comment above the parser block for why.
extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryAudioInfo(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t       size  = 0;
    ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = g_entryReader.data(g_session, index, &size, &entry);
    if (!ptr)
        return nullptr;

    const std::string type = androidDetectEntryType(entry->upperName(), size, ptr);
    const std::string info = slade_mobile::audioInfoFor(type, ptr, size);
    if (info.empty())
        return nullptr;

    return env->NewStringUTF(info.c_str());
}

// -----------------------------------------------------------------------
// Phase 7 (ROADMAP.md) -- WAD Editor MVP: first real edit operations.
// Scope for this pass: rename, delete, and Save As. Not yet done (left
// for a later pass, see comments at each function): Export/Import/
// Replace/Add/Move entry, and Phase 8's full "write to temp file, verify,
// then replace" safe-save flow -- Save As here writes straight to
// wherever the user points the SAF picker, which is still safe in the
// sense the ROADMAP cares about (the *original* WAD is never touched
// during editing -- Kotlin opens it read-only and this session never
// holds a writable fd to it) but doesn't yet cover overwriting the same
// file in place with a verify-before-replace step.
// -----------------------------------------------------------------------

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

    const bool ok = g_archiveOperations.rename(
            g_session, static_cast<unsigned>(index), nameChars);
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

    return g_archiveOperations.remove(
                   g_session, static_cast<unsigned>(index))
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

    return g_archiveOperations.move(
                   g_session,
                   static_cast<unsigned>(index),
                   static_cast<unsigned>(newPosition))
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

    if (!g_session.isOpen())
        return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    return slade_mobile::buildEntryListArray(env, g_session);
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

    return g_session.isDirty() ? JNI_TRUE : JNI_FALSE;
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
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeSaveToFd(
        JNIEnv* env,
        jobject /* this */,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    if (!g_session.isOpen())
    {
        close(fd);
        return JNI_FALSE;
    }

    // allowIwadOverwrite=true: see serializeSession()'s comment -- Save As
    // always writes to a brand-new file (MainActivity's saveAsLauncher
    // uses a separate SAF picker from the one the original was opened
    // through), so iwad_lock's protection against clobbering a real IWAD
    // in place doesn't apply to this code path.
    MemChunk out;
    if (!g_serializer.serialize(g_session, out, /*allowIwadOverwrite=*/true))
    {
        close(fd);
        return JNI_FALSE;
    }

    if (!slade_mobile::writeAllAndClose(fd, out.data(), out.size()))
        return JNI_FALSE;

    g_session.clearDirty();
    return JNI_TRUE;
}

// Phase 8 (Safe Save), step 1 of 2 -- see ArchiveSession::hasPendingSave()'s
// comment for the full reasoning on why this is split from step 2
// (nativeCommitSave) into two separate JNI calls instead of one function
// like nativeSaveToFd above. In short: Kotlin must not open the ORIGINAL
// document for writing until this step has already validated the new
// content, because opening a document in "rwt" mode truncates it
// immediately -- before either side has written a single byte -- so any
// validation done *after* that point would be validating over an already-
// destroyed original.
//
// Does the same serialize-and-prime work as nativeSaveToFd, but with
// allowIwadOverwrite=false (this WILL eventually overwrite the file the
// archive was opened from, unlike Save As -- exactly the case iwad_lock
// exists for), then re-parses the serialized bytes as a fresh WadArchive
// and sanity-checks entry count matches -- turning ROADMAP.md's "Reopen
// validation (пока только вручную)" into something automatic, at least
// for this one structural check.
//
// Returns null on success (with the validated bytes stashed in
// g_session's pendingSave for nativeCommitSave to pick up), or a non-null
// error string on failure -- distinct from the plain-boolean convention
// elsewhere, since "IWAD saving disabled" vs. "validation failed" vs. "no
// archive open" are meaningfully different things to tell the user here.
extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeValidateForSave(
        JNIEnv* env,
        jobject /* this */) {

    const char* error = g_saveCoordinator.validateForSave(g_session);
    return error ? env->NewStringUTF(error) : nullptr;
}

// Phase 8 (Safe Save), step 2 of 2 -- writes the bytes nativeValidateForSave
// already validated and stashed to `fd`. `fd` is expected to come from
// re-opening the ORIGINAL document (the one the archive was opened from)
// in "rwt" mode -- Kotlin's job is to have only done that AFTER step 1
// returned success, per this function's own file comment above. Closes
// fd itself, on both the success and failure path, same contract as
// nativeSaveToFd. Clears pendingSave either way (a half-written or
// failed attempt shouldn't be retried blind with stale bytes -- if it
// needs retrying, step 1 should run again first).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeCommitSave(
        JNIEnv* env,
        jobject /* this */,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    if (!g_session.isOpen() || !g_session.hasPendingSave())
    {
        close(fd);
        g_session.clearPendingSave();
        return JNI_FALSE;
    }

    MemChunk* out = g_session.pendingSave();
    if (!slade_mobile::writeAllAndClose(fd, out->data(), out->size()))
    {
        g_session.clearPendingSave();
        return JNI_FALSE;
    }

    g_session.clearPendingSave();
    g_session.clearDirty();
    return JNI_TRUE;
}

// Phase 8: Discard Changes. Thin wrapper -- see ArchiveSession::
// discardChanges()'s comment for why this is safe/cheap (mc_ is never
// mutated by any edit operation, so "undo everything" is just reparsing
// it fresh). Returns false if nothing is open; Kotlin doesn't need to
// distinguish that from "discard itself failed" since both mean nothing
// changed, and the button that triggers this is disabled when
// !archiveOpen anyway.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeDiscardChanges(
        JNIEnv* env,
        jobject /* this */) {

    return g_session.discardChanges() ? JNI_TRUE : JNI_FALSE;
}

// -----------------------------------------------------------------------
// Export / Add / Replace (ROADMAP.md's remaining Phase 7 items).
// -----------------------------------------------------------------------

// Reads the whole contents of `fd` into memory via mmap, then closes fd.
// Returns nullptr (with *outSize == 0) on any failure. Caller owns the
// mapping and must munmap() it at *outSize bytes once done -- same
// fstat-then-mmap pattern openWadFileFd() already uses for the original
// WAD, reused here since Add/Replace both just need a picked file's whole
// contents once, to hand to ArchiveEntry::importMem() (which immediately
// copies them into its own storage -- the mapping's job ends there).
// Exports the entry at `index`'s current bytes to `fd` (a SAF
// ACTION_CREATE_DOCUMENT result, same "rwt"/detachFd() contract as
// saveToFd()). Uses g_entryReader.data(g_session, ) -- NOT a raw offset lookup -- so
// exporting a just-Replaced-but-not-yet-saved entry exports its NEW
// content, not what used to be on disk.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeExportEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    uint32_t       size = 0;
    const uint8_t* data = g_entryReader.data(g_session, index, &size);
    if (!data)
    {
        close(fd);
        return JNI_FALSE;
    }

    return slade_mobile::writeAllAndClose(fd, data, size) ? JNI_TRUE : JNI_FALSE;
}

// Adds a brand-new entry named `name` at the end of the archive, with
// content read whole from `fd` (a SAF ACTION_OPEN_DOCUMENT result, "r"
// mode -- read-only, same detachFd() contract as the read path in
// openWadFileFd()). WadArchive::addEntry() sanitizes `name` itself (wad-
// friendly: 8 chars max, no extension) per its own doc comment, so
// whatever the picked file's display name was is passed through as-is;
// no need to duplicate that sanitization on the Kotlin side.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeAddEntry(
        JNIEnv* env,
        jobject /* this */,
        jstring name,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    if (!g_session.isOpen() || !name)
    {
        close(fd);
        return JNI_FALSE;
    }

    slade_mobile::MappedFile mapped;
    if (!slade_mobile::mapReadOnlyFd(fd, mapped))
        return JNI_FALSE;

    const char* nameChars = env->GetStringUTFChars(name, nullptr);
    if (!nameChars)
    {
        mapped.reset();
        return JNI_FALSE;
    }

    const bool ok = g_archiveOperations.add(
            g_session, nameChars, mapped.data, static_cast<uint32_t>(mapped.size));
    env->ReleaseStringUTFChars(name, nameChars);
    mapped.reset();
    return ok ? JNI_TRUE : JNI_FALSE;
}

// Replaces the entry at index using the selected file's whole contents.
// mmap/SAF fd ownership remains in JNI; the archive-domain mutation itself
// is delegated to ArchiveOperations.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeReplaceEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    if (!g_session.isOpen() || index < 0)
    {
        close(fd);
        return JNI_FALSE;
    }

    slade_mobile::MappedFile mapped;
    if (!slade_mobile::mapReadOnlyFd(fd, mapped))
        return JNI_FALSE;

    const bool ok = g_archiveOperations.replace(
            g_session,
            static_cast<unsigned>(index),
            mapped.data,
            static_cast<uint32_t>(mapped.size));
    mapped.reset();
    return ok ? JNI_TRUE : JNI_FALSE;
}