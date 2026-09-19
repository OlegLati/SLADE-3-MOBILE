package com.oleglati.slade_3_mobile

import android.app.AlertDialog
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Typeface
import android.os.Bundle
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import android.widget.EditText
import android.widget.ImageView
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.lifecycle.lifecycleScope
import androidx.recyclerview.widget.LinearLayoutManager
import com.oleglati.slade_3_mobile.databinding.ActivityMainBinding
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private val archiveRepository = ArchiveRepository()
    private val adapter = WadEntryAdapter(
        onEntryClick = ::onEntryClicked,
        onEntryLongClick = ::onEntryLongClicked
    )

    // Phase 7: whether a WAD is currently open in the native session --
    // drives Save As's enabled state. Not the same as "adapter has
    // entries": a validly-opened empty WAD (0 lumps, like stringFromJNI()'s
    // hand-built test WAD) is still "open" and, once edits exist, still
    // worth being able to save.
    private var archiveOpen = false

    // Phase 8: mirrors archiveRepository.isDirty() so Save/Discard's enabled
    // state (see updateActionButtonsEnabled()) doesn't need its own
    // suspend round-trip on every button-state refresh -- refreshed
    // alongside the "*" indicator in refreshDirtyIndicator(), and reset
    // directly (not via isDirty()) on a fresh open, same reasoning as
    // archiveOpen just above: nothing to round-trip for, it's known false.
    private var isDirty = false

    // Phase 8 (Safe Save): which document the currently-open archive came
    // from, so "Save" (unlike Save As, which always picks a brand-new
    // destination) knows what to eventually overwrite. Only meaningful
    // while archiveOpen is true -- see saveInPlace().
    private var openedWadUri: android.net.Uri? = null

    // Phase 5: only one of these is ever meaningfully "in flight" at a
    // time from the user's perspective (you can't open a new WAD while
    // browsing one, and tapping a second entry means you're no longer
    // interested in the first preview). Cancelling the previous Job before
    // starting a new one means a stale result can't land after a newer
    // request -- e.g. tap entry A, immediately tap entry B: A's dialog
    // must not pop up after B's. This only pre-empts work that hasn't
    // reached the native call yet (see archiveRepository.kt) -- once a native
    // call has actually started on its dedicated thread, cancellation
    // can't interrupt it mid-flight, it just gets ignored on completion.
    private var wadLoadJob: Job? = null
    private var entryLoadJob: Job? = null

    // NOTE: entryList.isEnabled (toggled in setBusy()) is purely cosmetic
    // here -- RecyclerView being a ViewGroup, disabling it does not cascade
    // down to each item view's own click listener (see WadEntryAdapter,
    // which calls itemView.setOnClickListener directly). This flag is the
    // actual guard against a tap being processed while busy.
    private var isBusy = false

    // Export/Replace launchers below are entry-scoped, but ActivityResult
    // callbacks take no arguments -- these remember which entry triggered
    // the picker so the callback knows what to act on once it returns.
    private var pendingExportEntry: WadEntry? = null
    private var pendingReplaceEntry: WadEntry? = null

    // System file picker (Storage Access Framework) -- works under scoped
    // storage without needing broad storage permissions, since the user
    // explicitly picks the file.
    //
    // CHANGED (memory safety): used to be
    // contentResolver.openInputStream(uri)?.use { it.readBytes() }, which
    // reads the WHOLE file into a Kotlin ByteArray on the JVM heap, which
    // then gets copied again crossing the JNI boundary (GetByteArrayElements
    // isn't guaranteed to just hand over a pointer -- it can copy), before
    // native code makes its own MemChunk copy on top of that. For a small
    // vanilla IWAD that's nothing, but Sigil/Sigil2's Buckethead/Thorr
    // soundtrack editions run 50-150MB (real audio replacing MUS lumps),
    // and tripling that was almost certainly what was crashing the app.
    // Getting a real file descriptor and mmap-ing it directly in native
    // code (see native-lib.cpp) cuts that down to essentially one copy.
    private val openWadLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@registerForActivityResult

            // Phase 8: remembered regardless of whether the open below
            // succeeds -- harmless either way, since Save's enabled state
            // is gated on archiveOpen (set only on a successful open), not
            // on this being non-null.
            openedWadUri = uri

            binding.fileNameText.text = queryDisplayName(uri)

            val pfd: ParcelFileDescriptor? = try {
                contentResolver.openFileDescriptor(uri, "r")
            } catch (e: Exception) {
                null
            }
            if (pfd == null) {
                binding.statusText.text = "Could not open file"
                adapter.submitList(emptyList())
                return@registerForActivityResult
            }

            // Read size BEFORE detaching -- once detached, the
            // ParcelFileDescriptor object itself must not be touched again
            // (its docs explicitly say not to call close() on it either;
            // ownership of the raw fd has fully passed to native code,
            // which is responsible for closing it once done).
            val fileSizeBytes = pfd.statSize.toInt()
            val fd = pfd.detachFd()

            // Cancel any previous in-flight open (e.g. user backed out of
            // the picker and immediately picked another file) before
            // starting the new one -- see the Job field comments above.
            wadLoadJob?.cancel()
            wadLoadJob = lifecycleScope.launch {
                setBusy(true, "Opening WAD…")
                when (val result = archiveRepository.openWad(fd)) {
                    is NativeResult.Success -> handleWadResult(fileSizeBytes, result.value)
                    is NativeResult.Failure -> {
                        archiveOpen = false
                        isDirty = false
                        binding.statusText.text = "File size: $fileSizeBytes bytes\nERROR: ${result.message}"
                        adapter.submitList(emptyList())
                    }
                }
                setBusy(false)
            }
        }

    // Phase 7: Save As. CreateDocument always hands back a fresh/truncated
    // file, so "rwt" (not "rw") -- matches saveToFd()'s expectation on the
    // native side. Same detachFd()/ownership-transfer pattern as the read
    // path above; native code closes fd itself once done either way.
    private val saveAsLauncher =
        registerForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
            if (uri == null) return@registerForActivityResult

            val pfd: ParcelFileDescriptor? = try {
                contentResolver.openFileDescriptor(uri, "rwt")
            } catch (e: Exception) {
                null
            }
            if (pfd == null) {
                Toast.makeText(this, "Не удалось открыть файл для записи", Toast.LENGTH_SHORT).show()
                return@registerForActivityResult
            }
            val fd = pfd.detachFd()

            wadLoadJob?.cancel()
            wadLoadJob = lifecycleScope.launch {
                setBusy(true, "Saving…")
                val saved = when (val result = archiveRepository.saveToFd(fd)) {
                    is NativeResult.Success -> result.value
                    is NativeResult.Failure -> {
                        Toast.makeText(this@MainActivity, "Ошибка сохранения: ${result.message}", Toast.LENGTH_SHORT).show()
                        false
                    }
                }
                setBusy(false)
                Toast.makeText(
                    this@MainActivity,
                    if (saved) "Сохранено" else "Не удалось сохранить",
                    Toast.LENGTH_SHORT
                ).show()
                if (saved) refreshDirtyIndicator()
            }
        }

    // Phase 8 (Safe Save): "Save" -- overwrites openedWadUri in place,
    // unlike Save As above (always a brand-new SAF destination). Two
    // native calls in a fixed order, not one: nativeValidateForSave() must
    // fully succeed BEFORE this ever opens the original document, because
    // opening it in "rwt" mode truncates it the instant the call is made
    // -- independent of whether or when any bytes actually get written
    // afterward. If validation fails, the original file is never even
    // opened for writing, so it's byte-for-byte untouched.
    //
    // No picker here (unlike Save As/Export/Replace) -- openedWadUri was
    // captured back when the file was opened, there's nothing for the
    // user to choose.
    private fun saveInPlace() {
        val uri = openedWadUri ?: return

        wadLoadJob?.cancel()
        wadLoadJob = lifecycleScope.launch {
            setBusy(true, "Проверка перед сохранением…")

            val validationError = when (val result = archiveRepository.validateForSave()) {
                is NativeResult.Success -> result.value // null == validation passed
                is NativeResult.Failure -> result.message
            }
            if (validationError != null) {
                setBusy(false)
                Toast.makeText(this@MainActivity, "Не удалось сохранить: $validationError", Toast.LENGTH_LONG).show()
                return@launch
            }

            // Past this point the original is about to be truncated --
            // validation above is what makes that safe to do.
            val pfd: ParcelFileDescriptor? = try {
                contentResolver.openFileDescriptor(uri, "rwt")
            } catch (e: Exception) {
                null
            }
            if (pfd == null) {
                setBusy(false)
                Toast.makeText(this@MainActivity, "Не удалось открыть файл для записи", Toast.LENGTH_SHORT).show()
                return@launch
            }
            val fd = pfd.detachFd()

            val saved = when (val result = archiveRepository.commitSave(fd)) {
                is NativeResult.Success -> result.value
                is NativeResult.Failure -> {
                    Toast.makeText(this@MainActivity, "Ошибка сохранения: ${result.message}", Toast.LENGTH_SHORT).show()
                    false
                }
            }
            setBusy(false)
            Toast.makeText(
                this@MainActivity,
                if (saved) "Сохранено" else "Не удалось сохранить",
                Toast.LENGTH_SHORT
            ).show()
            if (saved) refreshDirtyIndicator()
        }
    }

    // Export: same CreateDocument/"rwt"/detachFd() contract as Save As,
    // just for one entry's bytes instead of the whole archive. Uses
    // pendingExportEntry rather than a captured local since the picker
    // callback fires well after onEntryLongClicked returns.
    private val exportLauncher =
        registerForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
            val entry = pendingExportEntry
            pendingExportEntry = null
            if (uri == null || entry == null) return@registerForActivityResult

            val pfd: ParcelFileDescriptor? = try {
                contentResolver.openFileDescriptor(uri, "rwt")
            } catch (e: Exception) {
                null
            }
            if (pfd == null) {
                Toast.makeText(this, "Не удалось открыть файл для записи", Toast.LENGTH_SHORT).show()
                return@registerForActivityResult
            }
            val fd = pfd.detachFd()

            entryLoadJob?.cancel()
            entryLoadJob = lifecycleScope.launch {
                setBusy(true)
                val ok = when (val result = archiveRepository.exportEntry(entry.index, fd)) {
                    is NativeResult.Success -> result.value
                    is NativeResult.Failure -> {
                        Toast.makeText(this@MainActivity, "Ошибка: ${result.message}", Toast.LENGTH_SHORT).show()
                        false
                    }
                }
                setBusy(false)
                Toast.makeText(
                    this@MainActivity,
                    if (ok) "Экспортировано" else "Не удалось экспортировать",
                    Toast.LENGTH_SHORT
                ).show()
            }
        }

    // Replace: picks a file to read whole via OpenDocument/"r"/detachFd()
    // (same contract as openWadLauncher), overwriting pendingReplaceEntry's
    // content in place. Refreshes the list afterward -- size (shown per
    // entry) may have changed even though the index and name didn't.
    private val replaceLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            val entry = pendingReplaceEntry
            pendingReplaceEntry = null
            if (uri == null || entry == null) return@registerForActivityResult

            val pfd: ParcelFileDescriptor? = try {
                contentResolver.openFileDescriptor(uri, "r")
            } catch (e: Exception) {
                null
            }
            if (pfd == null) {
                Toast.makeText(this, "Не удалось открыть файл", Toast.LENGTH_SHORT).show()
                return@registerForActivityResult
            }
            val fd = pfd.detachFd()

            entryLoadJob?.cancel()
            entryLoadJob = lifecycleScope.launch {
                setBusy(true)
                val ok = when (val result = archiveRepository.replaceEntry(entry.index, fd)) {
                    is NativeResult.Success -> result.value
                    is NativeResult.Failure -> {
                        Toast.makeText(this@MainActivity, "Ошибка: ${result.message}", Toast.LENGTH_SHORT).show()
                        false
                    }
                }
                if (ok) refreshEntryList()
                else Toast.makeText(this@MainActivity, "Не удалось заменить", Toast.LENGTH_SHORT).show()
                setBusy(false)
            }
        }

    // Add: picks a file to read whole, same contract as Replace above, but
    // appends a brand-new entry instead of overwriting an existing one --
    // so unlike Export/Replace it isn't tied to a pending entry, and needs
    // a name prompt (suggested from the picked file's own display name)
    // rather than reusing one that's already there. See
    // showAddEntryNameDialog() for the prompt, called from here once a
    // file's picked.
    private var pendingAddFd: Int? = null

    private val addEntryLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@registerForActivityResult

            val suggestedName = queryDisplayName(uri)
                .substringBeforeLast('.') // WAD lump names carry no extension
                .uppercase()
                .take(8) // WadArchive's own directory format: 8 bytes per name

            val pfd: ParcelFileDescriptor? = try {
                contentResolver.openFileDescriptor(uri, "r")
            } catch (e: Exception) {
                null
            }
            if (pfd == null) {
                Toast.makeText(this, "Не удалось открыть файл", Toast.LENGTH_SHORT).show()
                return@registerForActivityResult
            }
            pendingAddFd = pfd.detachFd()
            showAddEntryNameDialog(suggestedName)
        }

    private fun showAddEntryNameDialog(suggestedName: String) {
        val input = EditText(this).apply {
            setText(suggestedName)
            setSelection(text.length)
        }
        AlertDialog.Builder(this)
            .setTitle("Имя новой entry")
            .setView(input)
            .setPositiveButton("Добавить") { _, _ ->
                val fd = pendingAddFd
                pendingAddFd = null
                val name = input.text.toString().trim()
                if (fd == null) return@setPositiveButton
                if (name.isEmpty()) {
                    Toast.makeText(this, "Имя не может быть пустым", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }

                entryLoadJob?.cancel()
                entryLoadJob = lifecycleScope.launch {
                    setBusy(true)
                    val ok = when (val result = archiveRepository.addEntry(name, fd)) {
                        is NativeResult.Success -> result.value
                        is NativeResult.Failure -> {
                            Toast.makeText(this@MainActivity, "Ошибка: ${result.message}", Toast.LENGTH_SHORT).show()
                            false
                        }
                    }
                    if (ok) refreshEntryList()
                    else Toast.makeText(this@MainActivity, "Не удалось добавить", Toast.LENGTH_SHORT).show()
                    setBusy(false)
                }
            }
            .setNegativeButton("Отмена") { _, _ ->
                // Cancelling here leaves pendingAddFd's underlying fd
                // dangling (never handed to native code, so never closed
                // there) -- close it directly so it isn't leaked.
                pendingAddFd?.let { ParcelFileDescriptor.adoptFd(it).close() }
                pendingAddFd = null
            }
            .setOnCancelListener {
                // Dismissed by tapping outside rather than either button --
                // same leak as the negative-button path above, same fix.
                pendingAddFd?.let { ParcelFileDescriptor.adoptFd(it).close() }
                pendingAddFd = null
            }
            .show()
    }

    // Phase 5 "progress state": no real percentage is available (see the
    // ProgressBar comment in activity_main.xml), so this just toggles
    // between "idle" and "working" -- disabling the open button and entry
    // list prevents a second native call from being queued behind the
    // first (which would otherwise mean tapping around during a slow open
    // silently piles up work rather than doing nothing).
    private fun setBusy(busy: Boolean, statusWhileBusy: String? = null) {
        isBusy = busy
        binding.progressBar.visibility = if (busy) android.view.View.VISIBLE else android.view.View.GONE
        binding.btnOpenWad.isEnabled = !busy
        binding.btnSaveAs.isEnabled = !busy && archiveOpen
        binding.btnAddEntry.isEnabled = !busy && archiveOpen
        binding.entryList.isEnabled = !busy // cosmetic only, see isBusy field comment
        updateActionButtonsEnabled()
        if (busy && statusWhileBusy != null) {
            binding.statusText.text = statusWhileBusy
        }
    }

    // Phase 8: single source of truth for Save/Discard's enabled state,
    // called both from setBusy() (busy/archiveOpen changed) and from
    // refreshDirtyIndicator() (isDirty changed on its own, independent of
    // any busy transition) -- rather than duplicating this condition in
    // both places and risking them drifting apart. Both buttons share the
    // same condition: there's nothing for either to do on a clean or
    // closed archive.
    private fun updateActionButtonsEnabled() {
        val enabled = !isBusy && archiveOpen && isDirty
        binding.btnSave.isEnabled = enabled
        binding.btnDiscard.isEnabled = enabled
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.entryList.layoutManager = LinearLayoutManager(this)
        binding.entryList.adapter = adapter

        lifecycleScope.launch {
            binding.statusText.text = when (val result = archiveRepository.greeting()) {
                is NativeResult.Success -> result.value
                is NativeResult.Failure -> result.message
            }
        }

        binding.btnOpenWad.setOnClickListener {
            // "*/*" because .wad has no standard MIME type -- the system
            // picker will still let the user browse to and select any file.
            openWadLauncher.launch(arrayOf("*/*"))
        }

        binding.btnSaveAs.setOnClickListener {
            saveAsLauncher.launch("edited.wad")
        }

        binding.btnAddEntry.setOnClickListener {
            addEntryLauncher.launch(arrayOf("*/*"))
        }

        binding.btnSave.setOnClickListener { saveInPlace() }
        binding.btnDiscard.setOnClickListener { confirmDiscardChanges() }
    }

    // JNI now returns one array element per WAD entry, formatted as
    // "name\tsize\ttype" (see native-lib.cpp) -- type comes from
    // androidDetectEntryType(), a standalone byte-signature/name classifier
    // in compat/slade_shims.cpp (not the real EntryType engine, which needs
    // ZIP-based resource archive support we haven't ported yet). On
    // failure it instead returns a single-element array whose entry
    // starts with "ERROR: ".
    private fun handleWadResult(fileSizeBytes: Int, rawLines: Array<String>) {
        if (rawLines.size == 1 && rawLines[0].startsWith("ERROR:")) {
            archiveOpen = false
            isDirty = false
            binding.statusText.text = "File size: $fileSizeBytes bytes\n${rawLines[0]}"
            adapter.submitList(emptyList())
            updateActionButtonsEnabled()
            return
        }

        archiveOpen = true
        // A freshly-opened archive has no edits yet -- no need to round-trip
        // through archiveRepository.isDirty() just to confirm what's already known.
        isDirty = false
        val entries = parseEntryLines(rawLines)
        binding.statusText.text = "File size: $fileSizeBytes bytes — ${entries.size} entries"
        adapter.submitList(entries)
        binding.fileNameText.text = binding.fileNameText.text.toString().removeSuffix(" *")
        updateActionButtonsEnabled()
    }

    private fun parseEntryLines(rawLines: Array<String>): List<WadEntry> =
        rawLines.mapIndexed { index, line ->
            val parts = line.split("\t")
            val name = parts.getOrElse(0) { "?" }
            val size = parts.getOrElse(1) { "0" }.toLongOrNull() ?: 0L
            val type = parts.getOrElse(2) { "Unknown" }
            WadEntry(index, name, size, type)
        }

    // Phase 7: called after every edit (rename/delete) and after a
    // successful Save. Re-lists from native rather than patching the
    // adapter locally -- see native-lib.cpp's comment on deleteEntry() for
    // why indices can't be trusted to stay stable across an edit -- and
    // separately polls isDirty() to keep the "*" indicator honest (a
    // rename to the same name, for instance, is a no-op SLADE may not
    // consider a real change).
    private suspend fun refreshEntryList() {
        when (val result = archiveRepository.listEntries()) {
            is NativeResult.Success -> adapter.submitList(parseEntryLines(result.value))
            is NativeResult.Failure ->
                Toast.makeText(this, "Ошибка обновления списка: ${result.message}", Toast.LENGTH_SHORT).show()
        }
        refreshDirtyIndicator()
    }

    private suspend fun refreshDirtyIndicator() {
        isDirty = (archiveRepository.isDirty() as? NativeResult.Success)?.value ?: false
        val base = binding.fileNameText.text.toString().removeSuffix(" *")
        binding.fileNameText.text = if (isDirty) "$base *" else base
        updateActionButtonsEnabled()
    }

    // Content URIs from the system file picker don't carry a plain
    // filename directly -- the display name has to be queried from the
    // ContentResolver via the standard OpenableColumns contract.
    private fun queryDisplayName(uri: android.net.Uri): String {
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
            val nameIndex = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
            if (nameIndex >= 0 && cursor.moveToFirst()) {
                cursor.getString(nameIndex)?.let { return it }
            }
        }
        return "Unknown file"
    }

    // Phase 7 (WAD Editor MVP): long-press context menu. Same isBusy guard
    // as onEntryClicked -- an edit mid-open/mid-save would race the native
    // session the same way a preview tap would.
    private fun onEntryLongClicked(entry: WadEntry) {
        if (isBusy) return
        AlertDialog.Builder(this)
            .setTitle(entry.name)
            .setItems(
                arrayOf(
                    "Переименовать", "Удалить", "Экспортировать", "Заменить",
                    "Переместить вверх", "Переместить вниз"
                )
            ) { _, which ->
                when (which) {
                    0 -> showRenameDialog(entry)
                    1 -> confirmDelete(entry)
                    2 -> {
                        pendingExportEntry = entry
                        exportLauncher.launch(entry.name)
                    }
                    3 -> {
                        pendingReplaceEntry = entry
                        replaceLauncher.launch(arrayOf("*/*"))
                    }
                    4 -> moveEntryBy(entry, -1)
                    5 -> moveEntryBy(entry, 1)
                }
            }
            .show()
    }

    // Phase 7 (WAD Editor MVP): the last remaining operation, backed by
    // WadArchive::moveEntry() -- see native-lib.cpp's nativeMoveEntry
    // comment for the full "position is post-removal, not pre-removal"
    // gotcha. Buttons only do adjacent +-1 moves (no arbitrary
    // drag-and-drop target yet, see ROADMAP.md), so `newPosition = index
    // + delta` is always correct here without any further adjustment --
    // that stops being true the moment a caller passes a non-adjacent
    // target, so don't reuse this signature as-is for drag-and-drop later.
    //
    // Bounds-checked here rather than just letting the native call fail
    // silently on an invalid position: the top/bottom entry not having a
    // corresponding button-that-does-nothing is a better experience than
    // a Toast saying "couldn't move" for something the UI should have
    // just not offered.
    private fun moveEntryBy(entry: WadEntry, delta: Int) {
        val newPosition = entry.index + delta
        if (newPosition < 0 || newPosition >= adapter.itemCount) {
            Toast.makeText(
                this,
                if (delta < 0) "${entry.name} уже наверху" else "${entry.name} уже внизу",
                Toast.LENGTH_SHORT
            ).show()
            return
        }

        entryLoadJob?.cancel()
        entryLoadJob = lifecycleScope.launch {
            setBusy(true)
            val ok = when (val result = archiveRepository.moveEntry(entry.index, newPosition)) {
                is NativeResult.Success -> result.value
                is NativeResult.Failure -> {
                    Toast.makeText(this@MainActivity, "Ошибка: ${result.message}", Toast.LENGTH_SHORT).show()
                    false
                }
            }
            if (ok) refreshEntryList()
            else Toast.makeText(this@MainActivity, "Не удалось переместить", Toast.LENGTH_SHORT).show()
            setBusy(false)
        }
    }

    private fun showRenameDialog(entry: WadEntry) {
        val input = EditText(this).apply {
            setText(entry.name)
            setSelection(text.length)
        }
        AlertDialog.Builder(this)
            .setTitle("Переименовать ${entry.name}")
            .setView(input)
            .setPositiveButton("OK") { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isEmpty() || newName == entry.name) return@setPositiveButton

                entryLoadJob?.cancel()
                entryLoadJob = lifecycleScope.launch {
                    setBusy(true)
                    val ok = when (val result = archiveRepository.renameEntry(entry.index, newName)) {
                        is NativeResult.Success -> result.value
                        is NativeResult.Failure -> {
                            Toast.makeText(this@MainActivity, "Ошибка: ${result.message}", Toast.LENGTH_SHORT).show()
                            false
                        }
                    }
                    if (ok) refreshEntryList()
                    else Toast.makeText(this@MainActivity, "Не удалось переименовать", Toast.LENGTH_SHORT).show()
                    setBusy(false)
                }
            }
            .setNegativeButton("Отмена", null)
            .show()
    }

    private fun confirmDelete(entry: WadEntry) {
        AlertDialog.Builder(this)
            .setTitle("Удалить ${entry.name}?")
            .setMessage("Это действие можно отменить только через Save As без сохранения (файл на диске не тронут, пока вы явно не сохраните).")
            .setPositiveButton("Удалить") { _, _ ->
                entryLoadJob?.cancel()
                entryLoadJob = lifecycleScope.launch {
                    setBusy(true)
                    val ok = when (val result = archiveRepository.deleteEntry(entry.index)) {
                        is NativeResult.Success -> result.value
                        is NativeResult.Failure -> {
                            Toast.makeText(this@MainActivity, "Ошибка: ${result.message}", Toast.LENGTH_SHORT).show()
                            false
                        }
                    }
                    if (ok) refreshEntryList()
                    else Toast.makeText(this@MainActivity, "Не удалось удалить", Toast.LENGTH_SHORT).show()
                    setBusy(false)
                }
            }
            .setNegativeButton("Отмена", null)
            .show()
    }

    // Phase 8: Discard Changes -- distinct from just re-opening the same
    // file (which would also throw the edits away, but re-reads from disk
    // and re-runs androidDetectEntryType() on everything for no reason).
    // See ArchiveSession::discardChanges() for why this is cheap: mc_ is
    // never mutated by any edit operation, so reverting is just re-parsing
    // it fresh -- no separate pristine copy needed, no re-read from disk.
    private fun confirmDiscardChanges() {
        AlertDialog.Builder(this)
            .setTitle("Отменить все изменения?")
            .setMessage("Все правки с момента открытия файла будут потеряны. Файл на диске не тронут.")
            .setPositiveButton("Отменить изменения") { _, _ ->
                entryLoadJob?.cancel()
                entryLoadJob = lifecycleScope.launch {
                    setBusy(true)
                    val ok = when (val result = archiveRepository.discardChanges()) {
                        is NativeResult.Success -> result.value
                        is NativeResult.Failure -> {
                            Toast.makeText(this@MainActivity, "Ошибка: ${result.message}", Toast.LENGTH_SHORT).show()
                            false
                        }
                    }
                    if (ok) refreshEntryList()
                    else Toast.makeText(this@MainActivity, "Не удалось отменить изменения", Toast.LENGTH_SHORT).show()
                    setBusy(false)
                }
            }
            .setNegativeButton("Отмена", null)
            .show()
    }

    // CHANGED (content viewer, iteration 1): real byte-signature detection
    // identifies many entry types, but actual content preview is only
    // wired up for "Text" and "Palette" so far -- the two lowest-risk
    // types to start with. Everything else still falls back to the old
    // Toast placeholder until a later iteration adds it (Doom Graphic/Flat
    // need a PLAYPAL lookup + the same RGBA-in-C++ approach as the palette
    // viewer below; PNG can mostly ride on BitmapFactory directly; audio
    // needs at least header parsing for duration/format info).
    private fun onEntryClicked(entry: WadEntry) {
        // A WAD open in progress takes the whole global archive handle
        // native-lib.cpp keeps -- tapping an entry mid-open has nothing
        // valid to read yet, so ignore it rather than queue it up.
        if (isBusy && wadLoadJob?.isActive == true) return

        // Cancel any preview still loading for a previously-tapped entry --
        // see the Job field comment above onEntryClicked's declaration site.
        entryLoadJob?.cancel()
        entryLoadJob = lifecycleScope.launch {
            setBusy(true)
            when (entry.type) {
                "Text" -> showTextDialog(entry)
                "Palette" -> showPaletteDialog(entry)
                "Flat", "Doom Graphic" -> showImageDialog(entry)
                "PNG Image" -> showPngDialog(entry)
                "MIDI", "MUS Music", "GENMIDI Instruments", "WAV Sound",
                "OGG Audio", "FLAC Audio", "MP3 Audio", "DMX Sound",
                "PC Speaker Sound" -> showAudioInfoDialog(entry)
                else -> Toast.makeText(
                    this@MainActivity,
                    "${entry.name}: ${entry.type}, ${entry.size} B — просмотр этого типа пока не реализован",
                    Toast.LENGTH_SHORT
                ).show()
            }
            setBusy(false)
        }
    }

    // Distinguishes two different failure shapes rather than collapsing
    // them into one message: NativeResult.Failure means the native call
    // itself blew up (unexpected -- an exception or OOM, see SladeNative's
    // callNative()), while Success(null) means the call completed normally
    // but reports "nothing to show" per the existing per-function null
    // convention (e.g. no PLAYPAL to decode against). A Failure shows its
    // own message and reports itself "handled" so callers don't also show
    // their generic Success(null) message on top of it.
    private sealed class DialogData<out T> {
        data class Present<T>(val value: T) : DialogData<T>()
        object Absent : DialogData<Nothing>()
        object Handled : DialogData<Nothing>()
    }

    private fun <T> NativeResult<T>.toDialogData(entry: WadEntry): DialogData<T & Any> = when (this) {
        is NativeResult.Success -> if (value != null) DialogData.Present(value) else DialogData.Absent
        is NativeResult.Failure -> {
            Toast.makeText(this@MainActivity, "${entry.name}: $message", Toast.LENGTH_SHORT).show()
            DialogData.Handled
        }
    }

    private suspend fun showTextDialog(entry: WadEntry) {
        val text = when (val data = archiveRepository.entryText(entry.index).toDialogData(entry)) {
            is DialogData.Present -> data.value
            DialogData.Absent -> {
                Toast.makeText(this, "Не удалось прочитать ${entry.name}", Toast.LENGTH_SHORT).show()
                return
            }
            DialogData.Handled -> return
        }

        val textView = TextView(this).apply {
            setPadding(48, 32, 48, 32)
            setText(text)
            setTextIsSelectable(true)
            typeface = Typeface.MONOSPACE
            textSize = 12f
        }
        val scroll = ScrollView(this).apply { addView(textView) }

        AlertDialog.Builder(this)
            .setTitle(entry.name)
            .setView(scroll)
            .setPositiveButton("OK", null)
            .show()
    }

    // getEntryPalette() returns a packed IntArray: [width, height,
    // pixel0, pixel1, ...], pixels already in ARGB_8888 order -- matches
    // Bitmap.createBitmap()'s expected int-array format directly, no
    // conversion needed here. Upscaled with filter=false (nearest-
    // neighbor) afterward so individual color swatches stay crisp instead
    // of blurring into a gradient the way bilinear scaling would.
    private suspend fun showPaletteDialog(entry: WadEntry) {
        val packed = when (val data = archiveRepository.entryPalette(entry.index).toDialogData(entry)) {
            is DialogData.Present -> data.value
            DialogData.Absent -> {
                Toast.makeText(this, "Не удалось прочитать палитру ${entry.name}", Toast.LENGTH_SHORT).show()
                return
            }
            DialogData.Handled -> return
        }
        if (packed.size < 2) {
            Toast.makeText(this, "Не удалось прочитать палитру ${entry.name}", Toast.LENGTH_SHORT).show()
            return
        }

        val width = packed[0]
        val height = packed[1]
        val pixelCount = width * height
        if (width <= 0 || height <= 0 || packed.size < 2 + pixelCount) {
            Toast.makeText(this, "Повреждённые данные палитры ${entry.name}", Toast.LENGTH_SHORT).show()
            return
        }

        val pixels = packed.copyOfRange(2, 2 + pixelCount)
        val bitmap = Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)

        val scale = 24
        val scaled = Bitmap.createScaledBitmap(bitmap, width * scale, height * scale, false)

        val imageView = ImageView(this).apply {
            setImageBitmap(scaled)
            adjustViewBounds = true
        }

        AlertDialog.Builder(this)
            .setTitle("${entry.name} (${entry.size / 3} colors)")
            .setView(imageView)
            .setPositiveButton("OK", null)
            .show()
    }

    // Shared by "Flat" and "Doom Graphic" -- both decode to the same
    // [width, height, pixel...] packed format on the native side (see
    // native-lib.cpp: decodeFlat()/decodeDoomGraphic() via getEntryImage()),
    // the only real difference being that Doom Graphic pixels can be
    // transparent (alpha 0, shows through as the dialog's white background
    // here) where flats are always fully opaque 64x64 squares.
    //
    // A null/failed result most likely means the WAD has no PLAYPAL of its
    // own and nothing else in it classified as a palette either --
    // uncommon but possible for some PWADs that assume the base game's
    // palette. Says so explicitly rather than a generic error, since it's
    // the one failure mode a user could actually do something about (open
    // the IWAD alongside it, if support for multiple loaded archives ever
    // gets added).
    private suspend fun showImageDialog(entry: WadEntry) {
        val packed = when (val data = archiveRepository.entryImage(entry.index).toDialogData(entry)) {
            is DialogData.Present -> data.value
            DialogData.Absent -> {
                Toast.makeText(
                    this,
                    "Не удалось декодировать ${entry.name} (возможно, в WAD нет PLAYPAL)",
                    Toast.LENGTH_SHORT
                ).show()
                return
            }
            DialogData.Handled -> return
        }
        if (packed.size < 2) {
            Toast.makeText(
                this,
                "Не удалось декодировать ${entry.name} (возможно, в WAD нет PLAYPAL)",
                Toast.LENGTH_SHORT
            ).show()
            return
        }

        val width = packed[0]
        val height = packed[1]
        val pixelCount = width * height
        if (width <= 0 || height <= 0 || packed.size < 2 + pixelCount) {
            Toast.makeText(this, "Повреждённые данные изображения ${entry.name}", Toast.LENGTH_SHORT).show()
            return
        }

        val pixels = packed.copyOfRange(2, 2 + pixelCount)
        val bitmap = Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888)

        // Doom graphics range from a handful of pixels (HUD digits) up to
        // 320x200 (full-screen images); flats are always 64x64. An integer
        // nearest-neighbor scale up to a ~320px target keeps small sprites
        // readable without blurring, and leaves already-large images alone.
        val targetSize = 320
        val scale = (targetSize / maxOf(width, height)).coerceIn(1, 16)
        val scaled = if (scale > 1)
            Bitmap.createScaledBitmap(bitmap, width * scale, height * scale, false)
        else
            bitmap

        val imageView = ImageView(this).apply {
            setImageBitmap(scaled)
            adjustViewBounds = true
        }

        AlertDialog.Builder(this)
            .setTitle("${entry.name} (${width}×${height})")
            .setView(imageView)
            .setPositiveButton("OK", null)
            .show()
    }

    // Standard PNG bytes -- BitmapFactory decodes them directly, no need
    // for the manual RGBA-decode path Doom Graphic/Flat need above (those
    // aren't formats any Android API understands on its own).
    private suspend fun showPngDialog(entry: WadEntry) {
        val bytes = when (val data = archiveRepository.entryPng(entry.index).toDialogData(entry)) {
            is DialogData.Present -> data.value
            DialogData.Absent -> {
                Toast.makeText(
                    this,
                    "Не удалось прочитать PNG ${entry.name} (возможно, файл слишком большой)",
                    Toast.LENGTH_SHORT
                ).show()
                return
            }
            DialogData.Handled -> return
        }

        val bitmap = BitmapFactory.decodeByteArray(bytes, 0, bytes.size)
        if (bitmap == null) {
            Toast.makeText(this, "Повреждённый PNG ${entry.name}", Toast.LENGTH_SHORT).show()
            return
        }

        val imageView = ImageView(this).apply {
            setImageBitmap(bitmap)
            adjustViewBounds = true
        }

        AlertDialog.Builder(this)
            .setTitle("${entry.name} (${bitmap.width}×${bitmap.height})")
            .setView(imageView)
            .setPositiveButton("OK", null)
            .show()
    }

    // Shared by all nine audio lump types getEntryAudioInfo() (native-
    // lib.cpp) knows how to parse -- it returns a pre-formatted
    // multi-line info string (format, channels, sample rate, duration
    // where derivable) rather than structured data, since presentation
    // here is just "show it in a dialog" with nothing to lay out
    // differently per format.
    private suspend fun showAudioInfoDialog(entry: WadEntry) {
        val info = when (val data = archiveRepository.entryAudioInfo(entry.index).toDialogData(entry)) {
            is DialogData.Present -> data.value
            DialogData.Absent -> {
                Toast.makeText(this, "Не удалось прочитать метаданные ${entry.name}", Toast.LENGTH_SHORT).show()
                return
            }
            DialogData.Handled -> return
        }

        val textView = TextView(this).apply {
            setPadding(48, 32, 48, 32)
            setText(info)
            setTextIsSelectable(true)
            typeface = Typeface.MONOSPACE
            textSize = 13f
        }

        AlertDialog.Builder(this)
            .setTitle(entry.name)
            .setView(textView)
            .setPositiveButton("OK", null)
            .show()
    }

    // No `external fun` declarations or System.loadLibrary() here anymore
    // -- both moved to archiveRepository.kt, which is now the only class allowed
    // to touch the JNI boundary directly (see its file comment for why).
}
