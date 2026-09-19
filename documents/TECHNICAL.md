# TECHNICAL.md — SLADE Mobile

Техническая справка по текущему проекту SLADE Mobile.

> **Язык документа:** русский. Названия классов, методов, библиотек и команд сохраняются в оригинальном виде.

## 1. Идентификация проекта

**Проект:** SLADE 3 Mobile  
**Назначение:** Android-приложение для просмотра и редактирования WAD-архивов с использованием headless-компонентов SLADE 3.

**Текущая база SLADE:** `SLADE 3.3.0 Beta 2` — зафиксированное в проектной документации значение; при обновлении исходного дерева SLADE его необходимо перепроверять.

Текущий нативный путь ориентирован на WAD. Полноценный PK3/ZIP-путь пока не считается поддержанным.

## 2. Среда разработки

### Основная IDE

**Android Code Studio**.

Официальный репозиторий: https://github.com/AndroidCSOfficial/android-code-studio

Проект рассчитан на сборку непосредственно на Android.

## 3. Инструментарий Android

| Компонент | Версия / настройка |
|---|---|
| Минимальный Android SDK | API 24 |
| Target SDK | API 34 |
| Compile SDK | API 36 |
| NDK | `28.2.13676358` |
| CMake | `4.1.2` |
| Gradle Wrapper | `9.0.0` |
| Android Gradle Plugin | `8.13.0` |
| Kotlin | `2.1.0` |
| Java / целевая JVM | `17` |
| Нативный язык | C++ |
| Язык Android-приложения | Kotlin |
| Стандарт C++ | C++20 |
| Основной ABI | `arm64-v8a` |

## 4. Система нативной сборки

Нативный код собирается через Android Gradle Plugin + CMake + NDK.

Точка входа:

```text
app/src/main/cpp/CMakeLists.txt
```

Создаются две основные цели:

```text
slade_core       — STATIC
myapplication    — SHARED
```

`slade_core` содержит выбранные headless-компоненты SLADE, включая `Archive`, `ArchiveEntry`, `ArchiveManager`, `WadArchive` и необходимые классы `Utility`.

`myapplication` содержит JNI-мост и связывает `slade_core` с Kotlin-частью Android-приложения.

## 5. Интеграция SLADE

Исходный код SLADE находится в:

```text
app/src/main/cpp/third_party/SLADE/
```

Android-совместимость находится в:

```text
app/src/main/cpp/compat/
```

В `CMakeLists.txt` намеренно не включён `ZipArchive.cpp`, потому что desktop-реализация зависит от wxWidgets ZIP streams. Поэтому текущая нативная сборка не должна описываться как универсальная система архивов.

## 6. Текущая архитектура Kotlin ↔ native

Фактическая схема:

```text
Android UI / MainActivity
        ↓
SladeNative.kt
        ↓
JNI / native-lib.cpp
        ↓
ArchiveSession
        ↓
WadArchive
        ↓
SLADE headless core
```

`ArchiveSession` существует уже сейчас, но является WAD-ориентированной: она владеет `MemChunk*` и `WadArchive*` и отслеживает состояние изменённости.

Целевая архитектура с `ViewModel`, `Repository`, отдельными native services и универсальным `Archive` пока является планом структурного рефакторинга.

## 7. Многопоточность

Все вызовы native API из `SladeNative.kt` проходят через один `Executors.newSingleThreadExecutor()` и соответствующий coroutine dispatcher.

Это сознательная сериализация доступа: открытая `ArchiveSession` содержит изменяемое native-состояние и не рассчитана на произвольный параллельный доступ.

UI-поток не должен выполнять тяжёлые операции открытия, чтения, декодирования и сохранения напрямую.

## 8. Работа с файлами Android

### Открытие

```text
SAF Uri
   ↓
ParcelFileDescriptor
   ↓
detachFd()
   ↓
native fstat / mmap
   ↓
MemChunk
   ↓
WadArchive
```

В Kotlin не создаётся промежуточный полный `ByteArray` всего архива.

Текущая схема не является zero-copy: данные из отображённого файла могут копироваться в `MemChunk` SLADE и другие native-структуры.

## 9. Текущие операции WAD

В текущем приложении реализованы:

- открыть WAD;
- просмотреть записи;
- получить данные и предварительный просмотр;
- rename;
- delete;
- move;
- add;
- replace;
- export;
- отслеживать dirty state;
- Save As;
- сохранить изменения в исходный документ;
- Discard Changes.

## 10. Конвейер сохранения

### 10.1. Save As

Путь:

```text
изменённая ArchiveSession
        ↓
сериализация в MemChunk
        ↓
валидация сериализованных данных
        ↓
SAF ACTION_CREATE_DOCUMENT
        ↓
запись нового файла
```

