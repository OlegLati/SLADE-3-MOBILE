// -----------------------------------------------------------------------------
// compat/slade_shims.cpp
//
// android_compat.h (force-included, header-only) can only shim symbols that
// don't depend on types SLADE itself declares -- anything referencing
// slade::log::MessageType, slade::CVar, slade::ConsoleCommand, or
// slade::EntryType needs those real declarations visible first. So, same
// convention as every other SLADE .cpp file: include Main.h first, then
// provide real (or deliberately minimal) definitions for the handful of
// functions the linker reported as missing.
//
// Add this file to slade_core's source list in CMakeLists.txt.
// -----------------------------------------------------------------------------

#include "Main.h"

#include "Archive/ArchiveManager.h"
#include "Archive/Formats/WadArchive.h"
#include "Archive/Formats/WadJArchive.h"
#include "General/Console.h"
#include "General/Misc.h"
#include "General/UI.h"
#include "UI/WxUtils.h"
#include "Utility/Colour.h"

// Best-effort guesses at where these live, matching the Application/
// convention we already confirmed for Main.h. If the path is wrong the
// compiler will say "file not found" and we'll correct it.
#include "App.h"
#include "MainEditor/MainEditor.h"
#include "General/UndoRedo.h"
#include "Utility/Property.h"

#include <cstdio>
#include <cstring>
#include <variant>

using namespace slade;

// -----------------------------------------------------------------------
// ui:: splash/progress functions -- General/UI.h declares these for real
// (we originally shimmed our own versions in android_compat.h, but that
// collided with the genuine declarations once more files pulled this
// header in -- wrong parameter/return types, duplicate defaults, etc).
// No-op bodies: there's no splash window in the headless/Android build.
// -----------------------------------------------------------------------
namespace slade::ui
{
void showSplash(std::string_view, bool, wxWindow*) {}
void hideSplash() {}
void setSplashProgress(float) {}
void setSplashProgressMessage(std::string_view) {}
} // namespace slade::ui

// -----------------------------------------------------------------------
// wxutil::strFromView -- UI/WxUtils.h declares this returning wxString
// (not std::string, as our earlier guess in android_compat.h assumed).
// -----------------------------------------------------------------------
namespace slade::wxutil
{
wxString strFromView(std::string_view str)
{
    return wxString::FromUTF8(str);
}
} // namespace slade::wxutil

// -----------------------------------------------------------------------
// misc:: helpers -- General/Misc.h declares these for real too.
// -----------------------------------------------------------------------
namespace slade::misc
{
// Standard zlib-polynomial CRC32.
uint32_t crc(const unsigned char* data, unsigned int size)
{
    static uint32_t table[256];
    static bool     init = false;
    if (!init)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned int i = 0; i < size; ++i)
        c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// Classic WAD 8-char lump name convention (truncate, uppercase). NOTE: no
// default argument here on purpose -- it's already specified once on the
// declaration in General/Misc.h; repeating it here is itself an error.
std::string fileNameToLumpName(std::string_view file, bool percent_encoding_only)
{
    (void)percent_encoding_only;
    std::string name(file);
    auto        dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);
    if (name.size() > 8)
        name = name.substr(0, 8);
    for (auto& ch : name)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return name;
}
} // namespace slade::misc

// -----------------------------------------------------------------------
// log::message -- Log.h's log::info/warning/error/debug template helpers
// all funnel down to one of these two overloads. Route to stdout/stderr
// for now (visible via `adb logcat` under your app's stdout redirection,
// or just useful once we wire a proper Android log later).
// -----------------------------------------------------------------------
namespace slade::log
{
void message(MessageType type, std::string_view text, fmt::basic_format_args<fmt::context> args)
{
    std::string formatted = fmt::vformat(text, args);
    std::fprintf(stderr, "[SLADE] %s\n", formatted.c_str());
}

void message(MessageType type, int level, std::string_view text)
{
    (void)level;
    std::fprintf(stderr, "[SLADE] %.*s\n", static_cast<int>(text.size()), text.data());
}

// The actual overload log::info<Args...>()/warning<Args...>()/etc. (Log.h)
// funnel into: takes BOTH a verbosity level and format args together.
void message(MessageType type, int level, std::string_view text, fmt::basic_format_args<fmt::context> args)
{
    (void)level;
    std::string formatted = fmt::vformat(text, args);
    std::fprintf(stderr, "[SLADE] %s\n", formatted.c_str());
}
} // namespace slade::log

