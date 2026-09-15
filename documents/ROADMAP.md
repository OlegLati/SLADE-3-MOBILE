# SLADE Mobile — Development Roadmap

> Пошаговый план разработки SLADE Mobile: от WAD MVP до полноценного мобильного Doom Editor.

## Содержание

- [Project Status](#project-status)
- [Phase 0 — Project Bootstrap](#phase-0--project-bootstrap)
- [Phase 1 — SLADE Core Integration](#phase-1--slade-core-integration)
- [Phase 2 — Android File Access](#phase-2--android-file-access)
- [Phase 3 — WAD Browser](#phase-3--wad-browser)
- [Phase 4 — Entry Type System](#phase-4--entry-type-system)
- [Phase 5 — Background Processing](#phase-5--background-processing)
- [Phase 6 — ArchiveSession](#phase-6--archivesession)
- [Phase 7 — WAD Editor MVP](#phase-7--wad-editor-mvp)
- [Phase 8 — Safe Save](#phase-8--safe-save)
- [Phase 8.5 — Structural Refactoring](#phase-85--structural-refactoring)
- [Phase 9 — Undo / Redo](#phase-9--undo--redo)
- [Phase 10 — Native Tests](#phase-10--native-tests)
- [Phase 11 — Search & Filtering](#phase-11--search--filtering)
- [Phase 12 — Text Editor](#phase-12--text-editor)
- [Phase 13 — Image Viewer](#phase-13--image-viewer)
- [Phase 14 — Doom Graphic Editor](#phase-14--doom-graphic-editor)
- [Phase 15 — Palette Editor](#phase-15--palette-editor)
- [Phase 16 — Texture Editor](#phase-16--texture-editor)
- [Phase 17 — Audio Browser / Player](#phase-17--audio-browser--player)
- [Phase 18 — Hex Editor](#phase-18--hex-editor)
- [Phase 19 — PK3 / ZIP](#phase-19--pk3--zip)
- [Phase 20 — Android File Workflow](#phase-20--android-file-workflow)
- [Phase 21 — Mobile UX](#phase-21--mobile-ux)
- [Phase 22 — Properties / Metadata](#phase-22--properties--metadata)
- [Phase 23 — Recent / Favorites](#phase-23--recent--favorites)
- [Phase 24 — Resource Conversion](#phase-24--resource-conversion)
- [Phase 25 — Advanced Doom Tooling](#phase-25--advanced-doom-tooling)
- [Phase 26 — Map Viewer](#phase-26--map-viewer)
- [Phase 27 — Map Engine](#phase-27--map-engine)
- [Phase 28 — Map Editor — FINAL BOSS 👹](#phase-28--map-editor--final-boss-)
- [Phase 29 — Vertical Slices](#phase-29--vertical-slices)
- [Phase 30 — Priority Model](#phase-30--priority-model)
- [Phase 31 — Definition of Done — General](#phase-31--definition-of-done--general)
- [Phase 32 — Definition of Done — WAD Editor MVP](#phase-32--definition-of-done--wad-editor-mvp)
- [Phase 33 — Definition of Done — Text Editor](#phase-33--definition-of-done--text-editor)
- [Phase 34 — Definition of Done — Image Editor](#phase-34--definition-of-done--image-editor)
- [Phase 35 — Definition of Done — Map Editor](#phase-35--definition-of-done--map-editor)
- [Phase 36 — Recommended Development Order](#phase-36--recommended-development-order)
- [Phase 37 — Final Project Milestones](#phase-37--final-project-milestones)
- [Phase 38 — Project Philosophy](#phase-38--project-philosophy)
- [Phase 39 — Engineering Notes & Known Gotchas](#phase-39--engineering-notes--known-gotchas)


## Project Status

**Current stage:** WAD Editor MVP — core operations working end-to-end

Главная текущая задача:

> Move готов (Phase 7 закрыта). Safe Save (Phase 8) закрыта — Save As и Save (замена оригинала на месте) оба реализованы и подтверждены на устройстве, включая корректную работу iwad_lock и Discard Changes. Следующий этап: Structural Refactoring (Phase 8.5 / R0–R10). После фиксации baseline — разделить Kotlin state/UI и JNI/native responsibilities. Undo/Redo и native tests остаются обязательными частями P0 и выполняются внутри/сразу после этой архитектурной фазы.

Slice 2 ("First Edit") и Slice 3 ("Image Replacement") из раздела 29 подтверждены рабочими сквозь весь путь Open → Edit → Save → Close → Reopen → Verify, включая реальный запуск сохранённого WAD во внешнем движке (UZDoom/Delta Touch) — не только повторное открытие в самом SLADE Mobile.

---

# Phase 0 — Project Bootstrap

**Status: DONE**

- [x] Создан Android project
- [x] Kotlin application layer
- [x] C++ native layer
- [x] JNI integration
- [x] CMake integration

---

# Phase 1 — SLADE Core Integration

**Status: DONE / ONGOING**

- [x] Интеграция необходимых частей SLADE 3
- [x] Headless-oriented build
- [x] Compatibility layer
- [x] Устранение части desktop dependencies
- [x] Native archive infrastructure

### Remaining

- [ ] Уменьшать количество desktop dependencies
- [ ] Не допускать разрастания shim layer
- [ ] Выделять reusable headless SLADE components

---

# Phase 2 — Android File Access

**Status: DONE**

- [x] Storage Access Framework
- [x] `ParcelFileDescriptor`
- [x] raw file descriptor
- [x] mmap
- [x] Native file access
- [x] Передача данных в SLADE `MemChunk`

### Future

- [ ] Улучшить lifetime management
- [ ] Проверить поведение на больших файлах
- [ ] Обработать ошибки доступа/permission loss

---

# Phase 3 — WAD Browser

**Status: DONE**

- [x] Выбор WAD
- [x] Открытие WAD
- [x] Parsing directory
- [x] Enumeration entries
- [x] Basic type detection
- [x] Entry list
- [x] Basic preview infrastructure

Получен первый полноценный vertical slice:

```text
Android
 ↓
JNI
 ↓
SLADE
 ↓
WAD
 ↓
ArchiveEntry
 ↓
Android UI
```

---

# Phase 4 — Entry Type System

**Status: PARTIALLY DONE**

- [x] Basic entry type detection
- [x] Android-side detection для части типов
- [x] Базовые type-specific previews

### TODO

- [ ] Максимально использовать настоящий SLADE EntryType system
- [ ] Убрать дублирование detection logic
- [ ] Унифицировать type registry
- [ ] Добавить неизвестный/бинарный fallback

---

# Phase 5 — Background Processing

**Priority: P0**

**Status: DONE**

Все native-вызовы (open/parse/rename/delete/export/add/replace/save) идут через один выделенный background thread (`SladeNative.kt` + `nativeDispatcher`), а не через UI thread — сохраняя тот же инвариант "один поток одновременно трогает native-состояние", который раньше обеспечивался просто тем, что всё шло с UI thread.

### Tasks

- [x] Background archive loading
- [x] Background parsing
- [x] Background type detection
- [x] Background save
- [x] Progress state — только indeterminate (native не отдаёт процент прогресса; для настоящего прогресс-бара нужны изменения на C++-стороне)
- [x] Cancellation where appropriate — отменяется только ещё не начавшийся (queued) вызов; уже выполняющийся native-вызов прервать нельзя (блокирующий JNI)
- [x] Error propagation — `NativeResult<T>` sealed class на Kotlin-стороне отличает "native вызов упал с исключением" от штатного null/ERROR-протокола

Целевой flow:

```text
UI
 ↓
Request
 ↓
Background Worker
 ↓
Native
 ↓
Result
 ↓
UI
```

---

# Phase 6 — ArchiveSession

**Priority: P0**

**Status: DONE**

Реализовано как класс `ArchiveSession` (native-lib.cpp, анонимный namespace) вместо прежних `g_mc`/`g_wad` глобалов — владеет парой `MemChunk* mc_` + `WadArchive* wad_` с одним lifecycle (`open()`/`close()`), плюс dirty-флаг.

### Tasks

- [x] `ArchiveSession`
- [x] Native session handle
- [x] Open/close lifecycle
- [x] Entry access through session
- [x] Modification tracking
- [x] Dirty state

Первоначально:

```text
one active session
```

В будущем:

```text
multiple sessions / tabs
```

### Известное ограничение (важно для Phase 19 — PK3/ZIP)

`ArchiveSession` жёстко типизирован на `WadArchive*`, а не на базовый `Archive*`. Как только появится второй конкретный формат архива, session нужно будет обобщить (полиморфизм через `Archive*` или вариант/tagged union). См. секцию "Engineering Notes" внизу файла — там же про entryDataPtr()/чтение по смещению, которое тоже не переживёт PK3 as-is.

---

# Phase 7 — WAD Editor MVP

**Priority: P0 — MAIN MILESTONE**

**Status: DONE**

Это первый настоящий редакторский milestone. Основные операции реализованы и подтверждены рабочими на реальном устройстве, включая полный цикл Open → Edit → Save As → Close → Reopen → Verify — а для Replace дополнительно подтверждён запуск сохранённого WAD во внешнем движке (UZDoom/Delta Touch), не только повторное открытие в самом SLADE Mobile.

### Operations

- [x] Export entry
- [x] Import entry (реализовано как Replace/Add — отдельного "голого" import без привязки к конкретной entry не требовалось)
- [x] Replace entry
- [x] Add entry
- [x] Delete entry
- [x] Rename entry
- [x] Move entry

### Required flow

```text
Open
 ↓
Edit
 ↓
Save
 ↓
Close
 ↓
Reopen
 ↓
Verify
```

**Подтверждено:** rename/delete/Save As (2306 entries сохранены и переоткрыты корректно на DOOM.WAD); replace TITLEPIC → сохранённый edited.wad успешно запущен в UZDoom с новой картинкой на титульном экране; move entry (PLAYPAL сдвинут на две позиции вниз) → Save As → Close → Reopen подтверждён (2306 entries, размер файла не изменился) и дополнительно запущен в UZDoom (E1M1 загрузилась как обычно, 2306 lumps).

**Остаётся:** ничего — все операции WAD Editor MVP реализованы и подтверждены.

---

# Phase 8 — Safe Save

**Priority: P0**

**Status: DONE**

### Tasks

- [x] Temporary output file (в памяти — `MemChunk`, не файл на диске; см. ниже почему)
- [x] Archive serialization
- [x] Output validation (переоткрытие сериализованных байт как независимый `WadArchive` + сверка числа entries)
- [x] Reopen validation (теперь автоматическая часть `nativeValidateForSave`, а не только ручная проверка)
- [x] Safe replacement (замена оригинального файла на месте) — с оговоркой, см. ниже
- [x] Save As
- [x] Discard Changes

**Save As** соблюдает главное требование ("никогда не писать непосредственно в оригинальный WAD во время обычного редактирования") тем, что физически пишет в **новый** файл через отдельный SAF-picker (`ACTION_CREATE_DOCUMENT`) — оригинал открывается только на чтение и второй fd на запись к нему никогда не привязывается.

**Save (in-place)** реализована как двухшаговый протокол, а не "temp-file в том же каталоге + rename", потому что на Android нет прямого доступа к файловой системе для произвольного файла, выбранного через `ACTION_OPEN_DOCUMENT` — есть только `content://` URI и fd, а атомарный `rename()` поверх него приложению не предоставляется (это прерогатива провайдера, не наша).

Как сделано вместо этого:
1. `nativeValidateForSave()` — сериализует сессию **в памяти** (`MemChunk`, играет роль "temp file"), затем переоткрывает эти байты как независимый `WadArchive` и сверяет число entries. Оригинал в этот момент не тронут вообще.
2. Только если шаг 1 успешен, Kotlin (`MainActivity.saveInPlace()`) открывает ОРИГИНАЛЬНЫЙ URI в режиме `"rwt"`.
3. `nativeCommitSave(fd)` — записывает уже провалидированные байты.

**Честная оговорка:** `openFileDescriptor(uri, "rwt")` обнуляет файл в момент открытия, а не в момент записи — то есть если процесс убьют между шагом 2 и концом записи в шаге 3, оригинал может остаться повреждённым (усечённым). Более сильной гарантии (настоящий atomic rename) SAF для произвольного стороннего URI не даёт. Разница с "полноценной" схемой из DESIGN.md — по сути только в том, что "temp file" живёт в памяти процесса, а не на диске в том же каталоге; сам принцип "сначала полностью проверить, потом и только потом трогать оригинал" соблюдён.

Также подтверждено: `iwad_lock` (см. Native-раздел ниже) корректно блокирует `Save` (не `Save As`) для файлов, распознанных как настоящий IWAD — это намеренная защита, а не баг.

**Проверено на устройстве:** Discard Changes (правка исчезает, кнопки гаснут); Save на PWAD (`id1.wad` из Legacy of Rust) — сохранилось, "*" пропала, кнопки корректно disabled после; Save на настоящем IWAD корректно отклонён с понятным сообщением об ошибке.

---

# Phase 8.5 — Structural Refactoring

**Priority: P0 — START NOW**

**Status: NEXT**

> Полный план находится в `documents/REFACTORING.md`.

Эта фаза вводится после уже завершённого Safe Save и перед дальнейшим
расширением редактора. Она не является "заморозкой" разработки: мелкие
bugfixes и необходимые WAD improvements продолжаются, но новые крупные
подсистемы не должны добавляться обратно в `MainActivity.kt` и
`native-lib.cpp`.

### R0 — Baseline

- [ ] Зафиксировать текущую рабочую версию
- [ ] Сохранить WAD fixtures
- [ ] Зафиксировать Open → Edit → Save → Reopen regression flow
- [ ] Зафиксировать Rename/Delete/Move/Add/Replace/Export
- [ ] Зафиксировать Discard
- [ ] Зафиксировать внешний запуск сохранённого WAD

### R1 — Kotlin state separation

- [ ] Создать `ArchiveUiState`
- [ ] Создать/выделить `ArchiveViewModel`
- [ ] Выделить `ArchiveRepository`
- [ ] Перенести archive commands из Activity
- [ ] Перенести busy/loading/dirty state
- [ ] Оставить Activity presentation/lifecycle layer

### R2 — Native/JNI separation

- [ ] Вынести `ArchiveSession`
- [ ] Вынести entry data access
- [ ] Вынести save/serialization pipeline
- [ ] Вынести preview helpers
- [ ] Вынести detection helpers
- [ ] Оставить JNI thin

### R3 — Archive API

- [ ] Сформировать стабильный archive operation contract
- [ ] Убрать WAD-specific детали из верхнего application layer
- [ ] Сохранить WAD implementation без premature PK3 abstraction

### R4 — Typed data contract

- [ ] Ввести typed entry descriptor
- [ ] Убрать строковый протокол из UI
- [ ] Подготовить model к hierarchical archive paths

### R5 — Compatibility cleanup

- [ ] Классифицировать shims
- [ ] Удалить dead code
- [ ] Отделить Android platform adapters
- [ ] Не переносить domain logic в `compat/`

### R6 — Save isolation

- [ ] Вынести validate/commit
- [ ] Зафиксировать ownership pending save
- [ ] Добавить regression tests

### R7 — Undo/Redo foundation

- [ ] Command interface
- [ ] Rename command
- [ ] Delete command
- [ ] Move command
- [ ] Add command
- [ ] Replace command
- [ ] Undo stack
- [ ] Redo stack

### R8 — Native tests / CI

- [ ] Native unit tests
- [ ] WAD fixtures
- [ ] Save/reopen tests
- [ ] Malformed input tests
- [ ] CI build/test pipeline

### R9 — Performance

- [ ] Measure open
- [ ] Measure type detection
- [ ] Measure preview
- [ ] Measure serialization
- [ ] Measure large entry access
- [ ] Optimize only after profiling

### R10 — PK3/ZIP readiness

- [ ] Session independent of `WadArchive`
- [ ] Entry API independent of file offsets
- [ ] Type system independent of 8-char WAD names
- [ ] Save pipeline independent of WAD serialization

### Exit Criteria

```text
MainActivity = presentation
native-lib.cpp = JNI adapter
ArchiveSession = native domain component
SavePipeline = isolated
Entry model = typed
Regression suite = repeatable
```

После этого можно возвращаться к следующему большому функциональному
этапу без дальнейшего роста центральных монолитных файлов.

---

# Phase 9 — Undo / Redo

**Priority: P1**

**Status: TODO**

### Commands

- [ ] Rename
- [ ] Replace
- [ ] Add
- [ ] Delete
- [ ] Move

### UI

- [ ] Undo
- [ ] Redo
- [ ] Dirty state

---

# Phase 10 — Native Tests

**Priority: P0**

**Status: TODO**

### Fixtures

- [ ] Empty WAD
- [ ] Single-entry WAD
- [ ] Normal WAD
- [ ] Large WAD
- [ ] Corrupted header
- [ ] Truncated WAD
- [ ] Invalid directory

### Tests

- [ ] Open
- [ ] Directory
- [ ] Entry data
- [ ] Invalid offset
- [ ] Invalid size
- [ ] Corruption handling
- [ ] Save/reopen

---

# Phase 11 — Search & Filtering

**Priority: P1**

**Status: TODO**

- [ ] Search by name
- [ ] Filter by type
- [ ] Sort by name
- [ ] Sort by type
- [ ] Sort by size
- [ ] Search text contents
- [ ] Efficient list updates

---

# Phase 12 — Text Editor

**Priority: P1**

**Status: TODO**

### MVP

- [ ] Open text entry
- [ ] Edit
- [ ] Save
- [ ] Undo
- [ ] Redo
- [ ] Search
- [ ] Replace
- [ ] Go to line
- [ ] Copy
- [ ] Paste

### Supported formats

- [ ] DECORATE
- [ ] ZScript
- [ ] ACS
- [ ] MAPINFO
- [ ] ZMAPINFO
- [ ] TEXTURES
- [ ] ANIMDEFS
- [ ] SNDINFO
- [ ] GLDEFS

### Advanced

- [ ] Syntax highlighting
- [ ] Bracket matching
- [ ] Autocomplete
- [ ] Error highlighting

---

# Phase 13 — Image Viewer

**Priority: P1**

**Status: PARTIALLY DONE / TODO**

### Formats

- [ ] PNG
- [ ] JPEG
- [ ] Doom Graphic
- [ ] Flat
- [ ] Patch

### Features

- [ ] Zoom
- [ ] Pan
- [ ] Export
- [ ] Replace
- [ ] Preview

---

# Phase 14 — Doom Graphic Editor

**Priority: P1**

**Status: TODO**

- [ ] Pixel drawing
- [ ] Erase
- [ ] Color picker
- [ ] Zoom
- [ ] Palette selection
- [ ] Transparency
- [ ] X/Y offsets
- [ ] PNG import
- [ ] PNG export
- [ ] Save back to Doom Graphic

---

# Phase 15 — Palette Editor

**Priority: P1**

**Status: TODO**

- [ ] View palette
- [ ] Inspect RGB
- [ ] Edit colors
- [ ] Import
- [ ] Export
- [ ] PLAYPAL support
- [ ] Custom palette support

### Future

- [ ] Palette conversion
- [ ] Recoloring tools

---

# Phase 16 — Texture Editor

**Priority: P1**

**Status: TODO**

- [ ] TEXTURE1
- [ ] TEXTURE2
- [ ] PNAMES
- [ ] Create texture
- [ ] Delete texture
- [ ] Rename texture
- [ ] Change dimensions
- [ ] Add patch
- [ ] Remove patch
- [ ] Move patch
- [ ] Edit offsets
- [ ] Preview

### Touch

- [ ] Tap
- [ ] Drag
- [ ] Pinch zoom
- [ ] Long press

---

# Phase 17 — Audio Browser / Player

**Priority: P2**

**Status: TODO**

- [ ] WAV
- [ ] OGG
- [ ] MP3
- [ ] Doom sound resources
- [ ] Music
- [ ] Playback
- [ ] Pause
- [ ] Seek
- [ ] Metadata
- [ ] Export
- [ ] Replace

---

# Phase 18 — Hex Editor

**Priority: P2**

**Status: TODO**

- [ ] Hex view
- [ ] ASCII view
- [ ] Byte editing
- [ ] Search
- [ ] Go to offset
- [ ] Copy
- [ ] Paste
- [ ] Undo
- [ ] Redo

**Fallback:**
```text
Unknown Entry
     ↓
Hex Editor
```

---

# Phase 19 — PK3 / ZIP

**Priority: P1/P2**

**Status: TODO**

### Tasks

- [ ] ZIP reader
- [ ] PK3 support
- [ ] Entry abstraction
- [ ] Browse
- [ ] Add
- [ ] Remove
- [ ] Replace
- [ ] Rename
- [ ] Save

Архивный UI должен оставаться общим:

```text
Archive
 └── Entry
```

### Что не переживёт as-is из текущей WAD-реализации (важно прочитать перед стартом)

- **`ArchiveSession` жёстко типизирован на `WadArchive*`.** Нужно обобщить на базовый `Archive*` (или вариант/tagged union) прежде чем открывать второй конкретный формат.
- **`entryDataPtr()`/`buildEntryListArray()` читают байты entry напрямую по смещению из общего `MemChunk`.** Это работает только потому, что WAD хранит lump'ы несжатыми подряд в фиксированных позициях. PK3 — это ZIP: данные сжаты, никакого плоского "offset в общем буфере" не существует в принципе. Нужен реальный путь через decompression API SLADE (по-настоящему рабочий `ArchiveEntry::data()`/`loadEntryData()`, а не наш WAD-специфичный обход).
- **`androidDetectEntryType()`** (compat/slade_shims.cpp) — эвристика заточена под плоские 8-символьные WAD-имена + сигнатуры байт. У PK3/ZIP entries нормальные пути с расширениями и папками (namespaces вроде `sprites/`, `textures/`) — потребует расширения или отдельной ветки логики.
- **`saveToFd()`'s directory rebuild** (+ обход `iwad_lock` CVar) — целиком WAD-специфичный код. Для ZIP сериализация принципиально другая (через `ZipArchive::write()`, если он в SLADE core уже поддерживает запись без wxWidgets-зависимостей — стоит проверить заранее).

### Что переживёт без изменений

- Весь Kotlin-слой (`SladeNative.kt`, паттерн single-thread dispatcher, UI: rename/delete/export/replace/add, Save As, progress state) — работает через абстрактные index/name/bytes, формат архива снизу для него не важен.
- Публичные сигнатуры JNI-функций (`listEntries`, `renameEntry`, `deleteEntry`, `exportEntry`, `addEntry`, `replaceEntry`, `saveToFd`, `isDirty`) — менять придётся их C++ *реализацию*, не контракт.

---

# Phase 20 — Android File Workflow

**Priority: P2**

**Status: TODO**

- [ ] Open archive
- [ ] Save
- [ ] Save As
- [ ] Import
- [ ] Export
- [ ] Share
- [ ] Recent files
- [ ] Persistent URI permissions
- [ ] Permission error handling

---

# Phase 21 — Mobile UX

**Priority: P2**

**Status: TODO**

### Archive browser

- [ ] Mobile navigation
- [ ] Search
- [ ] Context menus
- [ ] Long press
- [ ] Bottom sheets where appropriate
- [ ] Touch-friendly controls

### Editors

- [ ] Consistent editor toolbar
- [ ] Back navigation
- [ ] Dirty state
- [ ] Save/discard prompt

### Tablet

- [ ] Landscape layout
- [ ] Split view
- [ ] Larger preview
- [ ] Keyboard/mouse support where appropriate

---

# Phase 22 — Properties / Metadata

**Priority: P2**

**Status: TODO**

**Display:**
```text
Name
Type
Size
Offset
Format
Compression
Namespace
```

Добавлять format-specific metadata по мере возможности.

---

# Phase 23 — Recent / Favorites

**Priority: P3**

**Status: TODO**

- [ ] Recent archives
- [ ] Favorite archives
- [ ] Favorite entries
- [ ] Recently edited entries
- [ ] Pin archive

---

# Phase 24 — Resource Conversion

**Priority: P3**

**Status: TODO**

Possible conversions:

```text
PNG → Doom Graphic
Doom Graphic → PNG
Image → Flat
Image → Patch
Palette conversion
Audio conversion
```

---

# Phase 25 — Advanced Doom Tooling

**Priority: P3**

**Status: TODO**

**Possible support:**
- [ ] DEHACKED
- [ ] DECORATE
- [ ] ZScript
- [ ] ACS
- [ ] MAPINFO
- [ ] ZMAPINFO
- [ ] SNDINFO
- [ ] GLDEFS
- [ ] Advanced texture handling
- [ ] Sprite handling
- [ ] Flats
- [ ] Colormaps
- [ ] Animated resources
- [ ] Switches
- [ ] Resource namespaces

---

# Phase 26 — Map Viewer

**Priority: P3**

**Status: TODO**

До полноценного редактора необходимо сделать viewer.

### Data

- [ ] Vertices
- [ ] Linedefs
- [ ] Sidedefs
- [ ] Sectors
- [ ] Things

### View

- [ ] Pan
- [ ] Zoom
- [ ] Grid
- [ ] Thing display
- [ ] Sector display
- [ ] Selection

---

# Phase 27 — Map Engine

**Priority: P3**

**Status: TODO**

Создать отдельный:

```text
MapModel
Geometry
Renderer
Interaction Layer
```

Не связывать Map Editor напрямую с Android UI.

---

# Phase 28 — Map Editor — FINAL BOSS 👹

**Priority: P4**

**Status: FUTURE**

### Supported formats

- [ ] Doom
- [ ] Doom II
- [ ] Hexen
- [ ] Boom
- [ ] UDMF
- [ ] ZDoom-specific formats

### MVP

- [ ] View map
- [ ] Zoom
- [ ] Pan
- [ ] Grid
- [ ] Select
- [ ] Move vertex
- [ ] Move thing
- [ ] Edit properties
- [ ] Edit textures
- [ ] Edit sectors
- [ ] Save

### Advanced

- [ ] Create vertices
- [ ] Create linedefs
- [ ] Split lines
- [ ] Create sectors
- [ ] Delete geometry
- [ ] Multi-selection
- [ ] Copy/paste
- [ ] Thing placement
- [ ] Texture alignment
- [ ] Node rebuilding
- [ ] Reject handling
- [ ] Blockmap handling
- [ ] 3D preview
- [ ] Advanced UDMF editing

---

# 29. Vertical Slices

Разработка должна идти через законченные vertical slices.

## Slice 1 — WAD Browser

```text
Open WAD
 ↓
Parse
 ↓
List entries
 ↓
Preview
```

**Status: DONE**

---

## Slice 2 — First Edit

```text
Open
 ↓
Modify entry
 ↓
Save
 ↓
Close
 ↓
Reopen
 ↓
Verify
```

**Status: DONE** — подтверждено на реальном устройстве (rename/delete → Save As → Close → Reopen DOOM.WAD, все 2306 entries на месте).

---

## Slice 3 — Image Replacement

```text
Open
 ↓
Select image
 ↓
Replace
 ↓
Save
 ↓
Reopen
 ↓
Preview
```

**Status: DONE** — подтверждено на реальном устройстве: заменил TITLEPIC в DOOM.WAD, сохранённый edited.wad успешно запустился в UZDoom (Delta Touch) с новой картинкой на титульном экране (не только повторное открытие в SLADE Mobile, а реальный запуск во внешнем движке).

---

## Slice 4 — Text Editing

```text
Open
 ↓
Select text entry
 ↓
Edit
 ↓
Save
 ↓
Reopen
```

---

## Slice 5 — Add Resource

```text
Open
 ↓
Import resource
 ↓
Add entry
 ↓
Save
 ↓
Reopen
```

---

## Slice 6 — Archive Management

```text
Rename
Delete
Move
Replace
Undo
Redo
Save
Reopen
```

---

## Slice 7 — PK3

```text
Open PK3
 ↓
Browse
 ↓
Edit
 ↓
Save
 ↓
Reopen
```

---

## Slice 8 — Doom Graphic

```text
Open graphic
 ↓
Edit pixels
 ↓
Change palette
 ↓
Save
 ↓
Reopen
```

---

## Slice 9 — Texture

```text
Open texture
 ↓
Edit patches
 ↓
Save
 ↓
Reopen
```

---

## Slice 10 — Map

```text
Open map
 ↓
View
 ↓
Edit
 ↓
Save
 ↓
Reopen
 ↓
Play
```

---

# 30. Priority Model

## P0 — Foundation

```text
[ ] Structural Refactoring R0–R8
[ ] MainActivity state separation
[ ] JNI/native separation
[ ] Archive API
[ ] Typed entry contract
[ ] Native regression suite
```


```text
[✓] Android project
[✓] Kotlin/C++
[✓] JNI
[✓] SLADE core
[✓] SAF
[✓] mmap
[✓] MemChunk
[✓] WAD loading
[✓] Directory parsing
[✓] Entry list
[✓] Basic type detection
[✓] Preview infrastructure
[✓] Background loading
[✓] ArchiveSession

[✓] Safe Save (см. Phase 8: Save As и Save в замену оригинала на месте оба готовы)
[ ] Validation
[ ] Native tests
```

---

## P1 — Core Editor

```text
[✓] Export
[✓] Import
[✓] Replace
[✓] Add
[✓] Delete
[✓] Rename
[✓] Save As

[✓] Move
[ ] Undo
[ ] Redo
[✓] Save (полноценная замена оригинала на месте, см. Phase 8)
[✓] Reopen validation (теперь автоматическая, часть nativeValidateForSave)
[ ] Search
```

---

## P2 — Main Resource Editors

```text
[ ] Text Editor
[ ] Image Viewer
[ ] Doom Graphic Editor
[ ] Palette Editor
[ ] Texture Editor
[ ] Audio Player
[ ] Hex Editor
```

---

## P3 — Advanced Features

```text
[ ] PK3
[ ] ZIP
[ ] Search improvements
[ ] Filtering
[ ] Recent files
[ ] Favorites
[ ] Conversion
[ ] Advanced Doom tooling
[ ] Mobile/tablet UX
```

---

## P4 — Final Boss

```text
[ ] Map Viewer
[ ] Map Engine
[ ] Map Editor
[ ] Advanced map formats
[ ] 3D preview
```

---

# 31. Definition of Done — General

Feature считается готовой только если:

```text
Implement
 ↓
Test
 ↓
Save
 ↓
Close
 ↓
Reopen
 ↓
Verify
```

Для операций редактирования также необходимо проверить, что другие entries не повреждены.

---

# 32. Definition of Done — WAD Editor MVP

WAD Editor MVP готов, когда доступны:

```text
[ ] Open
[ ] Browse
[ ] Search
[ ] Export
[ ] Import
[ ] Replace
[ ] Add
[ ] Delete
[ ] Rename
[ ] Move
[ ] Undo
[ ] Redo
[ ] Save
[ ] Save As
[ ] Validation
[ ] Reopen verification
```

и:

> Редактирование одного entry не должно повреждать остальные entries.

---

# 33. Definition of Done — Text Editor

```text
[ ] Open
[ ] Edit
[ ] Undo
[ ] Redo
[ ] Search
[ ] Replace
[ ] Save
[ ] Reopen
[ ] Encoding handling
```

---

# 34. Definition of Done — Image Editor

```text
[ ] Open
[ ] Zoom
[ ] Pan
[ ] Edit
[ ] Undo
[ ] Save
[ ] Replace
[ ] Export
```

Для Doom Graphics дополнительно:

```text
[ ] Palette
[ ] Transparency
[ ] Offsets
[ ] Doom encoding
```

---

# 35. Definition of Done — Map Editor

Map Editor считается готовым только после:

```text
Open existing map
 ↓
Edit geometry
 ↓
Edit things
 ↓
Edit sectors
 ↓
Edit textures
 ↓
Save
 ↓
Reopen in SLADE
 ↓
Reopen in compatible tools
 ↓
Play map
```

---

# 36. Recommended Development Order

Итоговый порядок разработки:

```text
1. Background loading
        ↓
2. ArchiveSession
        ↓
3. Safe Save
        ↓
4. Structural Refactoring R0–R6
        ↓
5. Native tests / CI
        ↓
6. Undo / Redo
        ↓
7. Export / Import / Replace
        ↓
6. Add / Delete / Rename / Move
        ↓
7. Undo / Redo
        ↓
8. Search
        ↓
9. Text Editor
        ↓
10. Image / Doom Graphic Editor
        ↓
11. Palette Editor
        ↓
12. Texture Editor
        ↓
13. Audio
        ↓
14. Hex Editor
        ↓
15. PK3 / ZIP
        ↓
16. Mobile UX refinement
        ↓
17. Advanced Doom tooling
        ↓
18. Map Viewer
        ↓
19. Map Engine
        ↓
20. MAP EDITOR 👹
```

---

# 37. Final Project Milestones

## Milestone A — Proof of Concept

```text
Open WAD
Browse
Preview
```

**STATUS: ACHIEVED**

---

## Milestone B — Real WAD Editor

```text
Open
Edit
Save
Reopen
Verify
```

**STATUS: ACHIEVED** — подтверждено на реальном устройстве (rename/delete/replace + Save As + reopen, включая запуск во внешнем движке UZDoom)

---

## Milestone C — Resource Editor

```text
WAD
+
Text
+
Images
+
Graphics
+
Palettes
+
Textures
+
Audio
```

---

## Milestone D — Mobile Doom Modding Tool

```text
WAD
PK3
Editors
Search
Import/Export
Undo/Redo
Mobile UX
```

---

## Milestone E — Advanced Doom Editor

```text
Scripts
Textures
Sprites
Maps
Advanced formats
```

---

## Milestone F — SLADE Mobile

```text
Archive Management
+
Resource Editors
+
Doom Tooling
+
Map Editor
+
Mobile-first UX
```

---

# 38. Project Philosophy

### Structural refactoring

После каждого крупного milestone необходимо уменьшать связанность, а не
только добавлять функции. Крупные файлы являются сигналом для extraction,
если в них появляется несколько независимых ответственностей.

Подробная политика и пошаговая миграция находятся в
`documents/REFACTORING.md`.



Главный принцип разработки:

> **Сначала надёжный редактор ресурсов, потом полноценный Doom Editor.**

Необходимо избегать попытки реализовать весь SLADE одновременно.

Каждая новая возможность должна проходить через:

```text
Core
 ↓
Native API
 ↓
Kotlin
 ↓
UI
 ↓
Save
 ↓
Reopen
 ↓
Test
```

Map Editor остаётся последней крупной подсистемой проекта.

---

# 39. Engineering Notes & Known Gotchas

Практические уроки из реализации Phase 5-7 — чтобы не наступать на те же грабли повторно при дальнейшей разработке.

## Native (C++)

**`iwad_lock` CVar (`WadArchive.cpp`) блокирует запись IWAD-архивов.**
`WadArchive::write()` безусловно отказывает, если архив распознан как IWAD (DOOM.WAD, DOOM2.WAD и т.д.) и `iwad_lock == true` (default). Осмысленная защита для desktop SLADE (не дать случайно испортить оригинал игры), но неприменима к нашему Save As — он всегда пишет в **новый** файл через отдельный SAF-picker, оригинал не трогается. Решение: временно выставлять `iwad_lock = false` только на время вызова `write()`, восстанавливать сразу после — на обоих путях (успех и провал), а не в конце функции.

**`ArchiveEntry::rawData()` всегда возвращает `nullptr` в headless-режиме.**
`loadEntryData()` пытается открыть `filename_` как файл на диске (`wxFile`) — а мы WAD как файл никогда не открываем (только mmap + MemChunk). `MemChunk::write(nullptr, size)` молча ничего не делает и **не продвигает курсор записи** — то есть сохранённый WAD получался бы структурно битым (директория пишется не там, где нужно), даже без явного краша. Решение: перед вызовом `write()` вручную "прогревать" данные каждой ещё не загруженной entry через `entry->importMem(mc->data() + offset, size)`, читая байты из session's MemChunk по уже известному offset (`getEntryOffset()`). Цена: данные каждой entry дублируются в памяти (лежат и в общем MemChunk, и в самой entry) с момента первого сохранения — стоит помнить на очень больших WAD.

**Осиротевшая закрывающая скобка namespace ловится не суммой, а структурой.**
При более раннем рефакторинге (введение `ArchiveSession` как отдельного anonymous namespace) потерялось открытие ВТОРОГО anonymous namespace, оборачивающего decode/parse-хелперы — осталась закрывающая `} // namespace` без пары. Суммарный подсчёт `{`/`}` по всему файлу это не ловит: лишняя `}` в одном месте компенсируется недостающей `{` в другом, сумма сходится, а компилятор падает на реальной сборке. Правильная проверка — сопоставлять парные маркеры (`namespace` / `} // namespace`) явно, либо трекать глубину вложенности по ходу файла построчно (никогда не должна уходить в минус, в конце файла должна быть 0), причём с игнорированием `//`/`/* */` комментариев и строковых литералов (иначе прозаические упоминания скобок в самих комментариях собьют подсчёт).

## Build / tooling

**`--` внутри XML-комментариев запрещено спецификацией XML**, а выглядит как безобидное тире — на эту грабли наступали трижды подряд при написании комментариев в `activity_main.xml`. AGP падает на этом с не самой очевидной с первого взгляда ошибкой парсинга XML. В этом проекте — использовать `.` или `:` вместо `--` как разделитель мысли в XML-комментариях.

## Kotlin

**`RecyclerView.isEnabled` не блокирует клики по дочерним view.**
Отключение `entryList.isEnabled` — чисто косметическое: RecyclerView как ViewGroup не пробрасывает enabled-состояние вниз на itemView, у которого свой собственный `OnClickListener`. Реальная защита от гонок при быстрых тапах (открытие WAD/edit в процессе + новый тап) — явный флаг `isBusy`, проверяемый в начале каждого обработчика клика/long-click.

**Suspend- и non-suspend-функция с одинаковым именем в одном классе — работает, но лишний риск.**
Публичная `suspend fun renameEntry(...)` и приватная `external fun renameEntry(...)` в одном `object` технически резолвятся однозначно (suspend-вызов недоступен из non-suspend лямбды `callNative`), но это не то, ради чего стоит держать в голове тонкости overload resolution. В этом проекте `external fun` всегда называются с префиксом `native*` (`nativeRenameEntry` и т.д.), отдельно от suspend-обёртки того же смысла.
