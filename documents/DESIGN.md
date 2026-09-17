# SLADE Mobile — Design Document

> Архитектурный документ проекта SLADE Mobile: Android UI + native adapter + headless SLADE Core.

Этот документ описывает архитектурные принципы и целевое устройство проекта.
Фактическое состояние реализации фиксируется в `documents/TECHNICAL.md`, а
последовательность разработки — в `documents/ROADMAP.md` и
`documents/REFACTORING.md`.

## Содержание

- [1. Overview](#1-overview)
- [2. Goals](#2-goals)
- [3. Non-Goals](#3-non-goals)
- [4. Core Architecture](#4-core-architecture)
- [5. Separation of Responsibilities](#5-separation-of-responsibilities)
- [6. Archive Abstraction](#6-archive-abstraction)
- [7. ArchiveSession](#7-archivesession)
- [8. File Access](#8-file-access)
- [9. Threading](#9-threading)
- [10. Native Resource Lifetime](#10-native-resource-lifetime)
- [11. Structural Refactoring Policy](#11-structural-refactoring-policy)
- [12. Entry Type System](#12-entry-type-system)
- [13. Editor Architecture](#13-editor-architecture)
- [14. Data vs Presentation](#14-data-vs-presentation)
- [15. Archive Editing](#15-archive-editing)
- [16. Save Architecture](#16-save-architecture)
- [17. Undo / Redo](#17-undo--redo)
- [18. Mobile UI](#18-mobile-ui)
- [19. Resource Editors](#19-resource-editors)
- [20. Map Architecture](#20-map-architecture)
- [21. Security & Robustness](#21-security--robustness)
- [22. Testing Philosophy](#22-testing-philosophy)
- [23. Performance](#23-performance)
- [24. Future Extensibility](#24-future-extensibility)
- [25. Final Vision](#25-final-vision)

## 1. Overview

**SLADE Mobile** — Android-приложение для просмотра и редактирования
Doom/WAD-ресурсов, использующее выбранные headless-компоненты SLADE 3.

Это не буквальный перенос desktop UI SLADE на Android. Android UI,
жизненный цикл, SAF и пользовательские workflows проектируются отдельно.

Целевая схема:

```text
SLADE 3 Source
      ↓
Headless SLADE Core
      ↓
Native Archive Layer
      ↓
JNI / Kotlin Native API
      ↓
Application State / ViewModel
      ↓
Android UI
```

## 2. Goals

Долгосрочная цель — полноценный мобильный инструмент для Doom-моддинга.
Он должен постепенно поддерживать:

- WAD и другие подходящие архивные форматы;
- просмотр, поиск и фильтрацию entries;
- импорт, экспорт, замену, удаление, переименование и перемещение;
- сохранение с проверкой результата;
- текстовые, графические, image, palette, texture, audio и binary editors;
- в будущем — Doom map workflows.

Текущий продуктовый scope остаётся WAD-first: расширение форматов идёт
после стабилизации базовой архитектуры.

## 3. Non-Goals

Проект не должен:

- портировать wxWidgets целиком;
- копировать desktop UI SLADE;
- переносить desktop-инфраструктуру без необходимости;
- превращать compatibility layer в независимую реализацию SLADE;
- переписывать зрелую доменную логику SLADE без технической причины.

## 4. Core Architecture

```text
┌─────────────────────────────┐
│ Android UI                  │
│ Activity / Views / Dialogs  │
└──────────────┬──────────────┘
               │
┌──────────────▼──────────────┐
│ Presentation / Application  │
│ ViewModel / UI State         │
│ Repository / orchestration   │
└──────────────┬──────────────┘
               │
┌──────────────▼──────────────┐
│ Kotlin Native API            │
│ SladeNative                  │
└──────────────┬──────────────┘
               │ JNI
┌──────────────▼──────────────┐
│ Native Archive Layer         │
│ Session / Operations / Save  │
└──────────────┬──────────────┘
               │
┌──────────────▼──────────────┐
│ SLADE Core                   │
│ Archives / Formats / Utils   │
└─────────────────────────────┘
```

Рефакторинг этой схемы выполняется постепенно. Рабочий WAD MVP не
переписывается с нуля.

## 5. Separation of Responsibilities

### SLADE Core

Отвечает за переиспользуемую доменную логику архивов, форматов и Doom
ресурсов. Не должен знать об Android UI.

### Native layer

Отвечает за JNI boundary, Android file descriptors, native lifetime,
archive session, сериализацию/валидацию и необходимые Android adapters.

### Kotlin application layer

Отвечает за UI, lifecycle, application state, SAF, coroutine orchestration
и преобразование domain data в presentation state.

Kotlin не должен зависеть от внутренних классов SLADE.

## 6. Archive Abstraction

Целевая доменная модель:

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

UI и application layer не должны знать, находится ли entry в WAD, PK3,
ZIP или другом контейнере.

В текущем MVP реализация всё ещё WAD-specific.

## 7. ArchiveSession

`ArchiveSession` представляет состояние редактируемого документа до
сохранения:

```text
ArchiveSession
 ├── source
 ├── archive data
 ├── archive
 ├── modifications
 └── dirty state
```

Сейчас существует одна активная session, конкретно связанная с
`WadArchive*`. Это осознанное ограничение WAD MVP. При добавлении второго
архивного формата session должна перейти к абстракции над конкретным
container type.

Undo/Redo не считается реализованной частью текущей session; это отдельный
будущий слой command/history model.

## 8. File Access

Android-файлы открываются через Storage Access Framework:

```text
SAF Uri
 ↓
ParcelFileDescriptor
 ↓
raw fd
 ↓
fstat / mmap
 ↓
MemChunk
 ↓
WadArchive
```

Большие архивы не должны без необходимости проходить через Kotlin
`ByteArray` и дополнительную JNI-копию.

Архитектура не является полностью zero-copy: SLADE `MemChunk` всё ещё
может содержать собственную копию данных.

## 9. Threading

Нативное состояние archive session не thread-safe. Поэтому все операции,
которые обращаются к текущей session, проходят через один выделенный
native executor:

```text
UI
 ↓
request
 ↓
single native executor
 ↓
JNI / ArchiveSession
 ↓
result
 ↓
UI
```

Это сохраняет инвариант: одновременно native session трогает только один
поток.

Coroutine cancellation может отменить queued работу, но не должна
предполагаться как механизм прерывания уже выполняющегося blocking JNI call.

## 10. Native Resource Lifetime

Все native resources должны иметь явный lifecycle:

```text
open → use → close → free
```

Особое внимание требуется для fd, mmap, `MemChunk`, `WadArchive`, native
handles и JNI references.

Не допускаются leaks, double free, dangling pointers и use-after-free.

## 11. Structural Refactoring Policy

Рефакторинг выполняется extraction-подходом:

```text
UI
 ↓
ViewModel / application state
 ↓
Repository / domain API
 ↓
Kotlin Native API
 ↓
JNI adapter
 ↓
Native domain
 ↓
SLADE Core
```

`MainActivity` не должна владеть archive business logic, а JNI не должен
становиться местом реализации самостоятельной archive domain logic.

Крупные файлы (`MainActivity.kt`, `native-lib.cpp`) уменьшаются постепенно,
путём извлечения существующих ответственностей.

Контрольные точки:

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

## 12. Entry Type System

Текущий WAD MVP использует временный Android-side classifier.

Цель:

```text
SLADE EntryType / domain knowledge
             ↓
      unified detection
             ↓
      Android presentation
```

Нельзя позволять временной detection logic стать второй постоянной
системой типов. При этом текущий WAD classifier не следует искусственно
обобщать под PK3/ZIP до появления соответствующего archive API.

## 13. Editor Architecture

Редактор выбирается по типу entry:

```text
Entry
 ↓
EntryType
 ↓
EditorRegistry
 ├── TextEditor
 ├── ImageEditor
 ├── GraphicEditor
 ├── PaletteEditor
 ├── TextureEditor
 ├── AudioEditor
 ├── HexEditor
 └── MapEditor
```

Archive browser не должен знать детали реализации отдельных редакторов.

## 14. Data vs Presentation

Native API должен постепенно переходить от UI-oriented методов к данным:

```text
readEntry()
    ↓
EntryData
    ↓
Kotlin
    ↓
appropriate editor
```

Специализированные preview helpers допустимы на текущей prototype/MVP
стадии, но не должны определять окончательную архитектуру native API.

## 15. Archive Editing

Мутирующие операции:

```text
add
remove
rename
move
replace
export
```

Они должны работать над session state, а не напрямую над исходным файлом.

В текущем MVP `add`, `remove`, `rename`, `move`, `replace` и `export`
реализованы через WAD `ArchiveSession`.

## 16. Save Architecture

Базовый принцип сохраняется:

```text
Original Archive
       ↓
ArchiveSession
       ↓
modifications
       ↓
serialized output
       ↓
validation
       ↓
Save As / Commit
```

### Save As

Save As всегда пишет в новый SAF destination и не требует записи в
исходный документ.

### Save in-place

Текущая реализация использует:

```text
serialize in memory
       ↓
independent validation/reopen
       ↓
open original URI as "rwt"
       ↓
commit validated bytes
```

Это защищает от записи заведомо невалидного сериализованного результата до
этапа validation.

Однако это **не атомарная файловая замена**: SAF не предоставляет
приложению универсальный `rename()` поверх произвольного выбранного
`content://` URI. Открытие `"rwt"` может усечь файл до завершения записи.

Поэтому документация не должна называть текущий in-place Save
"полностью crash-safe" или "atomic replacement".

IWAD намеренно не разрешается перезаписывать через in-place Save; для него
используется Save As.

## 17. Undo / Redo

Целевая модель — command/history layer:

```text
Command
 ├── Rename
 ├── Replace
 ├── Add
 ├── Delete
 └── Move

History
 ├── Undo
 └── Redo
```

Undo/Redo пока не является частью завершённого WAD MVP и не должен
описываться как уже реализованная функция.

## 18. Mobile UI

UI проектируется отдельно от desktop SLADE и учитывает:

- touch;
- небольшие экраны;
- tablets;
- portrait/landscape;
- long press;
- gestures;
- мобильные file workflows.

Archive browser должен оставаться независимым от конкретных редакторов.

## 19. Resource Editors

Планируемые редакторы включают:

- Text Editor;
- Image Editor;
- Doom Graphic Editor;
- Palette Editor;
- Texture Editor;
- Audio Browser/Player;
- Hex Editor.

Их реализация выполняется после прохождения соответствующих этапов
структурного рефакторинга и тестирования.

## 20. Map Architecture

Map Editor должен иметь отдельную архитектуру:

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

Планируемые map formats включают Doom/Doom II, Hexen, Boom и UDMF/ZDoom
варианты по мере развития проекта.

## 21. Security & Robustness

Любой внешний архив считается потенциально повреждённым.

Нельзя без проверки доверять:

- offsets;
- sizes;
- counts;
- dimensions;
- directory data.

Бинарные parser paths должны выполнять bounds checking до доступа к памяти.

## 22. Testing Philosophy

Функция считается готовой не только после успешного happy path.

Ключевой regression flow:

```text
Open
 ↓
Modify
 ↓
Save / Save As
 ↓
Close
 ↓
Reopen
 ↓
Verify
```

R0 должен превратить уже подтверждённые WAD workflows в воспроизводимый
baseline с fixtures, ожидаемыми entry counts и другими полезными
проверками.

## 23. Performance

Оптимизация выполняется после фиксации ownership и API boundaries.

Приоритеты:

- background parsing;
- разумное использование памяти;
- mmap там, где он действительно помогает;
- lazy loading там, где это уместно;
- эффективные списки;
- отсутствие ненужных Kotlin/native copies;
- своевременное освобождение native resources.

Профилирование предшествует целевым performance changes.

## 24. Future Extensibility

Целевая структура:

```text
Archive
 ├── WAD
 ├── ZIP
 ├── PK3
 └── future formats
```

и:

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

Добавление нового формата или редактора не должно требовать переписывания
archive browser и Android UI state model.

## 25. Final Vision

Конечный продукт должен восприниматься как самостоятельный Android-
инструмент:

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

Расширение до новых форматов, полноценных resource editors и map tooling
происходит после стабилизации WAD MVP и архитектурного фундамента.
