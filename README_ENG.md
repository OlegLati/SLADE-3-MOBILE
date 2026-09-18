# SLADE 3 MOBILE

SLADE 3 MOBILE is an Android port/integration of the [SLADE](https://github.com/sirjuddington/SLADE) editor, focused on WAD editing and related Doom-engine resource workflows on Android devices.

The project is developed directly on Android, with Android Code Studio as the primary development environment. The goal is to make practical SLADE functionality available without requiring a desktop PC.

> **Project status:** WAD Editor MVP completed; current stage is structural refactoring  
> **Current baseline:** `v0.1.0`

## Features

The current MVP includes:

- WAD archive opening and browsing
- WAD entry listing
- Preview support for supported resource types
- Entry rename
- Entry deletion
- Entry move
- Entry addition
- Entry replacement
- Entry export
- Dirty-state tracking
- Save As through Android Storage Access Framework (SAF)
- In-place save after pre-save validation
- Discard Changes
- Native C++ integration with the SLADE codebase
- Android-specific compatibility/shim layer

The current native editing path is **WAD-first**. PK3/ZIP support is not complete in the current native path and remains a future-stage task.

### Important save semantics

Save As creates a new file through SAF. In-place saving follows `validate → commit`: the archive is serialized and validated in memory first, and only then are the validated bytes written to the original document opened through SAF in `rwt` mode.

This path is **not an atomic file replacement**. If writing the original document is interrupted after it has been opened in `rwt` mode, the original file may be truncated or partially written. The SAF limitation and the lack of atomic replacement are therefore explicit parts of the current architecture.

## Why this project exists

SLADE is a powerful editor for Doom-engine and related game resources, but its traditional workflow assumes access to a desktop PC.

SLADE 3 MOBILE explores what can be done when that workflow is brought to Android.

The project is not intended to replace the desktop version immediately. Instead, it aims to provide a useful mobile editor while gradually building a maintainable Android-native architecture around the SLADE codebase.

## Project Status

### Completed

- Initial Android project and native integration
- WAD archive editor MVP
- WAD opening, browsing and previews
- Rename / delete / move / add / replace / export
- Save As
- Save validation
- In-place save through `validate → commit`
- Discard Changes
- Android SAF-based file access
- Initial third-party license audit
- Git repository and GitHub integration
- `v0.1.0` WAD Editor MVP baseline

### Current stage

- Structural architectural refactoring
- Separation of Android UI, application state, repository/domain logic and native archive operations
- Further separation of JNI from native domain logic
- Stronger typed Kotlin/native data contracts
- Preparation of native automated tests
- Foundation for Undo/Redo

### Planned

- Improved performance and memory handling after ownership boundaries are stable
- More resource editors
- Full PK3/ZIP support
- Broader archive abstraction
- More complete mobile-oriented workflows

See [`documents/ROADMAP.md`](documents/ROADMAP.md) for the detailed roadmap.

## Architecture

### Current architecture

The actual implementation currently follows this structure:

```text
Android UI / MainActivity
        │
        ▼
SladeNative.kt
        │
        ▼
JNI / native-lib.cpp
        │
        ▼
ArchiveSession
        │
        ▼
WadArchive / SLADE headless core
```

`ArchiveSession` already exists, but it is currently a WAD-oriented native session. Kotlin calls are routed through one dedicated background thread to prevent concurrent access to mutable native state.

### Target architecture

The structural refactoring is gradually moving toward:

```text
Android UI
    │
    ▼
ViewModel / UI State
    │
    ▼
Archive Repository / application layer
    │
    ▼
SladeNative / JNI
    │
    ▼
Native Archive Layer
    ├── ArchiveSession
    ├── ArchiveOperations
    ├── EntryData
    └── Save Pipeline
    │
    ▼
SLADE Core
```

`ViewModel`, `Repository` and separate `ArchiveOperations` are **target architecture**, not claims that these components already exist in the current codebase.

## Technology Stack

- **Platform:** Android
- **Minimum Android API:** 24
- **Target Android API:** 34
- **Compile SDK:** 36
- **ABI:** `arm64-v8a`
- **Language:** Kotlin + C++20
- **Native build:** CMake
- **Build system:** Gradle / Android Gradle Plugin
- **Native code:** selected headless SLADE components + Android compatibility layer
- **Storage:** Android Storage Access Framework (SAF)

The primary development environment is **Android Code Studio**. Exact SDK/NDK/CMake and other tool versions are documented in [`documents/TECHNICAL.md`](documents/TECHNICAL.md).

## Repository Structure

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
│   ├── DESIGN.md
│   ├── ROADMAP.md
│   ├── TECHNICAL.md
│   ├── THIRD_PARTY_NOTICES.md
│   └── REFACTORING.md
├── README.md
├── README_ENG.md
├── SECURITY.md
├── build.gradle
└── settings.gradle
```

### Third-party code

The repository contains the SLADE source tree and several third-party libraries.

Important license files are intentionally kept with their respective source trees.

See [`documents/THIRD_PARTY_NOTICES.md`](documents/THIRD_PARTY_NOTICES.md) for the third-party license inventory.

## Building

The project is intended to be buildable directly on Android using Android Code Studio.

The exact local SDK/NDK/CMake versions are documented in [`documents/TECHNICAL.md`](documents/TECHNICAL.md).

A typical development workflow is:

```text
Open project in Android Code Studio
        ↓
Sync Gradle
        ↓
Build
        ↓
Install APK
        ↓
Test on Android device
```

Desktop build instructions may be added later if the development workflow expands to additional platforms.

## Installation

1. Open the Releases page.
2. Download the latest `app-debug.arm64-v8a.apk`.
3. Install the APK on an ARM64 (`arm64-v8a`) Android device.
4. Launch SLADE 3 Mobile.

### Requirements

- ARM64 (`arm64-v8a`) Android device.
- Android API 24 or newer.

This is a debug build intended for testing. Keep backups of important WAD files before editing.

## Development Workflow

Git is used to keep the project recoverable while the architecture is being refactored.

The stable baseline is:

```text
master
└── v0.1.0
    └── WAD Editor MVP
```

Project rule: new work is performed in dedicated feature/refactoring branches rather than directly on the stable baseline.

Example:

```bash
git switch -c refactor/architecture
```

Small, focused commits are preferred so that individual changes can be reviewed, tested and reverted independently.

## Documentation

Project documentation is kept in [`documents/`](documents/). Documents in that directory are maintained in Russian; this English README is the English entry point for the project.

- [`DESIGN.md`](documents/DESIGN.md) — architecture and design principles
- [`ROADMAP.md`](documents/ROADMAP.md) — development roadmap and milestones
- [`TECHNICAL.md`](documents/TECHNICAL.md) — technical stack and implementation details
- [`REFACTORING.md`](documents/REFACTORING.md) — structural refactoring strategy
- [`documents/THIRD_PARTY_NOTICES.md`](documents/THIRD_PARTY_NOTICES.md) — third-party software and license inventory

## Licensing

SLADE is distributed under the **GNU General Public License, version 2 (GPL-2.0)**.

This repository also contains third-party components under their own licenses, including MIT-licensed libraries, public-domain code, and components with their own permissive license terms.

The original license and copyright notices for third-party components are preserved with the corresponding source code.

See:

- [`app/src/main/cpp/third_party/SLADE/LICENSE`](app/src/main/cpp/third_party/SLADE/LICENSE)
- [`documents/THIRD_PARTY_NOTICES.md`](documents/THIRD_PARTY_NOTICES.md)

> **Important:** The overall licensing boundary between project-owned Android code and integrated GPL-covered SLADE code is intentionally not presented here as a single new repository-wide license. The licensing structure should be reviewed before publishing a final distribution under a new overall licensing scheme.

## Project Philosophy

1. **Android first** — development must remain possible without a desktop PC.
2. **Preserve working functionality** — refactoring should not unnecessarily break the existing MVP.
3. **Use the SLADE core** — avoid rewriting mature functionality without a strong reason.
4. **Keep boundaries clear** — Android UI, application state, JNI and native archive logic should have explicit responsibilities.
5. **Test before expanding** — structural changes should be validated before adding large new features.
6. **Small Git commits** — every significant change should remain easy to inspect and revert.
7. **Plan for growth** — the architecture should eventually support more archive formats and resource editors without turning the Android layer into a monolith.

## Disclaimer

SLADE 3 MOBILE is an independent Android project/integration and is not presented as the official desktop SLADE project.

SLADE and its associated source code remain subject to their respective copyrights and licenses.
