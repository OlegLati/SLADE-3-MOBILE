# SLADE 3 MOBILE

SLADE 3 MOBILE is an Android port/integration of the [SLADE](https://github.com/sirjuddington/SLADE) editor, focused on bringing WAD editing and related Doom-engine resource workflows to Android devices.

The project is developed directly on Android, with AndroidIDE as the primary development environment. The goal is to make practical SLADE functionality available without requiring a desktop PC.

> **Project status:** WAD Editor MVP / active development  
> **Current baseline:** `v0.1.0`

## Features

The current MVP includes:

- WAD archive opening and browsing
- WAD entry listing
- Entry selection and basic entry operations
- Preview support for supported resource types
- WAD editing
- Save
- Save As
- Android Storage Access Framework (SAF) integration
- Native C++ integration with the SLADE codebase
- Android-specific compatibility/shim layer

The project is currently **WAD-first**. Broader archive support such as PK3/ZIP is planned for a later stage.

## Why this project exists

SLADE is a powerful editor for Doom-engine and related game resources, but its traditional desktop workflow assumes access to a PC.

SLADE 3 MOBILE exists to explore what can be done when that workflow is brought to Android.

The project is not intended to replace the desktop version immediately. Instead, it aims to provide a useful mobile editor while gradually building a maintainable Android-native architecture around the proven SLADE codebase.

## Project Status

### Completed

- Initial Android project and native integration
- WAD archive editor MVP
- WAD entry browsing and operations
- Resource previews
- Android SAF-based file access
- Safe Save / Save As pipeline
- Initial third-party license audit
- Git repository and GitHub integration
- `v0.1.0` WAD Editor MVP baseline

### In progress

- Architectural refactoring
- Separation of Android UI, application state, repository logic and native archive operations
- Cleaner JNI boundary
- Stronger native archive/session abstractions
- Typed Kotlin/native data contracts
- Undo/Redo
- Native automated tests

### Planned

- Improved performance and memory handling
- More resource editors
- PK3/ZIP support
- Broader archive abstraction
- More complete mobile-oriented workflows

See [`documents/ROADMAP.md`](documents/ROADMAP.md) for the detailed roadmap.

## Architecture

The project currently combines Kotlin/Android code with the C++ SLADE core.

The intended architecture is gradually moving toward:

```text
Android UI
    │
    ▼
ViewModel / UI State
    │
    ▼
Archive Repository
    │
    ▼
SladeNative / JNI
    │
    ▼
Native Archive Layer
    │
    ├── ArchiveSession
    ├── ArchiveOperations
    ├── EntryData
    ├── Preview
    └── Save Pipeline
    │
    ▼
SLADE Core
```

The architecture is deliberately being refactored incrementally rather than rewriting the existing SLADE integration from scratch.

## Technology Stack

- **Platform:** Android
- **Minimum Android API:** 24
- **Target Android API:** 34
- **Compile SDK:** 36
- **ABI:** `arm64-v8a`
- **Language:** Kotlin + C++20
- **Native build:** CMake
- **Build system:** Gradle / Android Gradle Plugin
- **Native code:** SLADE + Android compatibility layer
- **Storage:** Android Storage Access Framework (SAF)

The project is primarily developed using **AndroidIDE**.

## Repository Structure

```text
SLADE_3_MOBILE/
├── app/
│   └── src/main/
│       ├── cpp/
│       │   ├── compat/
│       │   ├── third_party/
│       │   │   └── SLADE/
│       │   ├── native-lib.cpp
│       │   └── ...
│       └── java/
│           └── ...
├── documents/
│   ├── DESIGN.md
│   ├── ROADMAP.md
│   ├── TECHNICAL.md
│   └── REFACTORING.md
├── THIRD_PARTY_NOTICES.md
├── README.md
├── .gitignore
├── build.gradle
└── settings.gradle
```

### Third-party code

The repository contains the SLADE source tree and several third-party libraries.

Important license files are intentionally kept with their respective source trees.

See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the third-party license inventory.

## Building

The project is intended to be buildable directly on Android using AndroidIDE.

The exact local SDK/NDK/CMake versions are documented in [`documents/TECHNICAL.md`](documents/TECHNICAL.md).

A typical development workflow is:

```text
Open project in AndroidIDE
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

## Development Workflow

Git is used to keep the project recoverable while the architecture is being refactored.

The stable baseline is:

```text
master
└── v0.1.0
    └── WAD Editor MVP
```

Development work is performed in feature/refactoring branches rather than directly on the stable baseline.

Example:

```bash
git switch -c refactor/architecture
```

Small, focused commits are preferred so that individual changes can be reviewed, tested and reverted independently.

## Documentation

Project documentation is kept in [`documents/`](documents/):

- [`DESIGN.md`](documents/DESIGN.md) — target architecture and design principles
- [`ROADMAP.md`](documents/ROADMAP.md) — development roadmap and milestones
- [`TECHNICAL.md`](documents/TECHNICAL.md) — technical stack and implementation details
- [`REFACTORING.md`](documents/REFACTORING.md) — planned refactoring strategy
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) — third-party software and license inventory

## Licensing

SLADE is distributed under the **GNU General Public License, version 2 (GPL-2.0)**.

This repository also contains third-party components under their own licenses, including MIT-licensed libraries, public-domain code, and components with their own permissive license terms.

The original license and copyright notices for third-party components are preserved with the corresponding source code.

See:

- [`app/src/main/cpp/third_party/SLADE/LICENSE`](app/src/main/cpp/third_party/SLADE/LICENSE)
- [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)

> **Important:** The overall licensing boundary between project-owned Android code and integrated GPL-covered SLADE code is intentionally not presented here as a single new repository-wide license. The licensing structure should be reviewed before publishing a final distribution under a new overall licensing scheme.

## Project Philosophy

The project follows a few practical principles:

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