// -----------------------------------------------------------------------
// ConsoleCommand -- real implementation lives in General/Console.cpp
// (a whole command-registry subsystem we haven't ported). For now, the
// constructor is a deliberate no-op: it registers nothing, so any console
// commands declared at file scope elsewhere just silently don't exist.
// Fine for our current goal (opening/listing archives); revisit if/when
// an in-app console is added.
// -----------------------------------------------------------------------
namespace slade
{
ConsoleCommand::ConsoleCommand(std::string_view, void (*)(const std::vector<std::string>&), int, bool) {}
} // namespace slade

// -----------------------------------------------------------------------
// CIntCVar/CBoolCVar/etc -- same story as ConsoleCommand: the real CVar
// registry (General/CVar.cpp) isn't part of this build, so these
// constructors don't register the cvar anywhere or make it persist to a
// config file. HOWEVER -- unlike ConsoleCommand, CVar.h (real, unmodified
// header) gives each of these a public `value` field with no in-class
// initializer, read via operator int()/operator*()/etc. everywhere the
// cvar is used (e.g. ArchiveEntry.cpp: `max_entry_size_mb * MB_TO_BYTES`).
// An empty constructor body leaves `value` at its static-storage zero-init
// -- 0 -- forever, regardless of the real default passed to CVAR(...) (512
// for max_entry_size_mb). That silently makes maxEntrySizeBytes() return
// 0, which makes ArchiveEntry::importMem() reject every entry with actual
// content ("Over maximum entry size") -- the real cause behind
// androidLoadAllEntryData() failing to load any entry's bytes. Assigning
// the constructor's defval parameter to `value` is enough to fix that,
// even without wiring up the rest of the real CVar registry/persistence.
// -----------------------------------------------------------------------
namespace slade
{
CIntCVar::CIntCVar(std::string_view, int defval, unsigned short) : value{ defval } {}
CBoolCVar::CBoolCVar(std::string_view, bool defval, unsigned short) : value{ defval } {}
CFloatCVar::CFloatCVar(std::string_view, double defval, unsigned short) : value{ defval } {}
CStringCVar::CStringCVar(std::string_view, std::string_view defval, unsigned short) : value{ defval } {}
} // namespace slade

// -----------------------------------------------------------------------
// EntryType -- the real entry-type detection system (Archive/EntryType/*)
// matches lump contents against ~50 registered EntryDataFormat signatures
// and isn't ported yet. These stubs make every entry resolve to "unknown"
// rather than actually detecting textures/sounds/scripts/etc. Good enough
// to prove Archive/WadArchive open+list works; revisit when we want real
// type-aware handling (e.g. rendering a texture preview).
// -----------------------------------------------------------------------
namespace slade
{
EntryType* EntryType::unknownType()
{
    static EntryType instance;
    return &instance;
}

EntryType* EntryType::fromId(std::string_view)
{
    return EntryType::unknownType();
}

bool EntryType::detectEntryType(ArchiveEntry& entry)
{
    entry.setType(EntryType::unknownType());
    return true;
}
} // namespace slade

// -----------------------------------------------------------------------
// EntryDataFormat::anyFormat -- companion singleton to EntryType's
// "unknown" type, representing "matches any data". Same treatment: a
// minimal stand-in rather than the full ~50-format signature registry.
// -----------------------------------------------------------------------
namespace slade
{
EntryDataFormat* EntryDataFormat::anyFormat()
{
    static EntryDataFormat instance("any");
    return &instance;
}

// This is EntryDataFormat's actual key function: declared virtual in the
// header but with no inline body there (unlike the destructor, which is
// already `= default` in-class and so can't serve as the anchor). Defining
// it out-of-line here is what makes the vtable get emitted.
int EntryDataFormat::isThisFormat(MemChunk&)
{
    return EntryDataFormat::MATCH_FALSE;
}
} // namespace slade

