package com.oleglati.slade_3_mobile

import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.withContext
import java.util.concurrent.Executors

// Phase 5 (background processing): every JNI call used to run directly on
// the UI thread from MainActivity's click handlers. openWadFileFd() parses
// the *entire* WAD directory and runs androidDetectEntryType() on every
// entry up front (see native-lib.cpp) -- on a large WAD (thousands of
// entries) that's enough work to visibly freeze the UI thread, and any
// per-entry preview decode (Doom Graphic/Flat especially, which walks raw
// pixel data through a palette in C++) adds more of the same on every tap.
//
// Why ONE background thread and not a thread pool / Dispatchers.IO:
// native-lib.cpp keeps the open archive as global C++ state and says so
// explicitly -- "Not thread-safe, but all calls come from the UI thread
// via one Activity, so that's moot." Moving calls off the UI thread would
// break that invariant if two native calls could ever run concurrently
// (e.g. a slow archive open racing a tap-triggered entry preview). Routing
// every native call through a single dedicated thread preserves the
// original invariant -- "only one thread ever touches native state at a
// time" -- it's just no longer required to be the UI thread specifically.
private val nativeExecutor = Executors.newSingleThreadExecutor { runnable ->
    Thread(runnable, "slade-native")
}
private val nativeDispatcher = nativeExecutor.asCoroutineDispatcher()

// Sealed outcome instead of relying purely on the existing null/"ERROR:"
// prefix conventions from native-lib.cpp. Those conventions are kept as-is
// on the native side (not touched here) -- this just gives call sites one
// consistent way to also catch anything that slips past them, e.g. an
// unexpected exception thrown while marshaling a JNI return value, or an
// OutOfMemoryError from a very large decoded bitmap. Before this wrapper,
// either of those would have crashed the app instead of surfacing as an
// error the user can dismiss.
sealed class NativeResult<out T> {
    data class Success<T>(val value: T) : NativeResult<T>()
    data class Failure(val message: String) : NativeResult<Nothing>()
}

private suspend fun <T> callNative(block: () -> T): NativeResult<T> =
    withContext(nativeDispatcher) {
        try {
            NativeResult.Success(block())
        } catch (e: Throwable) {
            // Deliberately catches Throwable, not just Exception: an
            // OutOfMemoryError decoding a huge bitmap is an Error, not an
            // Exception, and should still surface as a dismissable message
            // rather than take the whole process down.
            NativeResult.Failure(e.message ?: e.javaClass.simpleName)
        }
    }

// Thin suspend wrappers around the raw `external fun` declarations. Kept
// in one object (rather than back on MainActivity) so every call site is
// forced through callNative()/nativeDispatcher -- there's no direct path
// left to call the native functions from the UI thread by accident.
object SladeNative {

    init {
        System.loadLibrary("myapplication")
    }

    suspend fun greeting(): NativeResult<String> =
        callNative { stringFromJNI() }

    suspend fun openWad(fd: Int): NativeResult<Array<String>> =
        callNative { openWadFileFd(fd) }

    suspend fun entryText(index: Int): NativeResult<String?> =
        callNative { getEntryText(index) }

    suspend fun entryPalette(index: Int): NativeResult<IntArray?> =
        callNative { getEntryPalette(index) }

    suspend fun entryImage(index: Int): NativeResult<IntArray?> =
        callNative { getEntryImage(index) }

    suspend fun entryPng(index: Int): NativeResult<ByteArray?> =
        callNative { getEntryPng(index) }

    suspend fun entryAudioInfo(index: Int): NativeResult<String?> =
        callNative { getEntryAudioInfo(index) }

    // Phase 7 (WAD Editor MVP): rename/delete mutate the session's entry
    // list, and removeEntry() shifts every later index down by one (see
    // native-lib.cpp's comment on deleteEntry()) -- callers MUST reload
    // via listEntries() afterward rather than patch a cached list, or a
    // later tap will read the wrong entry.
    //
    // NOTE: the private `external fun`s below use a `native`-prefixed name
    // distinct from their suspend wrapper (nativeRenameEntry vs
    // renameEntry, etc.) -- matching entryText()/getEntryText() and the
    // rest above. A same-named suspend/non-suspend pair would probably
    // still resolve correctly here (the non-suspend one is the only
    // candidate callable from callNative()'s non-suspend lambda), but
    // that's relying on a subtlety worth just not needing to reason about.
    suspend fun renameEntry(index: Int, newName: String): NativeResult<Boolean> =
        callNative { nativeRenameEntry(index, newName) }

