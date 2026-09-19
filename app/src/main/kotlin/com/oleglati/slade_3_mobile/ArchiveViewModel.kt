package com.oleglati.slade_3_mobile

import androidx.lifecycle.ViewModel

class ArchiveViewModel(
    private val repository: ArchiveRepository = ArchiveRepository()
) : ViewModel() {

    suspend fun greeting() = repository.greeting()

    suspend fun openWad(fd: Int) = repository.openWad(fd)

    suspend fun entryText(index: Int) = repository.entryText(index)
    suspend fun entryPalette(index: Int) = repository.entryPalette(index)
    suspend fun entryImage(index: Int) = repository.entryImage(index)
    suspend fun entryPng(index: Int) = repository.entryPng(index)
    suspend fun entryAudioInfo(index: Int) = repository.entryAudioInfo(index)

    suspend fun renameEntry(index: Int, newName: String) =
        repository.renameEntry(index, newName)

    suspend fun deleteEntry(index: Int) =
        repository.deleteEntry(index)

    suspend fun moveEntry(index: Int, newPosition: Int) =
        repository.moveEntry(index, newPosition)

    suspend fun listEntries() =
        repository.listEntries()

    suspend fun isDirty() =
        repository.isDirty()

    suspend fun saveToFd(fd: Int) =
        repository.saveToFd(fd)

    suspend fun validateForSave() =
        repository.validateForSave()

    suspend fun commitSave(fd: Int) =
        repository.commitSave(fd)

    suspend fun discardChanges() =
        repository.discardChanges()

    suspend fun exportEntry(index: Int, fd: Int) =
        repository.exportEntry(index, fd)

    suspend fun addEntry(name: String, fd: Int) =
        repository.addEntry(name, fd)

    suspend fun replaceEntry(index: Int, fd: Int) =
        repository.replaceEntry(index, fd)
}