// -----------------------------------------------------------------------
// WadJArchive::jaguarDecode -- Atari Jaguar Doom WAD decompression.
// Niche format variant; stub returns false (i.e. "not a Jaguar WAD / not
// handled") rather than actually decoding, so normal PC WADs are
// unaffected.
// -----------------------------------------------------------------------
namespace slade
{
bool WadJArchive::jaguarDecode(MemChunk&)
{
    return false;
}
} // namespace slade

// -----------------------------------------------------------------------
// app::archiveManager -- singleton accessor. The real one lives in
// Application/App.cpp (not ported -- pulls in the whole app-lifecycle
// subsystem). ArchiveManager's own implementation IS fully compiled as
// part of slade_core though, so a simple function-local static gives a
// real, working singleton for anything that just needs "the" archive
// manager (e.g. StringUtils.cpp's #include-directive resolution).
// -----------------------------------------------------------------------
namespace slade::app
{
ArchiveManager& archiveManager()
{
    static ArchiveManager instance;
    return instance;
}
} // namespace slade::app

// -----------------------------------------------------------------------
// ColRGBA::WHITE -- used as a default member initializer inside EntryType
// (and probably elsewhere). The real value lives in Utility/Colour.cpp,
// which we deliberately don't build (it pulls in a whole CIE/Lab
// colorimetry table we don't need yet). Just the plain RGB value here.
// -----------------------------------------------------------------------
namespace slade
{
const ColRGBA ColRGBA::WHITE(255, 255, 255, 255);
} // namespace slade

// -----------------------------------------------------------------------
// app:: / maineditor:: -- misc application-level accessors referenced
// from otherwise-portable utility code (FileUtils.cpp's executable-finder,
// StringUtils.cpp's #include-path resolution, Tokenizer.cpp's console
// test command). None of the real implementations (Application/App.cpp,
// MainEditor/MainEditor.cpp) are ported -- they pull in the whole
// application lifecycle / UI layer. Minimal stand-ins below.
// -----------------------------------------------------------------------
namespace slade::app
{
Platform platform()
{
    return Platform::Linux;
}

long runTimer()
{
    return 0;
}

std::string path(std::string_view filename, Dir dir)
{
    (void)dir;
    return std::string(filename);
}
} // namespace slade::app

namespace slade::maineditor
{
ArchiveEntry* currentEntry()
{
    return nullptr;
}
} // namespace slade::maineditor

// -----------------------------------------------------------------------
// undoredo:: -- undo/redo history tracking (General/UndoRedo.h/.cpp, not
// ported). Archive::createDir/removeDir/renameDir guard their recordUndoStep
// calls behind currentlyRecording(), so returning false here means
// UndoManager::recordUndoStep never actually runs -- but its symbol still
// has to exist for the linker, since the call is written unconditionally
// in the source even though it's dead at runtime with recording off.
// -----------------------------------------------------------------------
namespace slade::undoredo
{
bool currentlyRecording()
{
    return false;
}

UndoManager* currentManager()
{
    return nullptr;
}
} // namespace slade::undoredo

namespace slade
{
bool UndoManager::recordUndoStep(std::unique_ptr<UndoStep>) const
{
    return false;
}
} // namespace slade

// -----------------------------------------------------------------------
// log::message -- one more overload: plain (type, text), no level, no
// format args. Used by the non-template log::error(string_view) etc.
// -----------------------------------------------------------------------
namespace slade::log
{
void message(MessageType, std::string_view text)
{
    std::fprintf(stderr, "[SLADE] %.*s\n", static_cast<int>(text.size()), text.data());
}
} // namespace slade::log

// -----------------------------------------------------------------------
// More EntryType singletons + the instance method isThisType, same
// treatment as unknownType(): minimal stand-ins, not real detection.
// -----------------------------------------------------------------------
namespace slade
{
EntryType* EntryType::folderType()
{
    static EntryType instance;
    return &instance;
}

EntryType* EntryType::mapMarkerType()
{
    static EntryType instance;
    return &instance;
}

int EntryType::isThisType(ArchiveEntry&) const
{
    return 0;
}
} // namespace slade