    suspend fun deleteEntry(index: Int): NativeResult<Boolean> =
        callNative { nativeDeleteEntry(index) }

    // Phase 7: the last remaining WAD Editor MVP operation (Move entry).
    // `newPosition` is caller-computed (index -1/+1 for the up/down
    // buttons) -- see native-lib.cpp's nativeMoveEntry comment for why
    // that's an insertion index into the list AFTER the entry is removed,
    // not a swap-target in the original list, and why that distinction
    // only stays invisible for this exact +-1 adjacent-move case.
    suspend fun moveEntry(index: Int, newPosition: Int): NativeResult<Boolean> =
        callNative { nativeMoveEntry(index, newPosition) }

    suspend fun listEntries(): NativeResult<Array<String>> =
        callNative { nativeListEntries() }

    suspend fun isDirty(): NativeResult<Boolean> =
        callNative { nativeIsDirty() }

    // Save As only for now -- see native-lib.cpp's file comment above
    // saveToFd() for why this isn't yet Phase 8's full safe-save (write
    // to temp file, verify, then replace the original).
    suspend fun saveToFd(fd: Int): NativeResult<Boolean> =
        callNative { nativeSaveToFd(fd) }

    // Phase 8 (Safe Save), step 1 of 2: serializes + validates entirely
    // in memory, touching nothing on disk. Returns null on success
    // (validated bytes are stashed native-side for step 2), or a
    // human-readable error string otherwise -- e.g. "IWAD saving
    // disabled" if the opened archive is a real IWAD, which is a
    // meaningfully different message from a generic save failure. MUST
    // complete successfully BEFORE the caller opens the original
    // document in "rwt" mode for step 2 -- see MainActivity.saveInPlace()
    // and native-lib.cpp's nativeValidateForSave comment for why that
    // ordering isn't optional (opening a document "rwt" truncates it
    // immediately, not when bytes are actually written).
    suspend fun validateForSave(): NativeResult<String?> =
        callNative { nativeValidateForSave() }

    // Phase 8, step 2 of 2: writes what step 1 already validated to `fd`
    // -- expected to be the ORIGINAL document's fd, opened "rwt" only
    // after step 1 returned success.
    suspend fun commitSave(fd: Int): NativeResult<Boolean> =
        callNative { nativeCommitSave(fd) }

    // Phase 8: Discard Changes. Re-parses the session from its own
    // untouched original bytes (see native-lib.cpp's
    // ArchiveSession::discardChanges() comment) -- the archive stays
    // open, only the edits are thrown away. Callers must refresh via
    // listEntries() afterward, same as every other edit operation.
    suspend fun discardChanges(): NativeResult<Boolean> =
        callNative { nativeDiscardChanges() }

    // Export/Add/Replace (remaining Phase 7 items). Add/Replace both
    // mutate the entry list/content the same way rename/delete do --
    // reload via listEntries() afterward, same reasoning as those two.
    suspend fun exportEntry(index: Int, fd: Int): NativeResult<Boolean> =
        callNative { nativeExportEntry(index, fd) }

    suspend fun addEntry(name: String, fd: Int): NativeResult<Boolean> =
        callNative { nativeAddEntry(name, fd) }

    suspend fun replaceEntry(index: Int, fd: Int): NativeResult<Boolean> =
        callNative { nativeReplaceEntry(index, fd) }

    private external fun stringFromJNI(): String
    private external fun openWadFileFd(fd: Int): Array<String>
    private external fun getEntryText(index: Int): String?
    private external fun getEntryPalette(index: Int): IntArray?
    private external fun getEntryImage(index: Int): IntArray?
    private external fun getEntryPng(index: Int): ByteArray?
    private external fun getEntryAudioInfo(index: Int): String?
    private external fun nativeRenameEntry(index: Int, newName: String): Boolean
    private external fun nativeDeleteEntry(index: Int): Boolean
    private external fun nativeMoveEntry(index: Int, newPosition: Int): Boolean
    private external fun nativeListEntries(): Array<String>
    private external fun nativeIsDirty(): Boolean
    private external fun nativeSaveToFd(fd: Int): Boolean
    private external fun nativeValidateForSave(): String?
    private external fun nativeCommitSave(fd: Int): Boolean
    private external fun nativeDiscardChanges(): Boolean
    private external fun nativeExportEntry(index: Int, fd: Int): Boolean
    private external fun nativeAddEntry(name: String, fd: Int): Boolean
    private external fun nativeReplaceEntry(index: Int, fd: Int): Boolean
}
