# SLADE Mobile — Refactoring Plan

> Подробный план структурного рефакторинга SLADE Mobile. Документ описывает не
> переписывание проекта с нуля, а последовательную миграцию от работающего WAD
> MVP к поддерживаемой архитектуре, в которой UI, application state, native
> adapter и SLADE Core имеют чёткие границы.

## Содержание

- [1. Цель документа](#1-цель-документа)
- [2. Почему рефакторинг нужен сейчас](#2-почему-рефакторинг-нужен-сейчас)
- [3. Текущее состояние](#3-текущее-состояние)
- [4. Главные архитектурные проблемы](#4-главные-архитектурные-проблемы)
- [5. Целевое устройство проекта](#5-целевое-устройство-проекта)
- [6. Правила рефакторинга](#6-правила-рефакторинга)
- [7. Phase R0 — Baseline и защита от регрессий](#7-phase-r0--baseline-и-защита-от-регрессий)
- [8. Phase R1 — Kotlin: разделение UI и application state](#8-phase-r1--kotlin-разделение-ui-и-application-state)
- [9. Phase R2 — Native: разделение JNI и domain logic](#9-phase-r2--native-разделение-jni-и-domain-logic)
- [10. Phase R3 — Archive API](#10-phase-r3--archive-api)
- [11. Phase R4 — Data model и native/Kotlin contract](#11-phase-r4--data-model-и-nativekotlin-contract)
- [12. Phase R5 — Compatibility layer](#12-phase-r5--compatibility-layer)
- [13. Phase R6 — Save pipeline](#13-phase-r6--save-pipeline)
- [14. Phase R7 — Undo/Redo foundation](#14-phase-r7--undoredo-foundation)
- [15. Phase R8 — Testing и CI](#15-phase-r8--testing-и-ci)
- [16. Phase R9 — Performance pass](#16-phase-r9--performance-pass)
- [17. Phase R10 — Preparation for PK3/ZIP](#17-phase-r10--preparation-for-pk3zip)
- [18. Предлагаемая структура каталогов](#18-предлагаемая-структура-каталогов)
- [19. План миграции MainActivity](#19-план-миграции-mainactivity)
- [20. План миграции native-lib.cpp](#20-план-миграции-native-libcpp)
- [21. Что не надо делать](#21-что-не-надо-делать)
- [22. Definition of Done](#22-definition-of-done)
- [23. Контрольные точки](#23-контрольные-точки)

---

# 1. Цель документа

Цель рефакторинга — уменьшить связанность проекта и остановить рост
нескольких центральных файлов, не ломая уже работающий WAD Editor MVP.

Основные результаты:

```text
MainActivity.kt
    ↓
UI / event handling only

SladeNative.kt
    ↓
stable Kotlin/native boundary

ArchiveViewModel
    ↓
application state + commands

ArchiveRepository
    ↓
document/session operations

JNI bridge
    ↓
marshalling only

ArchiveSession / services
    ↓
native domain logic

SLADE Core
    ↓
formats / archive implementation
```

Рефакторинг считается успешным, если добавление нового редактора или нового
архивного формата не требует снова превращать `MainActivity.kt` или
`native-lib.cpp` в центральный файл проекта.

---

# 2. Почему рефакторинг нужен сейчас

Проект уже прошёл наиболее рискованный этап: есть рабочий end-to-end WAD
workflow.

Сейчас изменение архитектуры относительно безопасно:

```text
Open
 ↓
Browse
 ↓
Edit
 ↓
Save
 ↓
Reopen
 ↓
Verify
```

уже существует как проверяемый сценарий.

Если продолжать добавлять функции непосредственно в текущие крупные файлы,
рост будет примерно таким:

```text
MainActivity
    ├── WAD
    ├── dialogs
    ├── save
    ├── previews
    ├── editors
    ├── search
    ├── navigation
    └── state

native-lib.cpp
    ├── JNI
    ├── session
    ├── entries
    ├── save
    ├── type detection
    ├── previews
    ├── decoders
    └── format-specific code
```

После появления Text/Image/Texture/Audio/Hex/PK3 этот подход станет дорогим.

**Вывод:** производительность можно пока не оптимизировать глубоко, но
структурный рефакторинг уже пора начинать.

---

# 3. Текущее состояние

По состоянию на архив, наибольшие точки концентрации ответственности:

```text
app/src/main/kotlin/.../MainActivity.kt
    ≈ 964 строки

app/src/main/cpp/native-lib.cpp
    ≈ 1729 строк

app/src/main/cpp/compat/android_compat.h
    ≈ 789 строк

app/src/main/cpp/compat/slade_shims.cpp
    ≈ 793 строк
```

Kotlin API уже имеет полезную границу:

```text
SladeNative
    ↓
single-thread native executor
    ↓
external JNI methods
```

А native слой уже имеет `ArchiveSession`, что является хорошей основой для
дальнейшего разделения.

Следовательно, проект не следует переписывать. Нужно **извлекать уже
существующие ответственности**.

---

# 4. Главные архитектурные проблемы

## 4.1. MainActivity слишком много знает

Текущая Activity одновременно управляет:

- UI;
- application state;
- SAF;
- dirty state;
- save/discard;
- entry operations;
- preview selection;
- dialogs;
- coroutine jobs;
- loading/busy state;
- преобразованием данных в UI-модель.

Цель:

```text
MainActivity
    ↓
render state
dispatch user actions
handle Android lifecycle
```

---

## 4.2. native-lib.cpp совмещает JNI и backend

В одном translation unit сейчас находятся:

- JNI entry points;
- `ArchiveSession`;
- entry data access;
- serialization;
- preview helpers;
- type detection;
- resource-specific decoding;
- save pipeline.

Цель:

```text
JNI
 ↓
Archive API
 ↓
ArchiveSession / services
 ↓
SLADE
```

JNI-функция не должна содержать бизнес-логику.

---

## 4.3. Native/Kotlin API слишком WAD-specific

Текущий API содержит методы вроде:

```text
openWad
entryText
entryPalette
entryImage
nativeMoveEntry
nativeSaveToFd
```

Для WAD MVP это нормально.

Для PK3/ZIP такой API начнёт протекать деталями реализации.

Цель:

```text
openArchive
listEntries
readEntry
modifyEntry
save
```

а формат определяется metadata/session, а не названием JNI-функции.

Миграция выполняется только после стабилизации WAD-пути.

---

## 4.4. Entry data имеет два источника

Для текущего WAD:

```text
original MemChunk
        +
loaded/modified ArchiveEntry data
```

`entryDataPtr()` уже учитывает эту разницу.

При рефакторинге необходимо сохранить инвариант:

```text
untouched entry
    → original mapped/session bytes

modified entry
    → ArchiveEntry-owned bytes
```

Нельзя "упростить" это условие удалением проверки `isLoaded()`.

---

## 4.5. Compatibility layer может стать вторым SLADE

`compat/` должен оставаться адаптером.

Нельзя постепенно складывать туда:

- новую archive implementation;
- новую type system;
- новую editor framework;
- Android business logic.

Если shim начинает содержать самостоятельную предметную область, нужно
выносить её в отдельный adapter/service и документировать, почему она
не может жить в SLADE Core.

---

# 5. Целевое устройство проекта

Целевая схема:

```text
┌─────────────────────────────────────────────┐
│ Android UI                                  │
│ Activity / Fragments / dialogs / adapters   │
└──────────────────────┬──────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────┐
│ Presentation / ViewModel                    │
│ ArchiveUiState + user intents               │
└──────────────────────┬──────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────┐
│ Application layer                           │
│ ArchiveRepository / editor orchestration    │
└──────────────────────┬──────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────┐
│ Kotlin Native API                           │
│ typed calls + coroutine boundary            │
└──────────────────────┬──────────────────────┘
                       │ JNI
                       ▼
┌─────────────────────────────────────────────┐
│ Native adapter                              │
│ JNI marshalling + Android fd/SAF boundary   │
└──────────────────────┬──────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────┐
│ Native domain                               │
│ ArchiveSession / operations / save / read   │
└──────────────────────┬──────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────┐
│ SLADE headless core                         │
└─────────────────────────────────────────────┘
```

---

# 6. Правила рефакторинга

## Правило 1 — один шаг за раз

Каждый extraction должен:

```text
extract
 ↓
compile
 ↓
run
 ↓
execute regression slice
 ↓
commit
```

Не объединять несколько крупных архитектурных миграций в один неотлаженный
шаг.

## Правило 2 — не менять поведение одновременно со структурой

Если задача называется:

```text
Extract ArchiveSession
```

она не должна одновременно:

- менять формат данных;
- менять save semantics;
- менять JNI contract;
- менять UI.

Исключение — маленькие изменения, необходимые для компиляции.

## Правило 3 — сначала границы, потом оптимизация

Не надо сейчас оптимизировать каждую копию памяти.

Сначала сделать:

```text
who owns data?
who mutates data?
who saves data?
```

однозначными.

## Правило 4 — upstream SLADE не рефакторить без необходимости

`third_party/SLADE/` считается внешним исходным кодом.

Изменения там должны быть:

- минимальными;
- отдельно обоснованными;
- максимально upstream-friendly.

## Правило 5 — не создавать абстракции "на будущее" без потребителя

Не вводить десять интерфейсов только потому, что когда-нибудь появится
Map Editor.

Абстракция появляется, когда есть реальная вторая реализация или когда
она явно снижает текущую связанность.

---

# 7. Phase R0 — Baseline и защита от регрессий

**Priority: P0**

**Status: NEXT**

Перед большим рефакторингом зафиксировать поведение текущей версии.

### Tasks

- [ ] Зафиксировать успешную сборку debug APK
- [ ] Зафиксировать текущий `Open → Browse → Preview`
- [ ] Зафиксировать `Rename → Save As → Reopen`
- [ ] Зафиксировать `Delete → Save As → Reopen`
- [ ] Зафиксировать `Move → Save As → Reopen`
- [ ] Зафиксировать `Replace → Save → Reopen`
- [ ] Зафиксировать `Discard`
- [ ] Зафиксировать запуск изменённого WAD во внешнем Doom engine
- [ ] Сохранить несколько fixture-файлов
- [ ] Зафиксировать ожидаемое количество entries для каждого fixture
- [ ] Зафиксировать checksum/размеры там, где это полезно

### Главное правило

После каждой крупной стадии должен проходить хотя бы:

```text
Open → Edit → Save → Close → Reopen → Verify
```

---

# 8. Phase R1 — Kotlin: разделение UI и application state

**Priority: P0**

**Status: PLANNED**

Первый настоящий extraction.

## R1.1. Создать модели

```text
model/
├── WadEntry.kt
├── ArchiveUiState.kt
├── ArchiveError.kt
└── EntryPreview.kt
```

Если `WadEntry` уже существует, не дублировать его — только перенести и
улучшить границу.

## R1.2. Создать ArchiveViewModel

ViewModel владеет:

- списком entries;
- selected entry;
- busy/loading state;
- dirty state;
- текущим document identity;
- ошибками;
- командами open/close/edit/save/discard.

Концептуально:

```kotlin
data class ArchiveUiState(
    val isOpen: Boolean = false,
    val isBusy: Boolean = false,
    val isDirty: Boolean = false,
    val entries: List<WadEntry> = emptyList(),
    val selectedEntry: WadEntry? = null,
    val error: String? = null
)
```

## R1.3. Убрать из Activity

По очереди:

```text
refreshEntryList()
refreshDirtyIndicator()
setBusy()
updateActionButtonsEnabled()
```

переносятся в state-driven модель.

Затем:

```text
save
discard
rename
delete
move
add
replace
export
```

переводятся на ViewModel commands.

## R1.4. Dialogs

Сначала можно оставить существующие Android dialogs.

Цель первой стадии не в идеальном UI, а в том, чтобы Activity перестала
быть владельцем application logic.

### R1 Done

```text
MainActivity
    ↓
collect/render ArchiveUiState
    ↓
dispatch intents
```

и при этом все существующие WAD операции продолжают работать.

---

# 9. Phase R2 — Native: разделение JNI и domain logic

**Priority: P0**

**Status: PLANNED**

Цель — уменьшить `native-lib.cpp` без изменения поведения.

## R2.1. ArchiveSession

Перенести существующий класс:

```text
ArchiveSession
```

из `native-lib.cpp` в:

```text
native/archive/ArchiveSession.h
native/archive/ArchiveSession.cpp
```

Сохранить:

- `MemChunk*`;
- `WadArchive*`;
- dirty;
- pending save;
- open/close/discard semantics.

На этой стадии не обобщать его до PK3.

## R2.2. Entry access

Вынести:

```text
entryDataPtr()
findPalette()
```

и связанные helpers в:

```text
native/archive/EntryData.cpp
native/archive/EntryData.h
```

## R2.3. Save

Вынести:

```text
serializeSession()
```

и pending-save mechanics в:

```text
native/save/SavePipeline.cpp
native/save/SavePipeline.h
```

## R2.4. Preview

Разделить:

```text
text
palette
graphic
flat
png
audio metadata
```

на соответствующие service/helper units.

Не создавать отдельный service для каждой функции, если две функции логически
образуют один decoder.

## R2.5. JNI

После extraction `native-lib.cpp` должен остаться тонким:

```text
JNIEXPORT ...
{
    return archiveApi....
}
```

JNI отвечает за:

- получение Java/Kotlin arguments;
- вызов native API;
- создание JNI return values;
- преобразование ошибок.

JNI не отвечает за:

- поиск entry;
- сериализацию;
- dirty logic;
- type detection;
- decoding algorithms.

---

# 10. Phase R3 — Archive API

**Priority: P0**

**Status: PLANNED**

После R1/R2 создать стабильную domain boundary.

Минимальный API:

```text
open
close
isOpen
listEntries
readEntry
renameEntry
deleteEntry
moveEntry
addEntry
replaceEntry
exportEntry
discard
validateSave
commitSave
```

## Важное ограничение

Пока API может быть WAD-oriented.

Не надо делать полноценный plugin architecture для архивов до появления
реального второго контейнера.

Но имена и ответственность должны быть такими, чтобы PK3 не потребовал
встраивать новый backend непосредственно в Activity.

---

# 11. Phase R4 — Data model и native/Kotlin contract

**Priority: P1**

**Status: PLANNED**

Сейчас список entries передаётся как массив строк.

Целевая модель:

```text
EntryDescriptor
 ├── index/id
 ├── name
 ├── size
 ├── type
 ├── flags
 └── metadata
```

## Почему

Строка вроде:

```text
"123|TITLEPIC|Graphic|12345"
```

может быть быстрым MVP-протоколом, но плохо масштабируется.

Typed contract позволит:

- PK3 paths;
- folders;
- namespaces;
- explicit type ids;
- metadata;
- stable IDs.

## Миграция

Сначала:

```text
Array<String>
```

оставить как legacy API.

Затем добавить typed API.

После миграции UI удалить legacy parser.

---

# 12. Phase R5 — Compatibility layer

**Priority: P1**

**Status: PLANNED**

Текущие:

```text
android_compat.h
slade_shims.cpp
```

не следует массово переписывать.

Сначала провести классификацию каждой функции:

```text
A — обязательный SLADE compatibility shim
B — Android platform adapter
C — временная заглушка
D — собственная domain logic
E — dead code
```

### Дальше

```text
A → оставить
B → platform/
C → задокументировать/удалить при возможности
D → вынести из compat/
E → удалить
```

Это уменьшит риск того, что `compat/` станет "вторым backend".

---

# 13. Phase R6 — Save pipeline

**Priority: P0/P1**

**Status: PARTIALLY IMPLEMENTED**

Текущий двухшаговый протокол сохранить:

```text
validate
 ↓
open original for write
 ↓
commit
```

Вынести его в отдельный компонент.

Целевая модель:

```text
SaveRequest
 ↓
Serialize
 ↓
Validate
 ↓
Commit
```

## Не нарушать

Нельзя открывать оригинальный SAF document в `rwt` до успешной validation,
поскольку это может привести к немедленному truncation.

## Дополнительно

- [ ] Единый error model
- [ ] Проверка serialized size
- [ ] Повторное открытие serialized bytes
- [ ] Проверка entry count
- [ ] Проверка критических offsets/sizes
- [ ] Явное состояние `pendingSave`
- [ ] Гарантированный clear после commit/failure

---

# 14. Phase R7 — Undo/Redo foundation

**Priority: P0**

**Status: PLANNED**

Undo/Redo не должен внедряться непосредственно в Activity.

Целевая модель:

```text
Command
 ├── execute()
 └── undo()
```

Например:

```text
RenameEntryCommand
DeleteEntryCommand
MoveEntryCommand
AddEntryCommand
ReplaceEntryCommand
```

Session владеет history:

```text
ArchiveSession
 ├── command history
 ├── undo stack
 └── redo stack
```

UI только вызывает:

```text
undo()
redo()
```

Это также подготовит архитектуру к resource editors.

---

# 15. Phase R8 — Testing и CI

**Priority: P0**

**Status: PLANNED**

Рефакторинг без тестов нельзя безопасно продолжать бесконечно.

## Native unit tests

Минимум:

```text
ArchiveSession
EntryData
Rename
Delete
Move
Add
Replace
Serialize
Discard
Save validation
```

## Regression fixtures

Создать:

```text
tests/fixtures/
├── doom.wad
├── small.wad
├── malformed.wad
├── empty.wad
└── modified.wad
```

## Property/invariant checks

Проверять:

```text
numEntries before/after
entry names
entry sizes
entry order
saved bytes reopenable
```

## CI

Минимальный pipeline:

```text
Gradle check
 ↓
CMake/native build
 ↓
native tests
 ↓
lint/static checks
```

---

# 16. Phase R9 — Performance pass

**Priority: P1**

**Status: AFTER STRUCTURAL REFACTOR**

Производительность оптимизировать только после стабилизации границ.

## Сначала измерить

Профилировать:

```text
open WAD
parse directory
type detection
first preview
large entry read
serialize
save
```

## Возможные оптимизации

- lazy type detection;
- cache entry metadata;
- avoid repeated full-directory scans;
- reduce large `ByteArray` transfers;
- reduce duplicate entry data;
- improve large-entry preview;
- avoid unnecessary UI list rebuilds.

## Важно

Не оптимизировать "по ощущениям".

Сначала:

```text
measure
 ↓
identify bottleneck
 ↓
optimize
 ↓
measure again
```

---

# 17. Phase R10 — Preparation for PK3/ZIP

**Priority: P1**

**Status: BLOCKED BY R3/R4**

До начала PK3:

- [ ] `ArchiveSession` не зависит напрямую от `WadArchive`
- [ ] archive operations имеют общий contract
- [ ] entry model поддерживает hierarchical names
- [ ] data access не предполагает file offset
- [ ] save pipeline не предполагает WAD directory serialization
- [ ] type detection не зависит от 8-символьного имени
- [ ] compatibility layer не содержит ZIP business logic

Целевая схема:

```text
ArchiveSession
      │
      ▼
ArchiveBackend
 ┌────┴─────┐
 ▼          ▼
WAD       ZIP/PK3
```

---

# 18. Предлагаемая структура каталогов

## Kotlin

После R1-R4:

```text
app/src/main/kotlin/com/oleglati/slade_3_mobile/
├── MainActivity.kt
│
├── model/
│   ├── ArchiveUiState.kt
│   ├── EntryPreview.kt
│   ├── WadEntry.kt
│   └── NativeError.kt
│
├── viewmodel/
│   └── ArchiveViewModel.kt
│
├── repository/
│   └── ArchiveRepository.kt
│
├── native/
│   ├── SladeNative.kt
│   └── NativeResult.kt
│
├── ui/
│   ├── archive/
│   ├── preview/
│   ├── dialogs/
│   └── components/
│
└── adapter/
    └── WadEntryAdapter.kt
```

Не требуется сразу создавать все перечисленные подпапки.

## Native

```text
app/src/main/cpp/
├── CMakeLists.txt
│
├── jni/
│   ├── native_archive.cpp
│   ├── native_entries.cpp
│   ├── native_preview.cpp
│   └── native_save.cpp
│
├── archive/
│   ├── ArchiveSession.cpp
│   ├── ArchiveSession.h
│   ├── ArchiveOperations.cpp
│   └── ArchiveOperations.h
│
├── data/
│   ├── EntryData.cpp
│   └── EntryData.h
│
├── preview/
│   ├── TextPreview.cpp
│   ├── ImagePreview.cpp
│   ├── PalettePreview.cpp
│   └── AudioInfo.cpp
│
├── save/
│   ├── SavePipeline.cpp
│   └── SavePipeline.h
│
├── compat/
│   └── ...
│
└── third_party/
    └── SLADE/
```

На ранних этапах допустимо оставить один JNI translation unit, если
разделение по файлам не даёт практической пользы. Важнее разделить
ответственности, чем получить много маленьких файлов.

---

# 19. План миграции MainActivity

## Шаг 1

Вынести модели.

```text
WadEntry
DialogData
preview data
```

## Шаг 2

Создать `ArchiveUiState`.

## Шаг 3

Создать `ArchiveViewModel`.

## Шаг 4

Перенести:

```text
open
close
rename
delete
move
add
replace
export
save
discard
```

в ViewModel/repository.

## Шаг 5

Перенести preview orchestration.

## Шаг 6

Оставить Activity только с:

```text
onCreate
view binding
collect state
render state
register ActivityResult launchers
navigation/dialog presentation
```

### Целевой размер

Не ставить жёсткую цель "Activity должна быть 200 строк".

Лучший критерий:

> Activity не должна содержать domain/application logic.

---

# 20. План миграции native-lib.cpp

## Current

```text
native-lib.cpp
├── session
├── JNI
├── data access
├── save
├── preview
└── detection
```

## Step 1

Вынести `ArchiveSession`.

## Step 2

Вынести data access.

## Step 3

Вынести serialization/save.

## Step 4

Вынести preview helpers.

## Step 5

Вынести type detection.

## Step 6

Разделить JNI entry points.

## Step 7

Оставить compatibility-specific calls только в adapter layer.

### Целевой результат

```text
native-lib.cpp
```

либо исчезает, либо становится тонким umbrella/registration file.

Не нужно искусственно стремиться к файлу на 50 строк. Но он не должен быть
местом, где "живёт весь проект".

---

# 21. Что не надо делать

## Не переписывать проект с нуля

Рабочий WAD pipeline — слишком ценный regression baseline.

## Не переходить на Compose только ради рефакторинга

View Binding не является причиной текущей архитектурной проблемы.

## Не делать multi-module Gradle architecture преждевременно

Сначала разделить ответственности внутри одного модуля.

Модули можно добавить позже, когда появится реальная граница.

## Не делать полноценную plugin system

Пока нет нескольких backend/editor implementations.

## Не переносить весь SLADE в отдельные собственные копии

Это усложнит обновление upstream.

## Не делать оптимизацию памяти раньше измерений

Особенно в местах:

```text
mmap → MemChunk → ArchiveEntry
```

Сначала определить реальный bottleneck.

## Не делать массовый rename всего проекта

Рефакторинг должен уменьшать риск, а не создавать огромный diff.

---

# 22. Definition of Done

## Structural DoD

```text
[ ] MainActivity не владеет archive business logic
[ ] ViewModel владеет UI/application state
[ ] Repository/API скрывает native details
[ ] JNI не содержит archive algorithms
[ ] ArchiveSession находится вне JNI translation unit
[ ] Save pipeline изолирован
[ ] Entry data access изолирован
[ ] Compatibility layer классифицирован
```

## Behavioral DoD

После каждой крупной миграции:

```text
[ ] Open WAD
[ ] Browse entries
[ ] Preview
[ ] Rename
[ ] Delete
[ ] Move
[ ] Add
[ ] Replace
[ ] Export
[ ] Discard
[ ] Save As
[ ] Save
[ ] Reopen
```

## Regression DoD

```text
[ ] WAD снова открывается
[ ] Entry count совпадает
[ ] Entry order сохраняется
[ ] Names сохраняются
[ ] Modified data сохраняется
[ ] Unmodified data не повреждается
[ ] IWAD protection не нарушена
[ ] UI не блокируется
[ ] Native session не используется конкурентно
```

---

# 23. Контрольные точки

## Checkpoint R0

```text
Рабочая версия зафиксирована.
```

## Checkpoint R1

```text
MainActivity стала presentation layer.
```

## Checkpoint R2

```text
native-lib.cpp больше не содержит основной backend.
```

## Checkpoint R3

```text
Archive API сформирован.
```

## Checkpoint R4

```text
Typed entry contract готов.
```

## Checkpoint R5

```text
compat layer классифицирован и стабилизирован.
```

## Checkpoint R6

```text
Save pipeline изолирован и покрыт тестами.
```

## Checkpoint R7

```text
Undo/Redo работает через commands, а не через UI.
```

## Checkpoint R8

```text
Native regression suite защищает core.
```

## Checkpoint R9

```text
Performance bottlenecks измерены и оптимизированы.
```

## Checkpoint R10

```text
Архитектура готова к второму archive backend.
```

---

# Итоговая стратегия

Рефакторинг следует выполнять не как отдельную "заморозку" разработки, а как
короткую архитектурную фазу перед дальнейшим расширением:

```text
Current WAD MVP
      │
      ▼
R0 — Baseline
      │
      ▼
R1 — Kotlin state separation
      │
      ▼
R2 — Native/JNI separation
      │
      ▼
R3 — Archive API
      │
      ▼
R4 — Typed data contract
      │
      ├──────────────► WAD features continue
      │
      ▼
R5 — Compat cleanup
      │
      ▼
R6 — Save pipeline
      │
      ▼
R7 — Undo/Redo
      │
      ▼
R8 — Native tests/CI
      │
      ▼
R9 — Performance
      │
      ▼
R10 — PK3/ZIP readiness
```

**Главный принцип:** не пытаться заранее построить идеальную архитектуру.
Нужно последовательно убрать реальные точки связанности, которые уже видны
в текущем проекте.

После R2/R3 добавление новых функций должно становиться заметно дешевле:
новый редактор работает через application/domain API, а не через очередной
набор условий в `MainActivity.kt` и `native-lib.cpp`.