### 10.2. Сохранение в исходный файл

Путь:

```text
изменённая ArchiveSession
        ↓
validateForSave()
        ↓
сериализация + валидация в памяти
        ↓
SAF openFileDescriptor("rwt")
        ↓
commitSave(fd)
        ↓
обновление dirty state
```

Критическое правило: `validateForSave()` должен завершиться успешно **до** открытия исходного документа в режиме `rwt`.

### 10.3. Что именно проверяется

Валидация создаёт сериализованные байты в памяти и затем независимо пытается разобрать их через `WadArchive`. Проверяется успешное открытие и согласованность количества записей.

### 10.4. Ограничение атомарности

Текущий путь **не является атомарным replace**.

`rwt` может привести к немедленной обрезке исходного файла до фактической записи новых байтов. Поэтому сбой процесса или прерывание записи после открытия `rwt` может оставить исходный документ обрезанным или частично записанным.

Это ограничение текущего SAF-пути, а не утверждение о полной transactional/atomic save-системе.

## 11. Discard Changes

Discard не записывает изменения на диск. Native-сессия заново разбирает исходные данные и восстанавливает состояние открытого WAD без закрытия пользовательского документа.

После Discard UI должен заново получить список записей и состояние dirty flag.

## 12. Предварительный просмотр ресурсов

Native-слой предоставляет данные для:

- текста;
- PNG;
- палитры;
- flat 64×64;
- Doom Graphic / patch;
- метаданных аудио нескольких форматов;
- необработанных данных записей.

PNG декодируется на Android после получения PNG-данных из native-кода.

Полноценные редакторы ресурсов и воспроизведение аудио остаются задачами дальнейших этапов.

## 13. Определение типа записи

Сейчас используется `androidDetectEntryType()` из `compat/slade_shims.cpp`.

Это временный WAD-ориентированный классификатор на основе имени и сигнатур. Он не является полной заменой системы `EntryType` SLADE и не должен рассматриваться как готовая архитектура для PK3/ZIP.

## 14. Сохранение данных записей

Для WAD существует различие между исходными байтами и данными изменённой записи. При рефакторинге необходимо сохранить инвариант:

```text
неизменённая запись → исходные/session bytes
изменённая запись   → данные ArchiveEntry
```

Упрощение этого условия может привести к чтению устаревших данных после редактирования.

## 15. Структура репозитория

```text
SLADE_3_MOBILE/
├── app/
│   └── src/main/
│       ├── cpp/
│       │   ├── compat/
│       │   ├── third_party/SLADE/
│       │   ├── CMakeLists.txt
│       │   └── native-lib.cpp
│       └── kotlin/
│           └── com/oleglati/slade_3_mobile/
│               ├── MainActivity.kt
│               ├── SladeNative.kt
│               └── WadEntryAdapter.kt
├── documents/
├── gradle/libs.versions.toml
├── build.gradle
├── settings.gradle
├── gradle.properties
└── gradlew
```

## 16. Архитектурные ограничения

### Одна активная сессия

Текущая реализация рассчитана на одну активную архивную сессию.

### WAD-first

Основные операции редактирования и сохранения пока WAD-специфичны.

### PK3/ZIP

`ZipArchive.cpp` не входит в текущую native-сборку. Для PK3/ZIP потребуется отдельный Android-совместимый контейнерный слой и обобщение `ArchiveSession`.

### Размеры

При работе с размером файла на Kotlin-стороне следует предпочитать `Long`. Нативные интерфейсы должны явно учитывать ограничения используемых типов.

## 17. Целевой структурный рефакторинг

План:

```text
R0  Baseline / regression protection
R1  Kotlin state separation
R2  Native/JNI separation
R3  Archive API
R4  Typed data contract
R5  Compatibility layer cleanup
R6  Save pipeline isolation
R7  Undo/Redo foundation
R8  Manual WAD regression fixtures
R9  Performance profiling
R10 PK3/ZIP readiness
```

Подробности находятся в `documents/REFACTORING.md`.

## 18. Контрольный сценарий

Минимальный интеграционный сценарий для изменения архивного кода:

```text
Open
  ↓
Modify
  ↓
Save
  ↓
Close
  ↓
Reopen
  ↓
Verify
```

Для сохранения исходного файла дополнительно проверяется, что валидация завершается до открытия `rwt`, а после успешного commit dirty state сбрасывается.

## 19. Политика версий

При изменении сборочного или нативного окружения обновлять этот документ одновременно с кодом.

Необходимо фиксировать изменения:

- Android API;
- NDK;
- CMake;
- Gradle;
- Android Gradle Plugin;
- Kotlin;
- Java/JVM;
- стандарта C++;
- базовой версии SLADE;
- поддерживаемых ABI.
