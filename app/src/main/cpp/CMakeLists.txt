# For more information about using CMake with Android Studio, read the
# documentation: https://d.android.com/studio/projects/add-native-code.html.
# For more examples on how to use CMake, see https://github.com/android/ndk-samples.

cmake_minimum_required(VERSION 3.22.1)

project("myapplication")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# slade_core -- headless SLADE archive/parsing layer (no wxWidgets)
# =============================================================================

# Path to an UNMODIFIED checkout of github.com/sirjuddington/SLADE, cloned
# into app/src/main/cpp/third_party/SLADE (see README for the git command).
set(SLADE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SLADE/src)
set(SLADE_THIRDPARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party/SLADE/thirdparty)

set(COMPAT_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/compat/android_compat.h)

# Start SMALL -- add files one at a time as each compiles cleanly. Don't glob
# all of src/; most of it (UI/, MapEditor/*Panel, OpenGL/) depends directly
# on wxWidgets/OpenGL and isn't in scope yet.
add_library(slade_core STATIC
        ${SLADE_SRC}/Archive/Archive.cpp
        ${SLADE_SRC}/Archive/ArchiveDir.cpp
        ${SLADE_SRC}/Archive/ArchiveEntry.cpp
        ${SLADE_SRC}/Archive/ArchiveManager.cpp
        ${SLADE_SRC}/Archive/Formats/WadArchive.cpp
        # ZipArchive.cpp is deliberately NOT included yet: it needs
        # wxZipInputStream/wxZipOutputStream/wxFFileInputStream, i.e. a real
        # deflate/zip container implementation, not just declaration shims.
        # Revisit once WadArchive works end to end -- either implement a
        # thin zip layer on top of NDK's bundled zlib/minizip, or shim it
        # properly at that point.
        # ${SLADE_SRC}/Archive/Formats/ZipArchive.cpp
        ${SLADE_SRC}/Utility/StringUtils.cpp
        ${SLADE_SRC}/Utility/FileUtils.cpp
        ${SLADE_SRC}/Utility/Tokenizer.cpp
        ${SLADE_SRC}/Utility/MemChunk.cpp
        ${SLADE_SRC}/Utility/Parser.cpp
        ${SLADE_SRC}/Utility/Tree.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/compat/slade_shims.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ArchiveSession.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ArchiveSerializer.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/SaveCoordinator.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ArchiveOperations.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ArchiveEntryReader.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ArchivePreview.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ArchiveAudioPreview.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/ArchiveEntryList.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/FileDescriptorIO.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/NativeArchiveApi.cpp
)

target_include_directories(slade_core PUBLIC
        # compat/ MUST come first: it contains stand-in common.h / common2.h
        # files that shadow SLADE's real ones (which pull in wxWidgets,
        # FluidSynth, SFML). Quote-includes resolve to the first match on
        # this list, so order here is load-bearing, not cosmetic.
        ${CMAKE_CURRENT_SOURCE_DIR}/compat
        ${SLADE_SRC}
        ${SLADE_SRC}/..
        # The real Main.h lives in src/Application, not src/ directly.
        ${SLADE_SRC}/Application
        # Adjust to wherever fmt actually lives in the SLADE checkout
        # (SLADE bundles it under thirdparty/fmt/include).
        ${SLADE_THIRDPARTY}/fmt/include
)

target_compile_options(slade_core PUBLIC
        -include ${COMPAT_HEADER}
        -DSLADE_HEADLESS=1
        -Wno-unused-parameter
)

find_package(ZLIB REQUIRED)
target_link_libraries(slade_core PUBLIC z log)

# Optional native regression tests. Keep disabled for normal app builds; enable
# explicitly when validating the headless SLADE core after structural changes.
option(BUILD_NATIVE_TESTS "Build native WAD regression tests" OFF)

if(BUILD_NATIVE_TESTS)
    add_executable(native_wad_tests
            tests/native_wad_tests.cpp)

    target_link_libraries(native_wad_tests
            slade_core
            log)

    add_executable(native_audio_preview_tests
            tests/native_audio_preview_tests.cpp)

    target_link_libraries(native_audio_preview_tests
            slade_core
            log)

    add_executable(native_entry_list_tests
            tests/native_entry_list_tests.cpp)

    target_link_libraries(native_entry_list_tests
            slade_core
            log)
endif()

# NEXT FILES TO ADD, roughly in order of usefulness/difficulty:
#   Archive/Formats/DirArchive.cpp                   (no extra deps)
#   Archive/Formats/GrpArchive.cpp, PakArchive.cpp    (simple formats)
#   Archive/Formats/BZip2Archive.cpp, GZipArchive.cpp (need libbz2/zlib)
# Each new file may surface a new missing symbol -- add it to
# compat/android_compat.h when that happens.

# =============================================================================
# myapplication -- the JNI bridge library loaded from Kotlin via
# System.loadLibrary("myapplication"). This is what Android actually loads;
# it links slade_core in, so native-lib.cpp can call into SLADE's Archive
# classes directly.
# =============================================================================

add_library(myapplication SHARED
        native-lib.cpp
        JniUtils.cpp
        ArchiveEntryListJni.cpp)

target_include_directories(myapplication PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(myapplication
        slade_core
        android
        log)
