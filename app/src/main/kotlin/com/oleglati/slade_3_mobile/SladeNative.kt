package com.oleglati.slade_3_mobile

import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.withContext
import java.util.concurrent.Executors

// Все JNI-вызовы выполняются через один выделенный поток. Native layer хранит
// изменяемое состояние открытой ArchiveSession, поэтому последовательный
// доступ сохраняет инвариант: одновременно только один поток работает с
// native-состоянием.
private val nativeExecutor = Executors.newSingleThreadExecutor { runnable ->
    Thread(runnable, "slade-native")
}
private val nativeDispatcher = nativeExecutor.asCoroutineDispatcher()

// Единый результат для native-вызовов. Существующие native-контракты с
// null/"ERROR:" сохраняются, а Kotlin получает единый способ обработать
// успешный и ошибочный результат.
sealed class NativeResult<out T> {
    data class Success<T>(val value: T) : NativeResult<T>()
    data class Failure(val message: String) : NativeResult<Nothing>()
}

private suspend fun <T> callNative(block: () -> T): NativeResult<T> =
    withContext(nativeDispatcher) {
        try {
            NativeResult.Success(block())
        } catch (e: Throwable) {
            NativeResult.Failure(e.message ?: e.javaClass.simpleName)
        }
    }

// Тонкие suspend-обёртки над external fun. Все вызовы проходят через
// callNative()/nativeDispatcher и не вызывают JNI напрямую из UI-потока.
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

    // Мутирующие операции изменяют список записей. После них вызывающая
    // сторона должна обновить список через listEntries(), потому что
    // индексы последующих записей могут измениться.
    suspend fun renameEntry(index: Int, newName: String): NativeResult<Boolean> =
        callNative { nativeRenameEntry(index, newName) }

    suspend fun deleteEntry(index: Int): NativeResult<Boolean> =
        callNative { nativeDeleteEntry(index) }

    suspend fun moveEntry(index: Int, newPosition: Int): NativeResult<Boolean> =
        callNative { nativeMoveEntry(index, newPosition) }

    suspend fun listEntries(): NativeResult<Array<String>> =
        callNative { nativeListEntries() }

    suspend fun isDirty(): NativeResult<Boolean> =
        callNative { nativeIsDirty() }

    // Save As: записывает уже подготовленное состояние в явно выбранный
    // пользователем новый файл. Для сохранения исходного документа на месте
    // используется отдельная двухшаговая схема validateForSave() → commitSave().
    suspend fun saveToFd(fd: Int): NativeResult<Boolean> =
        callNative { nativeSaveToFd(fd) }

    // Шаг 1 сохранения на месте: сериализация и валидация полностью в памяти.
    // При успехе валидированные байты сохраняются native-side для следующего
    // шага. Этот вызов должен завершиться до открытия исходного SAF-документа
    // в режиме "rwt", поскольку такой режим может немедленно обрезать файл.
    suspend fun validateForSave(): NativeResult<String?> =
        callNative { nativeValidateForSave() }

    // Шаг 2 сохранения на месте: записывает байты, которые уже прошли
    // validateForSave(), в исходный fd, открытый в "rwt" после успешной
    // валидации.
    //
    // Ограничение текущего SAF-пути: это не атомарная замена файла. При
    // прерывании записи исходный документ может оказаться частично записанным.
    suspend fun commitSave(fd: Int): NativeResult<Boolean> =
        callNative { nativeCommitSave(fd) }

    // Discard Changes: native-сессия заново разбирает исходные данные и
    // отбрасывает несохранённые изменения. После успешного вызова список
    // записей следует обновить через listEntries().
    suspend fun discardChanges(): NativeResult<Boolean> =
        callNative { nativeDiscardChanges() }

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
