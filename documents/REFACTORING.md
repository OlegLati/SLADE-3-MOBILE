# SLADE Mobile — план структурного рефакторинга

> Документ описывает постепенное отделение UI, состояния приложения, JNI и нативной предметной логики от уже работающего WAD MVP.
>
> **Язык документа:** русский. Имена классов, файлов, методов и этапов R0–R10 сохраняются в техническом виде.

## 1. Цель

Рефакторинг должен уменьшить связанность проекта и остановить рост центральных файлов, не ломая работающий WAD MVP.

Целевая схема:

```text
Android UI
    ↓
ViewModel / UI State
    ↓
Archive Repository
    ↓
SladeNative
    ↓ JNI
Native adapter
    ↓
ArchiveSession / services
    ↓
SLADE Core
```

Рефакторинг не является переписыванием проекта с нуля.

## 2. Текущее состояние

На текущем этапе уже существуют:

- `MainActivity.kt` как основной Android-контроллер UI, состояния и части операций;
- `SladeNative.kt` как единая Kotlin/native граница;
- один выделенный native executor;
- `NativeResult`;
- `ArchiveSession` в `native-lib.cpp`;
- WAD-операции rename/delete/move/add/replace/export;
- Save As;
- `validateForSave()` и `commitSave()` для сохранения в исходный документ;
- `discardChanges()`.

Текущая архитектура ещё не содержит полноценного `ViewModel`, `Repository`, отдельного `SaveService` или универсального `Archive` API.

## 3. Главные точки концентрации

### `MainActivity.kt`

Сейчас совмещает UI, состояние открытого архива, SAF, dirty state, save/discard, операции над записями, preview orchestration, dialogs и запуск coroutine-операций.

Цель — оставить Activity преимущественно слоем Android UI и dispatch пользовательских действий.

### `SladeNative.kt`

Уже является полезной границей. Следующий шаг — сделать контракт ещё более типизированным и независимым от конкретных экранов.

### `native-lib.cpp`

Сейчас совмещает JNI, `ArchiveSession`, операции с WAD, preview helpers, type detection и save pipeline.

Цель — отделить JNI-маршалинг от native domain logic.

### `ArchiveSession`

Уже существует, но конкретно ориентирована на `WadArchive`. Универсализация должна быть отложена до реального потребителя, прежде всего до PK3/ZIP.

## 4. Таблица перехода

| Текущее состояние | Целевое состояние |
|---|---|
| `MainActivity` управляет UI + state + operations | Activity управляет UI и событиями |
| `SladeNative` содержит suspend-обёртки и raw JNI | стабильный типизированный native API |
| `native-lib.cpp` совмещает JNI и domain | JNI-маршалинг отдельно от domain |
| `ArchiveSession` WAD-specific | форматно-независимая сессия при появлении потребителя |
| Save pipeline в native/JNI коде | отдельный SaveService / save boundary |
| Undo/Redo отсутствует | command history |
| native tests отсутствуют как завершённый набор | автоматизированные native/regression tests |

## 5. Правила

### Правило 1 — один extraction за раз

```text
extract → compile → run → regression → commit
```

### Правило 2 — структура отдельно от поведения

Не менять save semantics, формат данных и UI одновременно с обычным extraction, если это не требуется для компиляции.

### Правило 3 — сначала ownership

Сначала определить, кто владеет данными, кто изменяет данные, кто сериализует и кто сохраняет.

### Правило 4 — не переписывать third_party/SLADE

Изменения внешнего SLADE должны быть минимальными и отдельно обоснованными.

### Правило 5 — не создавать пустые абстракции

Новая абстракция появляется только при реальном втором потребителе или при явном уменьшении текущей связанности.

## 6. R0 — фиксация baseline и защита от регрессий

**Статус:** в работе; native regression slice уже выделен и расширен.

Перед крупным рефакторингом зафиксировать успешную debug-сборку и сценарии Open → Browse → Preview, Rename, Delete, Move, Replace, Add, Export, Discard, Save As и сохранение в исходный файл. Native regression target уже покрывает WAD open/read/serialize, session lifecycle, discard, pending save, validation, archive operations и ключевые preview boundaries.

Минимальный контрольный сценарий после каждой крупной стадии:

```text
Open → Edit → Save → Close → Reopen → Verify
```

## 7. R1 — разделение Kotlin UI и состояния

Ввести модели состояния и `ArchiveViewModel` только после фиксации R0.

Цель:

```text
MainActivity
    ↓
UI events / rendering
    ↓
ArchiveViewModel
    ↓
SladeNative
```

## 8. R2 — разделение JNI и native domain

Разделить JNI entry point, native adapter и `ArchiveSession`/services. JNI-функции должны заниматься преобразованием аргументов и результатов, а не алгоритмами работы с архивом.

## 9. R3 — Archive API

Постепенно заменить набор WAD-ориентированных границ на более общие операции `openArchive`, `listEntries`, `readEntry`, `modifyEntry`, `save`, не объявляя PK3/ZIP поддержанными до появления реальной реализации.

## 10. R4 — типизированный контракт данных

Целевая модель записи:

```text
Entry
 ├── id
 ├── name
 ├── type
 ├── size
 ├── metadata
 └── data
```

## 11. R5 — compatibility layer

`compat/` остаётся тонким адаптером Android ↔ SLADE. Новая archive implementation и application logic туда не переносятся.

## 12. R6 — изоляция save pipeline

Выделить единый слой, который явно различает Save As, Save in-place, Discard, Validate и Commit.

Обязательный порядок in-place:

```text
serialize → validate → open rwt → commit
```

Текущая реализация остаётся честно описанной как неатомарная.

## 13. R7 — основа Undo / Redo

Ввести command/history model для Rename, Replace, Add, Delete и Move после стабилизации ownership состояния `ArchiveSession`.

## 14. R8 — native tests и CI

Расширить существующий native regression slice fixture-файлами для нормального, пустого, большого, повреждённого и обрезанного WAD; затем подключить воспроизводимый CI-запуск native target. На текущем этапе тесты остаются in-memory и запускаются через отдельный `native_wad_tests` target.

## 15. R9 — профилирование

После стабилизации границ измерять время открытия, чтение больших записей, preview decode, сериализацию, память и дополнительные native-копирования. Оптимизировать только измеренные узкие места.

## 16. R10 — подготовка PK3/ZIP

Перед полноценным PK3/ZIP необходимо решить ZIP/container implementation без wxWidgets, обобщить `ArchiveSession`, убрать WAD-only assumptions, определить универсальный EntryType/metadata contract и адаптировать save pipeline.

## 17. Что не делать

Не следует переписывать `MainActivity` или `native-lib.cpp` целиком, массово менять поведение и структуру одним коммитом, переносить desktop wxWidgets, объявлять PK3/ZIP готовыми до реализации или вводить абстракции без реального потребителя.

## 18. Definition of Done

Этап считается завершённым, когда проект собирается, WAD MVP продолжает работать, regression slice проходит, ownership слоя очевиден, JNI не содержит вынесенную domain logic, документация соответствует коду и следующая граница рефакторинга определена.