// -----------------------------------------------------------------------
// property::as* -- real (not stubbed) conversions from the generic
// variant<bool,int,unsigned int,double,string> property type used by
// ParseTreeNode. Genuinely simple to implement correctly rather than
// fake, so we just do that.
// -----------------------------------------------------------------------
namespace slade::property
{
std::string asString(const std::variant<bool, int, unsigned int, double, std::string>& v, int precision)
{
    return std::visit(
        [precision](auto&& val) -> std::string
        {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>)
                return val;
            else if constexpr (std::is_same_v<T, bool>)
                return val ? "true" : "false";
            else if constexpr (std::is_floating_point_v<T>)
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.*f", precision, static_cast<double>(val));
                return std::string(buf);
            }
            else
                return std::to_string(val);
        },
        v);
}

int asInt(const std::variant<bool, int, unsigned int, double, std::string>& v)
{
    return std::visit(
        [](auto&& val) -> int
        {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>)
            {
                try
                {
                    return std::stoi(val);
                }
                catch (...)
                {
                    return 0;
                }
            }
            else
                return static_cast<int>(val);
        },
        v);
}

bool asBool(const std::variant<bool, int, unsigned int, double, std::string>& v)
{
    return std::visit(
        [](auto&& val) -> bool
        {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>)
                return val == "true" || val == "1";
            else
                return static_cast<bool>(val);
        },
        v);
}

double asFloat(const std::variant<bool, int, unsigned int, double, std::string>& v)
{
    return std::visit(
        [](auto&& val) -> double
        {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::string>)
            {
                try
                {
                    return std::stod(val);
                }
                catch (...)
                {
                    return 0.0;
                }
            }
            else
                return static_cast<double>(val);
        },
        v);
}
} // namespace slade::property

// -----------------------------------------------------------------------
// misc:: two more real (not stubbed) helpers.
// -----------------------------------------------------------------------
namespace slade::misc
{
// Reverse of fileNameToLumpName: for our purposes, a straight pass-through
// is a reasonable default (no 8-char WAD naming constraint going this
// direction).
std::string lumpNameToFileName(std::string_view lump)
{
    return std::string(lump);
}

// Human-readable byte size, e.g. "1.2kb".
std::string sizeAsString(unsigned int size)
{
    constexpr const char* units[] = { "b", "kb", "mb", "gb" };
    double                s       = static_cast<double>(size);
    int                   unit    = 0;
    while (s >= 1024.0 && unit < 3)
    {
        s /= 1024.0;
        ++unit;
    }
    char buf[64];
    if (unit == 0)
        std::snprintf(buf, sizeof(buf), "%u%s", size, units[unit]);
    else
        std::snprintf(buf, sizeof(buf), "%.1f%s", s, units[unit]);
    return std::string(buf);
}
} // namespace slade::misc

