# Native regression tests

The first regression slice lives in:

    app/src/main/cpp/tests/native_wad_tests.cpp

It deliberately tests the SLADE WAD layer directly rather than going through JNI. The goal of R0 is to establish a small, deterministic baseline before extracting more architecture.

## Current coverage

- open a valid WAD;
- enumerate and read an entry;
- reject an invalid WAD;
- reject a truncated WAD;
- rename an entry, serialize, and reopen;
- add an entry, serialize, and reopen.

The tests use an in-memory WAD fixture, so they do not depend on external game files.

## Build

The test target is enabled by the CMake option:

    BUILD_NATIVE_TESTS=ON

For an Android ABI build, configure the existing Android CMake project with that option and build the native_wad_tests target.

The test binary is intentionally separate from the application JNI library. It exercises the same slade_core static library used by the application.

## Scope

This is only the first R0 slice. It does not yet cover:

- ArchiveSession;
- JNI marshalling;
- validateForSave() / commitSave();
- discardChanges();
- SAF file-descriptor handling;
- Android UI.

Those require either a small test seam around the native application layer or Android instrumentation coverage. They should be added without changing production behavior.

## Rule

Every structural extraction should keep this regression slice passing. Expand the slice before changing ownership or save semantics.
