#pragma once
// -----------------------------------------------------------------------------
// compat/common.h
//
// Stand-in for SLADE's real src/common.h, used only by the headless Android
// build. The real file's job is to be a single "pull in everything" header
// included by Main.h; the wx/FluidSynth/SFML portions are exactly what we
// can't (yet) build for Android, so this stub keeps only what the
// Archive/Utility layer actually needs: fmt, sigslot signals, and the plain
// C++ standard library headers.
//
// This works because compat/ is placed FIRST on the include path in
// CMakeLists.txt, so the quote-include "common.h" inside the real Main.h
// resolves to this file instead of the real src/common.h.
//
// If you add more .cpp files to slade_core later and hit a missing symbol
// that traces back to something only in the REAL common.h (a wx type,
// FluidSynth, SFML), that's a sign the new file has a genuine UI/audio
// dependency -- either exclude it from CMakeLists.txt for now, or extend
// this stub / android_compat.h with a shim for that specific symbol.
// -----------------------------------------------------------------------------

// fmt
// FMT_HEADER_ONLY: without this, fmt declares functions like fmt::vformat
// as extern, expecting you to compile+link thirdparty/fmt/src/format.cc
// separately. We aren't building that translation unit, so this macro
// switches fmt to its fully-inline mode instead.
#define FMT_HEADER_ONLY 1
#include <fmt/format.h>

// Sigslot (used by Archive's signal/slot system, e.g. archive->signals())
#include "thirdparty/sigslot/signal.hpp"

// C++
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

// NOTE: intentionally NOT included here (the real common.h has these):
//   - all wx/*.h headers    (no wxWidgets in this build target)
//   - <fluidsynth.h>        (MIDI synth -- out of scope for now)
//   - <SFML/System.hpp>     (out of scope for now)
