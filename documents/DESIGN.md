# SLADE Mobile — Design Document

> Архитектурный документ проекта SLADE Mobile: Android UI + native adapter + headless SLADE Core.

## Содержание

- [1. Overview](#1-overview)
- [2. Goals](#2-goals)
- [3. Non-Goals](#3-non-goals)
- [4. Core Architecture](#4-core-architecture)
- [5. Separation of Responsibilities](#5-separation-of-responsibilities)
- [6. Archive Abstraction](#6-archive-abstraction)
- [7. ArchiveSession](#7-archivesession)
- [8. File Access](#8-file-access)
- [9. mmap](#9-mmap)
- [10. Threading](#10-threading)
- [11. Native Resource Lifetime](#11-native-resource-lifetime)
- [11.1. Structural Refactoring Policy](#111-structural-refactoring-policy)
- [12. Entry Type System](#12-entry-type-system)
- [13. Editor Architecture](#13-editor-architecture)
- [14. Data vs Presentation](#14-data-vs-presentation)
- [15. Archive Editing](#15-archive-editing)
- [16. Transactional Save](#16-transactional-save)
- [17. Safe Save Requirements](#17-safe-save-requirements)
- [18. Undo / Redo](#18-undo--redo)
- [19. Mobile UI](#19-mobile-ui)
- [20. Text Editor](#20-text-editor)
- [21. Image Editor](#21-image-editor)
- [22. Doom Graphic Editor](#22-doom-graphic-editor)
- [23. Palette Editor](#23-palette-editor)
- [24. Texture Editor](#24-texture-editor)
- [25. Audio](#25-audio)
- [26. Hex Editor](#26-hex-editor)
- [27. Map Architecture](#27-map-architecture)
- [28. Map Data](#28-map-data)
- [29. Map Renderer](#29-map-renderer)
- [30. Security & Robustness](#30-security--robustness)
- [31. Testing Philosophy](#31-testing-philosophy)
- [32. Native Tests](#32-native-tests)
- [33. Regression Testing](#33-regression-testing)
- [34. Performance](#34-performance)
- [35. Future Extensibility](#35-future-extensibility)
- [36. Final Vision](#36-final-vision)


## 1. Overview

**SLADE Mobile** — Android-приложение для просмотра и редактирования Doom/WAD-ресурсов, построенное на базе headless-компонентов SLADE 3.

Проект не является попыткой буквально перенести desktop-интерфейс SLADE 3 на Android.

Основная идея:

```text
SLADE 3 Source
      │
      ▼
Headless SLADE Core
      │
      ▼
Android Native Adapter
      │
     JNI
      │
      ▼
Kotlin API
      │
      ▼
Android UI
```

SLADE используется прежде всего как **backend для работы с форматами, архивами и Doom-ресурсами**, а пользовательский интерфейс проектируется отдельно с учётом мобильных устройств.

---

# 2. Goals

Основная цель проекта — создать полноценный мобильный инструмент для Doom-моддинга.

Пользователь должен иметь возможность:

- открывать WAD;
- открывать PK3/ZIP и другие поддерживаемые архивы;
- просматривать содержимое архивов;
- искать и фильтровать entries;
- экспортировать ресурсы;
- импортировать новые ресурсы;
- заменять существующие ресурсы;
- удалять entries;
- переименовывать entries;
- перемещать entries;
- сохранять изменённые архивы;
- редактировать текстовые ресурсы;
- редактировать изображения;
- редактировать Doom Graphics;
- работать с палитрами;
- редактировать текстуры;
- просматривать и редактировать аудио-ресурсы;
- использовать Hex Editor для бинарных данных;
- в будущем редактировать Doom-карты.

---

# 3. Non-Goals

Проект не должен пытаться:

- портировать wxWidgets;
- копировать desktop UI SLADE;
- переносить всю desktop-инфраструктуру;
- поддерживать ненужные GUI-зависимости SLADE;
- превращать compatibility layer в замену всему desktop SLADE.

Если часть SLADE нужна только для desktop GUI и не требуется headless core, она не должна автоматически переноситься в Android.

---

# 4. Core Architecture

Главный архитектурный принцип:

```text
┌─────────────────────────────┐
│       Android UI            │
│ Kotlin / Android Framework  │
└──────────────┬──────────────┘
               │
              JNI
               │
┌──────────────▼──────────────┐
│    Android Native Layer     │
│ ArchiveSession / Adapters   │
└──────────────┬──────────────┘
               │
┌──────────────▼──────────────┐
│       SLADE Core             │
│ Archives / Formats / Utils  │
└─────────────────────────────┘
```

---

# 5. Separation of Responsibilities

## 5.1. SLADE Core

Core отвечает за:

- parsing;
- формат архивов;
- чтение ресурсов;
- запись ресурсов;
- определение типов;
- декодирование;
- кодирование;
- работу с внутренними структурами Doom.

Core не должен знать об Android UI.

---

## 5.2. Android Native Layer

Native adapter отвечает за:

- JNI;
- file descriptors;
- mmap;
- lifecycle native objects;
- преобразование данных между SLADE и Kotlin;
- Android-specific filesystem operations;
- безопасное управление native resources.

---

## 5.3. Kotlin Layer

Kotlin отвечает за:

- application state;
- lifecycle;
- UI;
- navigation;
- background jobs;
- SAF;
- отображение данных;
- user interaction.

Kotlin не должен напрямую зависеть от внутренних классов SLADE.

---

# 6. Archive Abstraction

Вместо привязки UI к WAD желательно использовать абстракцию:

```text
Archive
 └── Entry
```

Например:

```text
Archive
 ├── name
 ├── format
 └── entries[]

Entry
 ├── id
 ├── name
 ├── type
 ├── size
 ├── metadata
 └── data
```

Тогда UI не должен знать, находится ли entry в:

```text
WAD
PK3
ZIP
GRP
PAK
```

---

# 7. ArchiveSession

Для работы с редактируемым архивом используется концепция `ArchiveSession`.

```text
ArchiveSession
 ├── source
 ├── mapped data
 ├── archive
 ├── entries
 ├── modifications
 └── undo/redo history
```

Session отвечает за состояние документа до сохранения.

В будущем это позволит поддерживать несколько открытых архивов:

```text
Session A → DOOM.WAD
Session B → MOD.WAD
Session C → SIGIL.WAD
```

На ранней стадии допустима только одна активная session.

**Implementation Note (Phase 6, реализовано):** текущий `ArchiveSession` (native-lib.cpp) владеет парой `MemChunk* mc_` + `WadArchive* wad_` с общим lifecycle и dirty-флагом — соответствует плану выше по духу, но типизирован конкретно на `WadArchive*`, а не на абстрактный `Archive*`. Обобщение до полиморфной абстракции понадобится при добавлении PK3/ZIP (Phase 19 в ROADMAP.md) — там же зафиксированы детали, что именно придётся переделать.

---

# 8. File Access

Android-файлы открываются через Storage Access Framework.

Предпочтительный путь:

```text
SAF
 ↓
ParcelFileDescriptor
 ↓
raw file descriptor
 ↓
mmap
 ↓
SLADE MemChunk
```

Не следует без необходимости использовать:

```text
InputStream
 ↓
ByteArray
 ↓
JNI copy
```

для больших архивов.

---

# 9. mmap

`mmap` используется для уменьшения количества лишних копирований данных между Android и native layer.

Текущая архитектура не является полностью zero-copy:

```text
mmap
 ↓
MemChunk
```

поэтому данные всё ещё могут копироваться внутри native layer.

Полный zero-copy не является текущей целью.

---

# 10. Threading

Тяжёлые операции не должны выполняться на Android UI thread.

К ним относятся:

- opening archive;
- parsing;
- type detection;
- large entry reads;
- saving;
- validation;
- resource conversion.

Концептуальная схема:

```text
UI Thread
    │
    ├── request
    ▼
Background Worker
    │
    ├── Native processing
    │
    ▼
Main Thread
    │
    └── UI update
```

На Kotlin рекомендуется использовать coroutines и подходящий background dispatcher.

**Implementation Note (Phase 5, реализовано):** `SladeNative.kt` использует **один выделенный поток** (`Executors.newSingleThreadExecutor` + `asCoroutineDispatcher()`), а не общий thread pool (`Dispatchers.IO`) — сознательный выбор, а не недосмотр: `ArchiveSession` на native-стороне не thread-safe, и один выделенный поток сохраняет тот же инвариант "одновременно только один поток трогает native-состояние", который раньше обеспечивался просто тем, что все вызовы шли с UI thread.

---

# 11. Native Resource Lifetime

Все native resources должны иметь предсказуемый lifecycle:

```text
open
 ↓
use
 ↓
close
 ↓
free
```

Особое внимание:

- `mmap`;
- `MemChunk`;
- `WadArchive`;
- native handles;
- JNI references.

Не допускаются:

- memory leaks;
- double free;
- dangling pointers;
- use-after-free.

---


# 11.1. Structural Refactoring Policy

Проект развивается через постепенный structural refactoring. Рефакторинг
начинается **до** добавления большого количества новых редакторов и форматов,
потому что текущий WAD MVP уже предоставляет стабильный regression baseline.

Основные правила:

```text
UI
 ↓
ViewModel / application state
 ↓
Repository / domain API
 ↓
Kotlin native API
 ↓
JNI adapter
 ↓
Native domain
 ↓
SLADE Core
```

`MainActivity` не должна владеть archive business logic, а JNI не должен
содержать алгоритмы работы с архивом.

Текущие крупные файлы (`MainActivity.kt`, `native-lib.cpp`) следует уменьшать
путём извлечения уже существующих ответственностей, а не переписыванием
проекта с нуля.

Подробный пошаговый план находится в `documents/REFACTORING.md`.

## Refactoring checkpoints

```text
R0  Baseline / regression protection
R1  Kotlin state separation
R2  Native/JNI separation
R3  Archive API
R4  Typed data contract
R5  Compatibility layer cleanup
R6  Save pipeline isolation
R7  Undo/Redo command foundation
R8  Native tests / CI
R9  Performance profiling
R10 PK3/ZIP readiness
```

Производительность не является первым этапом. Сначала фиксируются ownership,
lifecycle и API boundaries; затем bottlenecks измеряются профилировщиком и
только после этого оптимизируются.

# 12. Entry Type System

Временная архитектура может использовать Android-specific detection для некоторых ресурсов.

Целевая архитектура:

```text
SLADE EntryType
       ↓
Unified detection
       ↓
Android
```

Необходимо избегать долгосрочного существования двух независимых систем определения типов.

**Implementation Note:** текущая detection — это `androidDetectEntryType()` (compat/slade_shims.cpp), самостоятельная byte-signature/name эвристика, не связанная с настоящим SLADE `EntryType` engine (который требует ZIP-based resource-архивы, ещё не портированные). Работает достаточно для WAD, но заточена под плоские 8-символьные имена и не переживёт PK3/ZIP as-is (там нормальные пути с расширениями и папками) — см. Phase 4 и Phase 19 в ROADMAP.md.

---

# 13. Editor Architecture

Редакторы выбираются по типу entry.

```text
Entry
 │
 ▼
EntryType
 │
 ▼
EditorRegistry
 │
 ├── TextEditor
 ├── ImageEditor
 ├── GraphicEditor
 ├── PaletteEditor
 ├── TextureEditor
 ├── AudioEditor
 ├── HexEditor
 └── MapEditor
```

Archive browser не должен знать детали реализации конкретных редакторов.

---

# 14. Data vs Presentation

Native слой должен предоставлять **данные**, а не UI-ориентированные операции.

Предпочтительно:

```text
readEntry()
```

вместо большого количества функций:

```text
getEntryImage()
getEntryPng()
getEntryPalette()
getEntryText()
getEntryAudio()
...
```

Специализированные функции допустимы на prototype-стадии.

Целевая модель:

```text
Entry
 ↓
EntryData
 ↓
Kotlin
 ↓
appropriate editor
```

---

# 15. Archive Editing

Основные операции:

```text
add
remove
rename
move
replace
export
```

Они должны выполняться через `ArchiveSession`, а не непосредственно над оригинальным файлом.

**Implementation Note (Phase 7):** add/remove/rename/replace/export реализованы через `ArchiveSession` (native-lib.cpp) — все мутирующие операции (`renameEntry`, `deleteEntry`, `addEntry`, `replaceEntry`) идут через `g_session.archive()`, никогда напрямую над исходными байтами файла. `move` пока не реализован.

---

# 16. Transactional Save

Оригинальный архив не должен изменяться напрямую во время редактирования.

Предпочтительный алгоритм:

```text
Original Archive
       │
       ▼
ArchiveSession
       │
       ├── modifications
       │
       ▼
Temporary Output
       │
       ▼
Validation
       │
       ▼
Replace / Save As
```

**Implementation Note (Phase 8, частично):** главный принцип раздела — "оригинал не должен изменяться напрямую" — уже соблюдается: Save As пишет в **новый** файл через отдельный SAF-picker (`ACTION_CREATE_DOCUMENT`), исходный файл открывается только на чтение и второй writable-дескриптор к нему никогда не привязывается. Полная цепочка Temporary Output → Validation → Replace (замена оригинала на месте) пока не реализована — см. Phase 8 в ROADMAP.md.

---

# 17. Safe Save Requirements

Перед заменой оригинального файла желательно проверить:

- archive header;
- directory;
- entry offsets;
- entry sizes;
- bounds;
- структуру output;
- возможность повторного открытия сохранённого архива.

**Цель:**
> Если операция сохранения завершилась успешно, полученный архив должен быть пригоден для повторного открытия.

---

# 18. Undo / Redo

Редактирование желательно строить вокруг command/history model.

```text
Command
 ├── Rename
 ├── Replace
 ├── Add
 ├── Delete
 └── Move
```

History:

```text
Undo
Redo
```

Это также позволит отказаться от немедленного изменения оригинального файла.

---

# 19. Mobile UI

SLADE Mobile не должен копировать desktop UI.

UI должен быть рассчитан на:

- touch;
- маленькие экраны;
- tablets;
- portrait;
- landscape;
- long press;
- gestures.

---

## 19.1. Archive Browser

Пример:

```text
┌─────────────────────────┐
│ DOOM.WAD            ⋮   │
├─────────────────────────┤
│ 🔍 Search               │
├─────────────────────────┤
│ PLAYPAL       Palette   │
│ TITLEPIC      Graphic   │
│ E1M1          Map       │
│ TEXTURE1      Texture   │
│ DECORATE      Text      │
│ DSBANK        Sound     │
└─────────────────────────┘
```

---

## 19.2. Context Menu

Long press entry:

```text
Rename
Replace
Export
Delete
Move
Properties
```

---

# 20. Text Editor

Основные функции:

- editing;
- undo;
- redo;
- search;
- replace;
- go to line;
- copy;
- paste;
- line numbers.

Дополнительно:

- syntax highlighting;
- bracket matching;
- autocomplete;
- error highlighting.

Поддерживаемые направления:

```text
DECORATE
ZScript
ACS
MAPINFO
ZMAPINFO
TEXTURES
ANIMDEFS
SNDINFO
GLDEFS
```

---

# 21. Image Editor

Поддерживаемые типы:

```text
PNG
JPEG
Doom Graphic
Flat
Patch
```

Основные функции:

- zoom;
- pan;
- edit;
- replace;
- export;
- undo.

---

# 22. Doom Graphic Editor

Специфические данные:

```text
pixels
palette
transparent color
offset X
offset Y
width
height
```

**MVP:**
- pixel drawing;
- erase;
- color picker;
- zoom;
- palette selection;
- offset editing;
- PNG import;
- PNG export.

---

# 23. Palette Editor

Поддержка:

```text
PLAYPAL
custom palettes
```

Функции:

- inspect colors;
- RGB values;
- edit;
- import;
- export.

**В будущем:**
- palette conversion;
- recoloring.

---

# 24. Texture Editor

Работа с:

```text
TEXTURE1
TEXTURE2
PNAMES
```

Функции:

- create;
- delete;
- rename;
- resize;
- add patch;
- remove patch;
- move patch;
- edit offsets;
- preview.

**Touch interaction:**
- tap;
- drag;
- pinch zoom;
- long press.

---

# 25. Audio

Поддерживаемые направления:

```text
WAV
OGG
MP3
Doom sound resources
Music
```

**MVP:**
- play;
- pause;
- seek;
- metadata;
- export;
- replace.

---

# 26. Hex Editor

Fallback editor для неизвестных и бинарных entries.

Пример:

```text
Offset     Hex                     ASCII

00000000   44 4F 4F 4D ...        DOOM...
```

Функции:

- view;
- edit;
- search;
- goto offset;
- copy;
- paste;
- undo.

---

# 27. Map Architecture

Map Editor должен иметь отдельную архитектуру.

```text
MapEntry
   ↓
MapModel
   ↓
Geometry
   ↓
Renderer
   ↓
Interaction Layer
```

Он не должен быть просто ещё одним `EntryEditor`.

---

# 28. Map Data

Основные объекты:

```text
Vertex
Linedef
Sidedef
Sector
Thing
```

Поддерживаемые форматы должны добавляться постепенно:

```text
Doom
Doom II
Hexen
Boom
UDMF
ZDoom
```

---

# 29. Map Renderer

Renderer должен быть независим от UI.

```text
MapModel
   ↓
Render data
   ↓
Android renderer
```

Это позволит впоследствии реализовать:

- zoom;
- pan;
- grid;
- selection;
- highlighting;
- different render modes.

---

# 30. Security & Robustness

Любой пользовательский архив следует считать потенциально повреждённым.

Нельзя доверять:

```text
offset
size
count
width
height
directory data
```

Каждый parser обязан выполнять bounds checking.

Например:

```text
offset >= 0
size >= 0
offset + size <= file_size
```

Аналогичные проверки необходимы во всех бинарных форматах.

---

# 31. Testing Philosophy

Функция считается готовой не тогда, когда она работает на одном хорошем файле.

Например:

```text
Replace
 ↓
Save
 ↓
Close
 ↓
Reopen
 ↓
Verify
```

должно быть частью тестирования.

---

# 32. Native Tests

Необходимо иметь fixtures:

```text
empty.wad
single_entry.wad
normal.wad
large.wad
corrupted_header.wad
truncated.wad
invalid_directory.wad
```

Основные тесты:

```text
test_open_wad
test_empty_wad
test_directory
test_entry_data
test_invalid_offset
test_invalid_size
test_corrupted_archive
```

---

# 33. Regression Testing

Изменения SLADE core должны проверяться:

```text
Build
 ↓
Native tests
 ↓
Archive tests
 ↓
Android integration
```

Особенно при обновлении upstream SLADE.

---

# 34. Performance

Проект должен учитывать большие архивы.

Требования:

- background parsing;
- efficient memory usage;
- mmap where appropriate;
- lazy loading where possible;
- efficient lists;
- отсутствие ненужных JVM/native copies;
- своевременное освобождение native resources.

---

# 35. Future Extensibility

Целевая структура:

```text
Archive
 ├── WAD
 ├── ZIP
 ├── PK3
 └── future formats
```

**и:**
```text
Entry
 ├── Text
 ├── Image
 ├── Graphic
 ├── Palette
 ├── Texture
 ├── Audio
 ├── Binary
 └── Map
```

Добавление нового формата или редактора не должно требовать переписывания archive browser.

---

# 36. Final Vision

Конечный продукт должен восприниматься как самостоятельный Android-инструмент:

> **SLADE Mobile — мобильный редактор Doom/WAD-ресурсов, использующий SLADE как backend.**

Пользовательский путь:

```text
Open Archive
     ↓
Browse
     ↓
Select Resource
     ↓
Preview / Edit
     ↓
Save
     ↓
Validate
     ↓
Use in Doom
```

Map Editor является последней крупной подсистемой проекта.