// -----------------------------------------------------------------------
// androidDetectEntryType -- a real (not stubbed), hand-written entry-type
// classifier, deliberately NOT implemented as an EntryType subsystem port.
//
// Why not use the real EntryType matching (isThisType/detectEntryType)?
// The genuine engine (EntryType::loadEntryTypes) reads its ~50 format
// definitions from config/entry_types.cfg *inside slade.pk3*, fetched via
// app::archiveManager().programResourceArchive() -- a real Zip-based
// resource archive. We deliberately haven't ported ZipArchive.cpp yet (see
// CMakeLists.txt), so that whole path is unavailable. EntryType's fields
// (name_, match_size_, etc.) are also private with no setters, so we can't
// construct our own named types through the public API either.
//
// CHANGED (memory safety): this used to take an ArchiveEntry&, and a
// separate androidLoadAllEntryData() helper eagerly copied every single
// entry's bytes into its own persistent ArchiveEntry::data_ before this
// ran. That worked, but tripled memory use for the whole WAD (once in the
// caller's byte array, once in the shared MemChunk we parse from, once
// more PER ENTRY duplicated into each ArchiveEntry) -- fine for a small
// vanilla IWAD, but almost certainly what was crashing large WADs with
// real audio replacing MUS lumps (Sigil's Buckethead/Thorr soundtrack
// editions) on a memory-constrained device. This version takes the name,
// size, and a raw pointer directly, read straight out of the single
// shared WAD buffer at the entry's recorded offset -- no ArchiveEntry
// data loading, no copying, ever, for detection purposes. See the call
// site in native-lib.cpp.
// -----------------------------------------------------------------------
namespace slade
{
namespace
{
bool matchesUpperName(std::string_view upperName, std::initializer_list<const char*> candidates)
{
    for (auto* candidate : candidates)
        if (upperName == candidate)
            return true;
    return false;
}

bool startsWithUpper(std::string_view upperName, std::string_view prefix)
{
    return upperName.size() >= prefix.size() && upperName.substr(0, prefix.size()) == prefix;
}
} // namespace

std::string androidDetectEntryType(std::string_view upperName, uint32_t size, const uint8_t* data)
{
    // Zero-length entries are always markers (same rule as the real
    // EntryType::detectEntryType).
    if (size == 0)
        return "Marker";

    // Classic Doom/Boom/ZDoom map data lumps -- identified by name, since
    // they have no magic header of their own (just tightly-packed binary
    // structs). Checking this first avoids the size-based heuristics below
    // misfiring on a map lump that happens to match a common byte size.
    if (matchesUpperName(
            upperName,
            { "THINGS",
              "LINEDEFS",
              "SIDEDEFS",
              "VERTEXES",
              "SEGS",
              "SSECTORS",
              "NODES",
              "SECTORS",
              "REJECT",
              "BLOCKMAP",
              "BEHAVIOR",
              "SCRIPTS",
              "TEXTMAP",
              "ZNODES",
              "ENDMAP",
              "DIALOGUE" }))
        return "Map Data";

    // GL nodes (precomputed BSP data for OpenGL-based ports, living in
    // their own GL_MAPxx namespace alongside the vanilla map data above).
    // Several sub-format versions exist -- plain vs "gNdX"-tagged
    // extended variants -- so rather than chase every magic-byte version,
    // identify these by name the same way as the vanilla lumps above.
    // (GLSSECT without the underscore shows up in some older glBSP output
    // too, hence both spellings.)
    if (matchesUpperName(upperName, { "GL_VERT", "GL_SEGS", "GL_SSECT", "GLSSECT", "GL_NODES", "GL_PVS" }))
        return "GL Nodes";

    // DOS text-mode screens: ENDOOM and its variants across different
    // games/ports, plus Heretic/Hexen's LOADING screen -- all the same
    // 80x25 characters, 2 bytes each (char + color attribute) = exactly
    // 4000 bytes, no magic header, which is why these fell through to the
    // printable-ASCII "Text" heuristic further down and failed it (the
    // attribute bytes are rarely in printable range).
    if (size == 4000
        && matchesUpperName(
            upperName, { "ENDOOM", "ENDTEXT", "ENDBOOT", "ENDSTRF", "ENDMS", "ENDING", "LOADING" }))
        return "Text Screen";

    // Raw fullscreen images (Heretic/Hexen title/help/credit/finale
    // screens, chess-piece win screens, etc): a plain 320x200 8-bit-
    // indexed VGA dump with NO header at all -- unlike the "Doom Graphic"
    // patch format further down, which always has a width/height/offset
    // header. The exact size (320*200 = 64000) is the only signal there
    // is, but it's a very specific number to hit by coincidence.
    if (size == 64000)
        return "Fullscreen Image";

    // Demo lumps (DEMO1, DEMO2, ... DEMOn): recorded player input for
    // playback, not a picture -- pure binary bytes with no signature of
    // their own. Without this, the first few bytes of a demo can
    // coincidentally pass the Doom Graphic heuristic below (small
    // "width"/"height" values plus a first column offset that happens to
    // land in-bounds) purely by chance -- which is exactly what happened
    // to DEMO1-DEMO3 before this check was added, while DEMO4 just didn't
    // get unlucky and fell through to "Unknown" instead.
    if (startsWithUpper(upperName, "DEMO"))
        return "Demo";

    // TEXTURE1/TEXTURE2 (texture definition tables -- lists of composite
    // textures built from patches) and PNAMES (the patch name list they
    // reference): tightly-packed binary structs, same story as the map
    // data lumps above -- no signature of their own, and their first few
    // bytes can coincidentally pass the Doom Graphic heuristic below
    // purely by chance (that's exactly what was happening to TEXTURE1/
    // TEXTURE2 before this check was added).
    if (matchesUpperName(upperName, { "TEXTURE1", "TEXTURE2" }))
        return "Texture Definitions";
    if (upperName == "PNAMES")
        return "Patch Names";

    // More DeuTex-era table lumps: ANIMATED (flat/wall texture animation
    // sequences) and SWITCHES (on/off switch texture pairs) -- again,
    // fixed-layout binary records with no signature, identified by name.
    if (upperName == "ANIMATED")
        return "Animated Textures";
    if (upperName == "SWITCHES")
        return "Switch Textures";

    // Sound falloff/distance-curve table (Heretic/Hexen).
    if (upperName == "SNDCURVE")
        return "Sound Curve Table";

    // Hexen-specific tables: FOGMAP (a second colormap used for the fog
    // effect -- same 34x256-byte layout as COLORMAP below), TINTTAB (a
    // 256x256 translucency blend lookup table), and TRANTBL0-TRANTBLB (12
    // player-class color translation tables, one per hex digit 0-9,A,B).
    if (upperName == "FOGMAP" && size % 256 == 0)
        return "Fog Table";
    if (upperName == "TINTTAB")
        return "Translucency Table";
    if (startsWithUpper(upperName, "TRANTBL"))
        return "Translation Table";

    // Sky flats: a flat literally named F_SKY1 (or F_SKY2/F_SKY3 in some
    // ports) triggers special sky rendering instead of being drawn as a
    // normal flat, so its actual lump data is just a tiny placeholder
    // rather than a real 4096-byte flat -- identified by name since its
    // size is deliberately not meaningful.
    if (matchesUpperName(upperName, { "F_SKY1", "F_SKY2", "F_SKY3" }))
        return "Sky Flat";

    if (!data)
        return "Unknown";

    // PNG: fixed 8-byte magic.
    static const uint8_t pngMagic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    if (size >= 8 && std::memcmp(data, pngMagic, 8) == 0)
        return "PNG Image";

    // MIDI.
    if (size >= 4 && std::memcmp(data, "MThd", 4) == 0)
        return "MIDI";

    // MUS: id Software's own music format (used by Doom/Heretic/Hexen/
    // Strife instead of raw MIDI -- these get converted to MIDI at
    // playback time, but the lump itself is MUS on disk). Magic is
    // "MUS" followed by a 0x1A byte.
    if (size >= 4 && std::memcmp(data, "MUS\x1A", 4) == 0)
        return "MUS Music";

    // GENMIDI: General MIDI instrument-definition table used for OPL
    // synth playback of MUS/MIDI music. Magic is the 8-byte "#OPL_II#".
    if (size >= 8 && std::memcmp(data, "#OPL_II#", 8) == 0)
        return "GENMIDI Instruments";

    // RIFF/WAV.
    if (size >= 12 && std::memcmp(data, "RIFF", 4) == 0 && std::memcmp(data + 8, "WAVE", 4) == 0)
        return "WAV Sound";

    // Compressed audio formats -- mods commonly replace MUS/MIDI music
    // lumps with real recorded tracks in these formats (e.g. Sigil's
    // Buckethead/Thorr soundtrack editions).
    if (size >= 4 && std::memcmp(data, "OggS", 4) == 0)
        return "OGG Audio";
    if (size >= 4 && std::memcmp(data, "fLaC", 4) == 0)
        return "FLAC Audio";
    if (size >= 3 && std::memcmp(data, "ID3", 3) == 0)
        return "MP3 Audio";
    // ID3-less MP3: raw frame sync is 11 set bits (0xFFE) at the very
    // start, i.e. byte 0 all-ones and the top 3 bits of byte 1 also
    // all-ones. Less airtight than a real magic string, but a very
    // reliable convention in practice, and nothing else we check for
    // starts with 0xFF.
    if (size >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0)
        return "MP3 Audio";

    // DMX sound effect: byte 0-1 = format (3, 0), bytes 2-3 = sample rate,
    // bytes 4-7 = sample count. Loosely sanity-check the declared sample
    // count against the actual entry size to avoid false positives.
    if (size >= 8 && data[0] == 0x03 && data[1] == 0x00)
    {
        const uint32_t numSamples = static_cast<uint32_t>(data[4]) | (static_cast<uint32_t>(data[5]) << 8)
                                     | (static_cast<uint32_t>(data[6]) << 16)
                                     | (static_cast<uint32_t>(data[7]) << 24);
        if (numSamples > 0 && numSamples <= size)
            return "DMX Sound";
    }

    // PC speaker sound effect: bytes 0-1 = format (always 0, 0 -- as
    // opposed to DMX's 3, 0 above), bytes 2-3 = sample count (u16, little-
    // endian), followed by that many single-byte samples (frequency-table
    // indices, not raw PCM). Total size should be exactly 4 + numSamples.
    if (size >= 4 && data[0] == 0x00 && data[1] == 0x00)
    {
        const uint32_t numSamples = static_cast<uint32_t>(data[2]) | (static_cast<uint32_t>(data[3]) << 8);
        if (numSamples > 0 && 4u + numSamples == size)
            return "PC Speaker Sound";
    }

    // Compiled ACS script: "ACS\0" (original format) or "ACSE"/"ACSe"
    // (extended formats).
    if (size >= 4
        && (std::memcmp(data, "ACS\0", 4) == 0 || std::memcmp(data, "ACSE", 4) == 0
            || std::memcmp(data, "ACSe", 4) == 0))
        return "Compiled ACS";

    // PLAYPAL and its per-episode Heretic variants (E2PAL/E3PAL/E4PAL/
    // E5PAL -- single-palette boss-death tint shifts for episodes 2-5):
    // name + exact multiple-of-768 (256 RGB triples per palette) size
    // check, rather than a bare size match, to avoid misclassifying an
    // arbitrary 768-byte binary blob in a non-IWAD context.
    if (matchesUpperName(upperName, { "PLAYPAL", "E2PAL", "E3PAL", "E4PAL", "E5PAL" }) && size % 768 == 0)
        return "Palette";

    // COLORMAP: name + multiple-of-256 size check (each colormap is a
    // 256-byte palette-index remap table).
    if (upperName == "COLORMAP" && size % 256 == 0)
        return "Colormap";

    // Flat (floor/ceiling texture): classic raw 64x64 = 4096 bytes, plus
    // the 4160-byte variant seen in Heretic/Hexen's animated liquid flats
    // (FLTFLWW*/FLTLAVA* -- a handful of extra bytes beyond the raw pixel
    // data, engine-specific, but consistent enough across both games to
    // treat as a second valid Flat size rather than a coincidence).
    if (size == 4096 || size == 4160)
        return "Flat";

    // Doom picture (patch) format: 4x int16 header (width, height,
    // left/top offset) followed by `width` int32 column offsets. Sanity-
    // check the first column offset actually points back inside the entry
    // -- cheap but effective at rejecting non-picture binary data.
    if (size >= 8)
    {
        const uint16_t width = static_cast<uint16_t>(data[0] | (data[1] << 8));
        if (width > 0 && width < 4096 && size >= 8u + 4u * width)
        {
            const uint32_t firstColOfs = static_cast<uint32_t>(data[8]) | (static_cast<uint32_t>(data[9]) << 8)
                                          | (static_cast<uint32_t>(data[10]) << 16)
                                          | (static_cast<uint32_t>(data[11]) << 24);
            if (firstColOfs >= 8 && firstColOfs < size)
                return "Doom Graphic";
        }
    }

    // Text/script catch-all: printable ASCII (plus common whitespace) for
    // at least the first 512 bytes (or the whole entry, if shorter), and
    // no embedded null byte -- the same "no null in the body" signal the
    // real EntryType.cpp uses for its ACS-source special case.
    {
        const unsigned checkLen = size < 512 ? size : 512;
        bool           isText   = true;
        for (unsigned i = 0; i < checkLen; ++i)
        {
            const uint8_t c           = data[i];
            const bool    isPrintable = c == '\t' || c == '\n' || c == '\r' || (c >= 0x20 && c < 0x7F);
            if (!isPrintable)
            {
                isText = false;
                break;
            }
        }
        if (isText)
            return "Text";
    }

    return "Unknown";
}
} // namespace slade
