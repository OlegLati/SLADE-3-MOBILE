package com.oleglati.slade_3_mobile

// Application-level archive boundary between the Activity/UI and the JNI
// facade. The repository deliberately contains no Android UI code and no
// archive state of its own: native session state remains owned by
// SladeNative/native code. This makes it possible to move orchestration into
// a ViewModel without exposing JNI details to the UI layer.
class ArchiveRepository {

    suspend fun greeting() = SladeNative.greeting()

    suspend fun openWad(fd: Int) = SladeNative.openWad(fd)

    suspend fun entryText(index: Int) = SladeNative.entryText(index)
    suspend fun entryPalette(index: Int) = SladeNative.entryPalette(index)
    suspend fun entryImage(index: Int) = SladeNative.entryImage(index)
    suspend fun entryPng(index: Int) = SladeNative.entryPng(index)
    suspend fun entryAudioInfo(index: Int) = SladeNative.entryAudioInfo(index)

    suspend fun renameEntry(index: Int, newName: String) =
        SladeNative.renameEntry(index, newName)

    suspend fun deleteEntry(index: Int) =
        SladeNative.deleteEntry(index)

    suspend fun moveEntry(index: Int, newPosition: Int) =
        SladeNative.moveEntry(index, newPosition)

    suspend fun listEntries() =
        SladeNative.listEntries()

    suspend fun isDirty() =
        SladeNative.isDirty()

    suspend fun saveToFd(fd: Int) =
        SladeNative.saveToFd(fd)

    suspend fun validateForSave() =
        SladeNative.validateForSave()

    suspend fun commitSave(fd: Int) =
        SladeNative.commitSave(fd)

    suspend fun discardChanges() =
        SladeNative.discardChanges()

    suspend fun exportEntry(index: Int, fd: Int) =
        SladeNative.exportEntry(index, fd)

    suspend fun addEntry(name: String, fd: Int) =
        SladeNative.addEntry(name, fd)

    suspend fun replaceEntry(index: Int, fd: Int) =
        SladeNative.replaceEntry(index, fd)
}
