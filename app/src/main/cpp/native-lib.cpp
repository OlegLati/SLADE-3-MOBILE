#include <jni.h>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>
#include <memory>

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// SLADE convention: every .cpp file includes Main.h FIRST. It's the header
// that (via common.h) sets up unqualified `string`/`string_view`/etc. via
// `using` declarations -- the rest of the codebase's headers rely on that
// already being in place by the time they're processed, rather than being
// self-contained. Skipping this is exactly what broke the previous attempt.
#include "Main.h"

#include "Archive/Formats/WadArchive.h"
#include "Utility/MemChunk.h"
#include "General/CVar.h"

using namespace slade;

// WadArchive::write() refuses to serialize an IWAD (e.g. DOOM.WAD) at all
// while this is true -- SLADE's own built-in safeguard against clobbering
// someone's original game IWAD in place. Declared here (defined in
// WadArchive.cpp) the same way WadJArchive.cpp does it, so saveToFd() can
// toggle it off for the one call where it doesn't apply -- see the
// comment there for why.
EXTERN_CVAR(Bool, iwad_lock)

// Defined in compat/slade_shims.cpp -- a real (not EntryType-based) byte-
// signature + name classifier. Takes the entry's uppercase name, size, and
// a raw pointer to its bytes directly, rather than an ArchiveEntry& --
// deliberately reads straight out of the shared WAD MemChunk (mc, below)
// by offset instead of going through ArchiveEntry::data()/rawData() at
// all. See the comment above its definition for the full story (in short:
// WadArchive::loadEntryData() only works from a file on disk, which we
// don't have, and an earlier per-entry-copy workaround risked tripling
// memory use on large WADs).
namespace slade
{
std::string androidDetectEntryType(std::string_view upperName, uint32_t size, const uint8_t* data);
}

// -----------------------------------------------------------------------
// ArchiveSession (Phase 6, ROADMAP.md): replaces the previous g_mc/g_wad
// raw-pointer pair with one owning object. Doesn't change behavior on its
// own -- same single-archive-at-a-time assumption as before, same "not
// thread-safe, but only ever touched from one native-dispatcher thread at
// a time" invariant (see SladeNative.kt's comment on why that's true) --
// it just gives the app a real open/close lifecycle (open()/close()
// instead of only ever implicitly replacing the previous archive when a
// new one loads) and one place for Phase 7's future edit operations to
// call markDirty() from, instead of another round of global-state
// plumbing when that lands.
//
// IMPORTANT (unchanged from the old g_mc/g_wad contract): WadArchive::
// open(MemChunk&) does NOT copy entry data into itself -- per HANDOFF
// notes, loadEntryData() only works from a file on disk (filename_ never
// set here), so entries are read by offset directly out of the session's
// MemChunk for as long as the archive is open. That means the MemChunk
// must outlive the WadArchive, and both must stay alive for the whole
// "list then click an entry" lifetime, not just for one JNI call --
// that's why they're owned together by one object with one lifetime,
// rather than as two independently-managed globals.
// -----------------------------------------------------------------------
#include "ArchiveSession.h"

// entryDataPtr() through audioInfoFor() below are internal helpers only
// ever called from this file's own JNIEXPORT functions -- anonymous
// namespace for internal linkage, same pattern as ArchiveSession's own
// namespace block just above. (This opening brace was missing -- left
// orphaned by whatever pass introduced ArchiveSession as its own separate
// namespace block above; the closing "} // namespace" for *this* block
// was already sitting right before audioInfoFor()'s callers, just with
// nothing above it to match. Compiler caught it as an "extraneous closing
// brace" rather than an unclosed one because everything from here down
// happened to still parse as valid top-level declarations on its own.)
namespace
{

// Looks up an entry by index in the currently-open session and returns a
// raw pointer to its bytes, bounds-checked. Returns nullptr on any
// failure -- no archive open, bad index, or (for an unmodified entry) an
// offset/size that doesn't fit inside the session's MemChunk. Shared by
// every "get entry content" JNI function so the bounds-check logic lives
// in exactly one place.
//
// Phase 7 (Export/Import/Replace): checks entry->isLoaded() FIRST and
// returns entry->rawData() in that case, rather than always reading from
// the session's MemChunk by offset. Entries only ever become "loaded"
// here via importMem() -- either Replace overwriting an existing entry's
// content, Add creating a brand new one (which has no offset into the
// original file at all), or saveToFd()'s own priming pass. For any of
// those, reading from the original MemChunk by offset would return
// stale/original bytes (Replace) or garbage/out-of-range data (Add) --
// only entries nobody has touched yet should fall through to the
// original offset-based read below.
const uint8_t* entryDataPtr(jint index, uint32_t* outSize, ArchiveEntry** outEntry = nullptr)
{
    if (!g_session.isOpen() || index < 0)
        return nullptr;

    auto* wad = g_session.archive();
    auto* mc  = g_session.memChunk();

    auto* entry = wad->entryAt(static_cast<unsigned>(index));
    if (!entry)
        return nullptr;

    if (outEntry)
        *outEntry = entry;

    if (entry->isLoaded())
    {
        const uint32_t size = entry->size();
        if (size == 0)
            return nullptr;
        *outSize = size;
        return entry->rawData(false); // false: never trigger loadEntryData() -- see file header comment on why that always fails here anyway
    }

    const uint32_t offset = wad->getEntryOffset(entry);
    const uint32_t size   = entry->size();
    if (size == 0 || static_cast<uint64_t>(offset) + size > mc->size())
        return nullptr;

    *outSize = size;
    return mc->data() + offset;
}

// Phase 7/8 shared helper: primes every entry's data (see the long-
// standing "IMPORTANT" file-header comment and nativeSaveToFd's own
// comment for why rawData()/loadEntryData() can't be trusted here) and
// serializes the current session via WadArchive::write(). Shared by Save
// As (nativeSaveToFd) and Safe Save's validation step
// (nativeValidateForSave) -- the two differ only in whether iwad_lock
// should apply for this particular write, hence the bool parameter
// rather than each duplicating the priming loop with its own iwad_lock
// handling bolted on.
//
// allowIwadOverwrite=true (Save As): never writes to the file the
// archive was opened from, so iwad_lock's protection doesn't apply here
// and is toggled off for the call, same as before this was factored out.
// allowIwadOverwrite=false (Safe Save / in-place): DOES eventually
// overwrite the original file the archive came from, which is exactly
// the scenario iwad_lock exists to guard against -- left untouched, so
// write() can refuse a real IWAD on its own and report why via
// global::error (checked by the caller).
bool serializeSession(MemChunk& out, bool allowIwadOverwrite)
{
    auto* wad = g_session.archive();
    auto* mc  = g_session.memChunk();

    for (unsigned i = 0; i < wad->numEntries(); ++i)
    {
        auto* entry = wad->entryAt(i);
        if (!entry || entry->isLoaded() || entry->size() == 0)
            continue; // already primed, or nothing to copy

        const uint32_t offset = wad->getEntryOffset(entry);
        const uint32_t size   = entry->size();
        if (static_cast<uint64_t>(offset) + size <= mc->size())
            entry->importMem(mc->data() + offset, size);
        // else: leave unloaded -- write() below still succeeds, just with
        // this one lump's content missing, rather than failing entirely.
    }

    if (!allowIwadOverwrite)
        return wad->write(out); // let iwad_lock refuse on its own if this is a real IWAD

    const bool previousIwadLock = iwad_lock;
    iwad_lock                   = false;
    const bool wrote            = wad->write(out);
    iwad_lock                   = previousIwadLock; // restored on both success and failure
    return wrote;
}

// Text lumps are, per androidDetectEntryType()'s own check, printable
// ASCII for at least the first 512 bytes -- but a longer lump isn't
// guaranteed ASCII-only past that prefix, and NewStringUTF() expects
// valid modified UTF-8. Rather than risk a JNI-level crash we can't debug
// without Logcat, replace anything outside safe printable ASCII with '.'
// and stop at the first embedded NUL (NewStringUTF would silently
// truncate there anyway, since it reads a C string).
std::string sanitizeAscii(const uint8_t* data, uint32_t len)
{
    std::string out;
    out.reserve(len);
    for (uint32_t i = 0; i < len; ++i)
    {
        const uint8_t c = data[i];
        if (c == 0)
            break;
        if (c == '\t' || c == '\n' || c == '\r' || (c >= 0x20 && c < 0x7F))
            out.push_back(static_cast<char>(c));
        else
            out.push_back('.');
    }
    return out;
}

// -----------------------------------------------------------------------
// Doom Graphic/Flat decoding needs a palette to turn indexed bytes into
// actual colors -- neither lump carries one itself. Scans the currently-
// open archive for PLAYPAL (exact name match preferred; falls back to the
// first entry androidDetectEntryType() calls "Palette", e.g. a Heretic
// E2PAL, if a PWAD happens not to include its own PLAYPAL). Deliberately
// reuses only the entryAt()/numEntries()/getEntryOffset() calls already
// proven to compile elsewhere in this file, rather than reaching for an
// unconfirmed find-by-name API on WadArchive/Archive -- see HANDOFF
// learning #6 ("don't guess at signatures").
//
// Only the first 768 bytes (palette 0 -- the normal, undamaged palette)
// of whatever's found are used; PLAYPAL packs 14 palettes back-to-back
// for damage/berserk/radiation flashes, but a static content-preview
// dialog has no notion of "current game state" to pick among them.
const uint8_t* findPalette(uint32_t* outSize)
{
    if (!g_session.isOpen())
        return nullptr;

    auto* wad = g_session.archive();
    auto* mc  = g_session.memChunk();

    const unsigned count            = wad->numEntries();
    const uint8_t* fallbackPtr       = nullptr;
    uint32_t       fallbackSize      = 0;

    for (unsigned i = 0; i < count; ++i)
    {
        auto* e = wad->entryAt(i);
        if (!e)
            continue;

        const uint32_t offset = wad->getEntryOffset(e);
        const uint32_t size   = e->size();
        if (size < 768 || static_cast<uint64_t>(offset) + size > mc->size())
            continue;

        const uint8_t* ptr = mc->data() + offset;
        if (androidDetectEntryType(e->upperName(), size, ptr) != "Palette")
            continue;

        if (e->upperName() == "PLAYPAL")
        {
            *outSize = size;
            return ptr;
        }
        if (!fallbackPtr)
        {
            fallbackPtr  = ptr;
            fallbackSize = size;
        }
    }

    if (fallbackPtr)
    {
        *outSize = fallbackSize;
        return fallbackPtr;
    }
    return nullptr;
}

// Packs [width, height, pixel0, pixel1, ...] into a jintArray -- the same
// convention getEntryPalette() already uses, so the Kotlin side can share
// one unpacking code path for palettes, flats, and graphics alike.
jintArray packImage(JNIEnv* env, int width, int height, const std::vector<jint>& pixels)
{
    jintArray result = env->NewIntArray(2 + static_cast<jsize>(pixels.size()));
    if (!result)
        return nullptr;

    const jint header[2] = { width, height };
    env->SetIntArrayRegion(result, 0, 2, header);
    env->SetIntArrayRegion(result, 2, static_cast<jsize>(pixels.size()), pixels.data());
    return result;
}

// Flat: raw indexed pixels, row-major, no header at all -- just a wall of
// palette-index bytes. androidDetectEntryType() only classifies exactly
// 4096 (classic 64x64) or 4160 (Heretic/Hexen animated liquid flats, 64x64
// plus 64 trailing bytes of engine-specific data that isn't part of the
// pixel grid) as "Flat", so 64x64 read from the front covers both cases.
std::vector<jint> decodeFlat(const uint8_t* data, uint32_t /* size */, const uint8_t* pal)
{
    constexpr int kDim = 64;
    std::vector<jint> pixels(static_cast<size_t>(kDim) * kDim);
    for (int i = 0; i < kDim * kDim; ++i)
    {
        const uint8_t idx = data[i];
        const uint8_t r   = pal[idx * 3 + 0];
        const uint8_t g   = pal[idx * 3 + 1];
        const uint8_t b   = pal[idx * 3 + 2];
        pixels[i] = static_cast<jint>(0xFF000000u | (static_cast<uint32_t>(r) << 16)
                                       | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b));
    }
    return pixels;
}

// Doom picture (patch) format: 4x int16 header (width, height, left/top
// offset -- offsets are for sprite placement and don't affect the pixel
// grid itself, so they're read but unused here), followed by `width`
// little-endian uint32 column offsets relative to the start of the entry.
// Each column is a run-length-encoded list of opaque "posts":
//   u8 topdelta (0xFF marks end-of-column)
//   u8 length
//   u8 unused (padding byte -- DOOM.EXE reads a byte here it never uses)
//   u8[length] pixel data (palette indices)
//   u8 unused (trailing padding byte)
// Anything not covered by a post stays fully transparent (alpha 0) --
// that's the whole point of the format; sprites/HUD graphics are
// non-rectangular cutouts, not solid rectangles. This is the classic
// decode, not the "tall patch" (DeepSea) variant some newer source ports
// support for patches taller than 254px -- fine for the vanilla-range
// IWADs/PWADs this app has been validated against so far.
std::vector<jint> decodeDoomGraphic(const uint8_t* data, uint32_t size, const uint8_t* pal, int* outW, int* outH)
{
    const uint16_t width  = static_cast<uint16_t>(data[0] | (data[1] << 8));
    const uint16_t height = static_cast<uint16_t>(data[2] | (data[3] << 8));

    std::vector<jint> pixels(static_cast<size_t>(width) * height, 0); // transparent by default

    for (uint16_t col = 0; col < width; ++col)
    {
        const uint32_t colOfsPos = 8u + static_cast<uint32_t>(col) * 4u;
        const uint32_t colOfs    = static_cast<uint32_t>(data[colOfsPos]) | (static_cast<uint32_t>(data[colOfsPos + 1]) << 8)
                                 | (static_cast<uint32_t>(data[colOfsPos + 2]) << 16)
                                 | (static_cast<uint32_t>(data[colOfsPos + 3]) << 24);
        if (colOfs >= size)
            continue; // corrupt offset -- skip this column, keep decoding the rest

        uint32_t pos = colOfs;
        while (pos < size)
        {
            const uint8_t topdelta = data[pos];
            if (topdelta == 0xFF)
                break;
            if (pos + 1 >= size)
                break;

            const uint8_t  length    = data[pos + 1];
            const uint32_t dataStart = pos + 3; // skip topdelta, length, unused padding byte
            if (static_cast<uint64_t>(dataStart) + length > size)
                break; // corrupt -- bail on this column rather than read out of bounds

            for (uint8_t y = 0; y < length; ++y)
            {
                const int row = static_cast<int>(topdelta) + y;
                if (row < 0 || row >= height)
                    continue;
                const uint8_t idx = data[dataStart + y];
                const uint8_t r   = pal[idx * 3 + 0];
                const uint8_t g   = pal[idx * 3 + 1];
                const uint8_t b   = pal[idx * 3 + 2];
                pixels[static_cast<size_t>(row) * width + col] = static_cast<jint>(
                        0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8)
                        | static_cast<uint32_t>(b));
            }

            pos = dataStart + length + 1; // + trailing unused padding byte
        }
    }

    *outW = width;
    *outH = height;
    return pixels;
}

// -----------------------------------------------------------------------
// Audio metadata parsers.
//
// Scope explicitly stops at "info", not playback (per HANDOFF's original
// next-steps list: "для звуков -- пока размер / частота, воспроизведение
// опционально"). Each parser reports whatever it can cheaply and honestly
// read from the header alone -- format, channels, sample rate, duration
// where it's actually derivable without a full decode -- and says so
// plainly when it isn't (MIDI/MUS duration depends on tempo events
// scattered through the track data, not a fixed header field; PC Speaker
// effects have no defined sample rate at all).
// -----------------------------------------------------------------------
uint16_t readU16LE(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t readU32LE(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
           | (static_cast<uint32_t>(p[3]) << 24);
}
uint16_t readU16BE(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

std::string formatDuration(double seconds)
{
    if (!(seconds >= 0.0) || seconds > 24.0 * 3600.0) // sanity bound; catches NaN too
        return "неизвестно";
    const int    mins = static_cast<int>(seconds) / 60;
    const double secs = seconds - mins * 60;
    char         buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%05.2f", mins, secs);
    return std::string(buf);
}

std::string parseWav(const uint8_t* data, uint32_t size)
{
    std::ostringstream out;
    out << "Format: WAV (PCM)\n";
    if (size < 12)
        return out.str() + "Заголовок обрезан.";

    uint32_t pos           = 12; // past "RIFF" + size + "WAVE"
    uint16_t channels      = 0;
    uint16_t bitsPerSample = 0;
    uint16_t audioFormat   = 0;
    uint32_t sampleRate    = 0;
    uint32_t byteRate      = 0;
    uint32_t dataSize      = 0;
    bool     haveFmt       = false;
    bool     haveData      = false;

    while (pos + 8 <= size)
    {
        const uint8_t* chunkId       = data + pos;
        const uint32_t chunkSize     = readU32LE(data + pos + 4);
        const uint32_t chunkDataStart = pos + 8;
        if (static_cast<uint64_t>(chunkDataStart) + chunkSize > size)
            break; // truncated/corrupt -- stop rather than read out of bounds

        if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16)
        {
            audioFormat   = readU16LE(data + chunkDataStart);
            channels      = readU16LE(data + chunkDataStart + 2);
            sampleRate    = readU32LE(data + chunkDataStart + 4);
            byteRate      = readU32LE(data + chunkDataStart + 8);
            bitsPerSample = readU16LE(data + chunkDataStart + 14);
            haveFmt       = true;
        }
        else if (std::memcmp(chunkId, "data", 4) == 0)
        {
            dataSize  = chunkSize;
            haveData  = true;
        }

        pos = chunkDataStart + chunkSize + (chunkSize % 2); // chunks are word-aligned
    }

    if (!haveFmt)
        return out.str() + "Не найден chunk 'fmt '.";

    out << "Каналы: " << channels << "\n";
    out << "Частота дискретизации: " << sampleRate << " Hz\n";
    out << "Разрядность: " << bitsPerSample << " бит\n";
    out << "Кодек: " << (audioFormat == 1 ? std::string("PCM") : "код " + std::to_string(audioFormat)) << "\n";
    if (haveData && byteRate > 0)
        out << "Длительность: " << formatDuration(static_cast<double>(dataSize) / byteRate);
    else
        out << "Длительность: неизвестно";

    return out.str();
}

std::string parseDmxSound(const uint8_t* data, uint32_t size)
{
    std::ostringstream out;
    out << "Format: DMX Sound (raw PCM, 8-bit unsigned mono)\n";
    if (size < 8)
        return out.str() + "Заголовок обрезан.";

    const uint16_t sampleRate = readU16LE(data + 2);
    const uint32_t numSamples = readU32LE(data + 4);

    out << "Частота дискретизации: " << sampleRate << " Hz\n";
    out << "Сэмплов: " << numSamples << "\n";
    if (sampleRate > 0)
        out << "Длительность: " << formatDuration(static_cast<double>(numSamples) / sampleRate);
    else
        out << "Длительность: неизвестно";

    return out.str();
}

std::string parsePcSpeakerSound(const uint8_t* data, uint32_t size)
{
    std::ostringstream out;
    out << "Format: PC Speaker Sound (частотная таблица, не PCM)\n";
    if (size < 4)
        return out.str() + "Заголовок обрезан.";

    const uint16_t numSamples = readU16LE(data + 2);
    out << "Тиков: " << numSamples << "\n";
    out << "Длительность: не определяется -- формат не задаёт фиксированную частоту дискретизации";
    return out.str();
}

std::string parseMidi(const uint8_t* data, uint32_t size)
{
    std::ostringstream out;
    out << "Format: MIDI\n";
    if (size < 14)
        return out.str() + "Заголовок обрезан.";

    // Standard MIDI file header fields are big-endian (unlike everything
    // else in this file, which is little-endian x86-native data).
    const uint16_t format   = readU16BE(data + 8);
    const uint16_t ntrks    = readU16BE(data + 10);
    const uint16_t division = readU16BE(data + 12);

    out << "Тип: " << format << "\n";
    out << "Дорожек: " << ntrks << "\n";
    if (division & 0x8000)
        out << "Деление: SMPTE\n";
    else
        out << "Деление: " << (division & 0x7FFF) << " тиков/четверть\n";
    out << "Длительность: требует разбора событий темпа по всем дорожкам, не вычисляется здесь";

    return out.str();
}

std::string parseMus(const uint8_t* data, uint32_t size)
{
    std::ostringstream out;
    out << "Format: MUS Music (id Software)\n";
    if (size < 16)
        return out.str() + "Заголовок обрезан.";

    const uint16_t scoreLen = readU16LE(data + 4);
    const uint16_t channels = readU16LE(data + 8);
    const uint16_t secChans = readU16LE(data + 10);
    const uint16_t instrCnt = readU16LE(data + 12);

    out << "Длина партитуры: " << scoreLen << " байт\n";
    out << "Каналы: " << channels << " (+ " << secChans << " вторичных)\n";
    out << "Инструментов: " << instrCnt << "\n";
    out << "Длительность: требует разбора событий темпа, не вычисляется здесь";

    return out.str();
}

std::string parseGenMidi(uint32_t size)
{
    std::ostringstream out;
    out << "Format: GENMIDI (таблица инструментов OPL для General MIDI)\n";
    out << "Размер: " << size << " байт\n";
    out << "Это таблица тембров синтеза, а не звуковая дорожка -- воспроизводить нечего.";
    return out.str();
}

// FLAC STREAMINFO block -- the mandatory first metadata block, guaranteed
// present right after the "fLaC" magic per spec. 4-byte block header
// (last-block flag + type in byte 0, 24-bit big-endian length), then 34
// bytes of content: min/max block size, min/max frame size, then 64 bits
// packed as sampleRate(20) | channels-1(3) | bitsPerSample-1(5) |
// totalSamples(36) -- unlike everything else parsed in this file, FLAC's
// bitstream is big-endian throughout.
std::string parseFlac(const uint8_t* data, uint32_t size)
{
    std::ostringstream out;
    out << "Format: FLAC\n";
    if (size < 4 + 4 + 34)
        return out.str() + "Заголовок обрезан.";

    const uint8_t blockType = data[4] & 0x7F;
    if (blockType != 0) // 0 = STREAMINFO -- should always be first, but don't assume blindly
        return out.str() + "Первый metadata-блок не STREAMINFO -- не удалось прочитать.";

    const uint32_t infoStart = 8; // past 4-byte magic + 4-byte block header
    uint64_t       packed    = 0;
    for (int i = 0; i < 8; ++i)
        packed = (packed << 8) | data[infoStart + 10 + i];

    const uint32_t sampleRate    = static_cast<uint32_t>((packed >> 44) & 0xFFFFF);
    const uint32_t channels      = static_cast<uint32_t>(((packed >> 41) & 0x7) + 1);
    const uint32_t bitsPerSample = static_cast<uint32_t>(((packed >> 36) & 0x1F) + 1);
    const uint64_t totalSamples  = packed & 0xFFFFFFFFFULL;

    out << "Каналы: " << channels << "\n";
    out << "Частота дискретизации: " << sampleRate << " Hz\n";
    out << "Разрядность: " << bitsPerSample << " бит\n";
    if (sampleRate > 0 && totalSamples > 0)
        out << "Длительность: " << formatDuration(static_cast<double>(totalSamples) / sampleRate);
    else
        out << "Длительность: неизвестно";

    return out.str();
}

// MPEG-1/2/2.5 Layer III frame header, parsed at the first valid frame
// found (skipping an ID3v2 tag if present). Assumes constant bitrate for
// the duration estimate -- accurate for CBR files (common for game-audio
// exports), an approximation for VBR ones, which this says explicitly
// rather than silently presenting a possibly-wrong number as exact.
std::string parseMp3(const uint8_t* data, uint32_t size)
{
    std::ostringstream out;
    out << "Format: MP3\n";

    uint32_t pos = 0;
    if (size >= 10 && std::memcmp(data, "ID3", 3) == 0)
    {
        const uint32_t tagSize = (static_cast<uint32_t>(data[6] & 0x7F) << 21)
                                  | (static_cast<uint32_t>(data[7] & 0x7F) << 14)
                                  | (static_cast<uint32_t>(data[8] & 0x7F) << 7)
                                  | static_cast<uint32_t>(data[9] & 0x7F);
        pos = 10 + tagSize;
    }

    // Scan forward a bit for the first valid frame sync -- some encoders
    // leave a few junk/padding bytes before the first real frame.
    uint32_t syncPos = size; // sentinel: not found
    for (uint32_t i = pos; i + 4 <= size && i < pos + 4096; ++i)
    {
        if (data[i] == 0xFF && (data[i + 1] & 0xE0) == 0xE0)
        {
            syncPos = i;
            break;
        }
    }
    if (syncPos == size)
        return out.str() + "Не найден валидный MP3-фрейм.";

    const uint8_t b1          = data[syncPos + 1];
    const uint8_t b2          = data[syncPos + 2];
    const uint8_t versionBits = (b1 >> 3) & 0x3; // 00=2.5, 10=2, 11=1
    const uint8_t layerBits   = (b1 >> 1) & 0x3; // 01=III, 10=II, 11=I
    if (layerBits != 0x1)
        return out.str() + "Layer != III -- оценка длительности для этого случая не реализована.";

    static const int kBitrateMpeg1[16]   = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 };
    static const int kBitrateMpeg2[16]   = { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 };
    static const int kSampleRateMpeg1[4] = { 44100, 48000, 32000, 0 };
    static const int kSampleRateMpeg2[4] = { 22050, 24000, 16000, 0 };
    static const int kSampleRateMpeg25[4] = { 11025, 12000, 8000, 0 };

    const uint8_t bitrateIdx    = (b2 >> 4) & 0xF;
    const uint8_t sampleRateIdx = (b2 >> 2) & 0x3;

    int bitrateKbps = 0, sampleRate = 0;
    if (versionBits == 0x3) // MPEG1
    {
        bitrateKbps = kBitrateMpeg1[bitrateIdx];
        sampleRate  = kSampleRateMpeg1[sampleRateIdx];
    }
    else if (versionBits == 0x2) // MPEG2
    {
        bitrateKbps = kBitrateMpeg2[bitrateIdx];
        sampleRate  = kSampleRateMpeg2[sampleRateIdx];
    }
    else // MPEG2.5
    {
        bitrateKbps = kBitrateMpeg2[bitrateIdx];
        sampleRate  = kSampleRateMpeg25[sampleRateIdx];
    }

    if (bitrateKbps == 0 || sampleRate == 0)
        return out.str() + "Не удалось разобрать заголовок фрейма (free/bad bitrate).";

    out << "Частота дискретизации: " << sampleRate << " Hz\n";
    out << "Битрейт: " << bitrateKbps << " kbps (из первого фрейма, предполагается CBR)\n";
    const double durationSec = static_cast<double>(size - syncPos) * 8.0 / (bitrateKbps * 1000.0);
    out << "Длительность (оценка): " << formatDuration(durationSec) << "\n";
    out << "Примечание: для VBR-файлов оценка может быть неточной.";

    return out.str();
}

// Ogg Vorbis: reads channel count + sample rate from the Vorbis
// identification header packed into the first page, then scans backward
// from the end of the entry for the last "OggS" page to read its granule
// position (== total PCM sample count for an audio stream, per the Ogg
// spec), giving an exact duration rather than an estimate.
std::string parseOgg(const uint8_t* data, uint32_t size)
{
    std::ostringstream out;
    out << "Format: OGG (предполагается Vorbis)\n";
    if (size < 27 || std::memcmp(data, "OggS", 4) != 0)
        return out.str() + "Заголовок обрезан или повреждён.";

    const uint8_t  pageSegments = data[26];
    const uint32_t payloadStart = 27u + pageSegments;
    if (static_cast<uint64_t>(payloadStart) + 30 > size || data[payloadStart] != 0x01
        || std::memcmp(data + payloadStart + 1, "vorbis", 6) != 0)
        return out.str() + "Не найден Vorbis identification header в первой странице.";

    const uint8_t  channels   = data[payloadStart + 11];
    const uint32_t sampleRate = readU32LE(data + payloadStart + 12);

    out << "Каналы: " << static_cast<int>(channels) << "\n";
    out << "Частота дискретизации: " << sampleRate << " Hz\n";

    // Backward scan for the last Ogg page's granule position (bytes 6-13
    // of the page header, little-endian 64-bit -- unlike the rest of the
    // Ogg page header layout, which the spec itself treats as opaque
    // bytes rather than a specific endianness, granule position is
    // defined as little-endian by every Vorbis/Ogg implementation).
    uint64_t granule = 0;
    bool     found    = false;
    if (size >= 27)
    {
        uint32_t i = size - 4;
        while (true)
        {
            if (data[i] == 'O' && data[i + 1] == 'g' && data[i + 2] == 'g' && data[i + 3] == 'S')
            {
                if (static_cast<uint64_t>(i) + 14 <= size && data[i + 4] == 0) // version byte sanity check
                {
                    granule = 0;
                    for (int b = 0; b < 8; ++b)
                        granule |= static_cast<uint64_t>(data[i + 6 + b]) << (8 * b);
                    found = true;
                }
                break; // last occurrence scanning backward -- stop regardless of validity
            }
            if (i == 0)
                break;
            --i;
        }
    }

    if (found && sampleRate > 0 && granule > 0)
        out << "Длительность: " << formatDuration(static_cast<double>(granule) / sampleRate);
    else
        out << "Длительность: не удалось определить";

    return out.str();
}

// Dispatches to the right parser above based on androidDetectEntryType()'s
// classification. Returns an empty string for any type this file doesn't
// have an audio parser for -- callers treat that the same as "not an
// audio entry at all".
std::string audioInfoFor(const std::string& type, const uint8_t* data, uint32_t size)
{
    if (type == "WAV Sound")
        return parseWav(data, size);
    if (type == "DMX Sound")
        return parseDmxSound(data, size);
    if (type == "PC Speaker Sound")
        return parsePcSpeakerSound(data, size);
    if (type == "MIDI")
        return parseMidi(data, size);
    if (type == "MUS Music")
        return parseMus(data, size);
    if (type == "GENMIDI Instruments")
        return parseGenMidi(size);
    if (type == "FLAC Audio")
        return parseFlac(data, size);
    if (type == "MP3 Audio")
        return parseMp3(data, size);
    if (type == "OGG Audio")
        return parseOgg(data, size);
    return "";
}
} // namespace

// NOTE: method names below (MemChunk::importMem, WadArchive::open(MemChunk&),
// Archive::numEntries) are best guesses based on the SLADE conventions we've
// seen so far in Archive.cpp/WadArchive.cpp/MemChunk.cpp -- not confirmed
// against the actual header. If this doesn't compile, send the error same
// as always and we'll fix the exact names.
// Phase 5 (background processing): these were originally
// Java_..._MainActivity_* -- the `external fun` declarations lived
// directly on MainActivity. They were moved to a dedicated `SladeNative`
// Kotlin object (SladeNative.kt) so every native call is forced through
// one suspend-function wrapper on a single dedicated background thread,
// keeping native calls off the UI thread while still serializing them
// (native side keeps non-thread-safe global archive state, see the
// "Persistent archive holder" comment below). JNI resolves native methods
// by fully-qualified class name, so the exported symbols had to be
// renamed to match the object they now actually live on -- `jobject thiz`
// is unused in all of these (just `this`/the singleton instance), so the
// rename is the only change needed here; none of the logic below cares
// which Kotlin class owns the native method.
extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {

    // Hand-built minimal valid WAD: 12-byte header, 0 lumps.
    // Layout: 4-byte magic ("PWAD"), int32 numlumps, int32 infotableofs.
    unsigned char header[12] = {
        'P', 'W', 'A', 'D',
        0, 0, 0, 0,   // numlumps = 0
        12, 0, 0, 0   // infotableofs = 12 (right after the header)
    };

    MemChunk mc;
    mc.importMem(header, sizeof(header));

    WadArchive wad;
    bool ok = wad.open(mc);

    std::ostringstream out;
    out << "SLADE core on Android\n";
    out << "WadArchive::open() -> " << (ok ? "OK" : "FAILED") << "\n";
    if (ok)
        out << "Entries: " << wad.numEntries();

    std::string result = out.str();
    return env->NewStringUTF(result.c_str());
}

// CHANGED (RecyclerView support): now returns a jobjectArray of jstring
// instead of a single formatted jstring, so the Kotlin side can bind it to
// a RecyclerView adapter instead of dumping everything into one TextView.
//
// CHANGED (real EntryType detection): each entry is now run through
// androidDetectEntryType() (compat/slade_shims.cpp) before being reported,
// so the type name reflects real byte-signature/name-based detection
// rather than always being "unknown". This is a standalone classifier, not
// the real EntryType engine -- see the comment above androidDetectEntryType's
// definition for why (it depends on slade.pk3's ZIP-based resource archive,
// which isn't ported yet).
//
// CHANGED (memory safety, takes an fd instead of a byte[]): used to take a
// jbyteArray that Kotlin filled via InputStream.readBytes() -- reading the
// WHOLE file into a JVM-heap ByteArray, then GetByteArrayElements() below
// potentially copying it AGAIN into native memory, before MemChunk::
// importMem() made a third copy. Fine for a small IWAD, but Sigil/Sigil2's
// soundtrack editions run 50-150MB, and tripling that was almost certainly
// what was crashing the app. Now Kotlin hands us a raw file descriptor
// (MainActivity.kt: ParcelFileDescriptor.detachFd()) and we mmap() it
// directly -- MemChunk still makes its own one copy (it owns its buffer,
// unchanged from before), but the JVM-heap and JNI-array copies are gone
// entirely. fd ownership is ours from here on: we close() it ourselves
// once mmap() has been called (the mapping stays valid independently of
// the descriptor after that, per standard POSIX semantics).
//
// Format:
//   - success: one array element per entry, each "name\tsize\ttype" (tab-
//     separated, parsed on the Kotlin side).
//   - failure: a single-element array whose entry starts with "ERROR: "
//     followed by an error message.
// Builds the "name\tsize\ttype" array shared by openWadFileFd() (after a
// fresh open) and listEntries() (Phase 7, after an edit changes the
// index/entry-count of an already-open session -- rename/delete need the
// Kotlin-side adapter to reload from scratch rather than patch its cached
// list locally, since removeEntry() shifts every later index down by one).
// Assumes g_session is already open; callers are responsible for that.
jobjectArray buildEntryListArray(JNIEnv* env)
{
    jclass stringClass = env->FindClass("java/lang/String");

    auto*    wad   = g_session.archive();
    auto*    mc    = g_session.memChunk();
    unsigned count = wad->numEntries();
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(count), stringClass, nullptr);

    for (unsigned i = 0; i < count; ++i)
    {
        auto* entry = wad->entryAt(i);

        std::ostringstream line;
        if (entry)
        {
            const uint32_t size = entry->size();
            const uint8_t* ptr  = nullptr;
            // Phase 7 (Export/Import/Replace): same isLoaded()-first check
            // as entryDataPtr() -- a replaced entry's real current bytes
            // live in entry->rawData(), not at its old offset in mc. See
            // entryDataPtr()'s comment for the full reasoning.
            if (entry->isLoaded())
            {
                if (size > 0)
                    ptr = entry->rawData(false);
            }
            else
            {
                const uint32_t offset = wad->getEntryOffset(entry);
                if (size > 0 && static_cast<uint64_t>(offset) + size <= mc->size())
                    ptr = mc->data() + offset;
            }

            line << entry->name() << "\t" << size << "\t" << androidDetectEntryType(entry->upperName(), size, ptr);
        }
        else
            line << "?\t0\t?";

        env->SetObjectArrayElement(
            result,
            static_cast<jsize>(i),
            env->NewStringUTF(line.str().c_str()));
    }

    return result;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_openWadFileFd(
        JNIEnv* env,
        jobject /* this */,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);
    jclass stringClass = env->FindClass("java/lang/String");

    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size <= 0)
    {
        close(fd);
        jobjectArray result = env->NewObjectArray(1, stringClass, nullptr);
        env->SetObjectArrayElement(result, 0, env->NewStringUTF("ERROR: could not stat file"));
        return result;
    }

    const size_t fileSize = static_cast<size_t>(st.st_size);
    void*        mapped   = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); // safe: the mapping (if it succeeded) stays valid regardless

    if (mapped == MAP_FAILED)
    {
        jobjectArray result = env->NewObjectArray(1, stringClass, nullptr);
        env->SetObjectArrayElement(result, 0, env->NewStringUTF("ERROR: mmap failed"));
        return result;
    }

    // Drop whatever archive was open before (if any) -- single-session
    // app, opening a new WAD replaces the old one entirely. g_session.
    // open() does this internally too (see its own close() call at the
    // top), but doing it explicitly here first means a failed mmap/stat
    // above still leaves any previous archive in place rather than tearing
    // it down before we know the new one is even readable.
    if (mapped == MAP_FAILED)
    {
        // (unreachable in practice -- checked above -- kept only so this
        // comment block reads top-to-bottom; see the early return above.)
    }

    const bool ok = g_session.open(
        reinterpret_cast<const unsigned char*>(mapped),
        static_cast<uint32_t>(fileSize));
    munmap(mapped, fileSize); // g_session has its own copy now -- the mapping is no longer needed

    if (!ok)
    {
        std::string msg = "ERROR: " + global::error;
        jobjectArray result = env->NewObjectArray(1, stringClass, nullptr);
        env->SetObjectArrayElement(result, 0, env->NewStringUTF(msg.c_str()));
        return result;
    }

    return buildEntryListArray(env);
}

// -----------------------------------------------------------------------
// getEntryText / getEntryPalette -- content viewers for the first two
// entry types (see HANDOFF discussion). Both look the entry up again by
// index in the persistent g_wad/g_mc pair rather than trusting anything
// cached on the Kotlin side, and both independently re-run
// androidDetectEntryType() rather than trusting the type string Kotlin
// already has -- cheap, and it means these functions are self-contained
// and can't be tricked into misreading an entry as the wrong type by a
// stale adapter list.
// -----------------------------------------------------------------------

// Returns the entry's bytes as a sanitized-ASCII jstring, or null if the
// archive isn't open, the index is invalid, or the entry isn't classified
// as "Text". Capped at 256 KB so a large ACS-source or SNDINFO lump
// doesn't hand a multi-megabyte string across the JNI boundary just for a
// preview dialog.
extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryText(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t        size = 0;
    ArchiveEntry*    entry = nullptr;
    const uint8_t*   ptr  = entryDataPtr(index, &size, &entry);
    if (!ptr)
        return nullptr;

    if (androidDetectEntryType(entry->upperName(), size, ptr) != "Text")
        return nullptr;

    constexpr uint32_t kMaxPreview = 262144; // 256 KB
    const uint32_t     previewLen  = size < kMaxPreview ? size : kMaxPreview;

    std::string text = sanitizeAscii(ptr, previewLen);
    if (previewLen < size)
        text += "\n\n[... truncated, entry is " + std::to_string(size) + " bytes ...]";

    return env->NewStringUTF(text.c_str());
}

// Returns a packed jintArray: [width, height, pixel0, pixel1, ...] where
// pixels are ARGB_8888 (matches android.graphics.Bitmap.Config.ARGB_8888
// directly, no conversion needed on the Kotlin side), rendered as a
// 16-column grid of color swatches. Returns null under the same
// conditions as getEntryText -- plus if the entry isn't "Palette", or its
// size isn't a clean multiple of 3 (each color is one RGB triple; PLAYPAL-
// style lumps have no alpha byte of their own, so alpha is forced to
// fully opaque here).
extern "C" JNIEXPORT jintArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryPalette(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t       size  = 0;
    ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = entryDataPtr(index, &size, &entry);
    if (!ptr)
        return nullptr;

    if (androidDetectEntryType(entry->upperName(), size, ptr) != "Palette")
        return nullptr;

    const uint32_t numColors = size / 3;
    if (numColors == 0)
        return nullptr;

    constexpr int kCols = 16;
    const int     rows  = static_cast<int>((numColors + kCols - 1) / kCols);

    std::vector<jint> pixels(static_cast<size_t>(kCols) * rows, 0xFF000000); // opaque black padding
    for (uint32_t i = 0; i < numColors; ++i)
    {
        const uint8_t r = ptr[i * 3 + 0];
        const uint8_t g = ptr[i * 3 + 1];
        const uint8_t b = ptr[i * 3 + 2];
        pixels[i] = static_cast<jint>(0xFF000000u | (static_cast<uint32_t>(r) << 16)
                                       | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b));
    }

    jintArray result = env->NewIntArray(2 + static_cast<jsize>(pixels.size()));
    if (!result)
        return nullptr;

    const jint header[2] = { kCols, rows };
    env->SetIntArrayRegion(result, 0, 2, header);
    env->SetIntArrayRegion(result, 2, static_cast<jsize>(pixels.size()), pixels.data());
    return result;
}

// Returns a packed jintArray in the same [width, height, pixels...]
// convention as getEntryPalette(), decoded via decodeFlat()/
// decodeDoomGraphic() above. Returns null if: the archive isn't open, the
// index is invalid, the entry isn't classified as "Flat" or "Doom
// Graphic", the archive has no usable palette (findPalette() found
// nothing -- some PWADs genuinely don't carry their own PLAYPAL), or (for
// Doom Graphic specifically) the header claims a width/height that
// doesn't actually fit inside the entry's declared size.
extern "C" JNIEXPORT jintArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryImage(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t       size  = 0;
    ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = entryDataPtr(index, &size, &entry);
    if (!ptr)
        return nullptr;

    const std::string type = androidDetectEntryType(entry->upperName(), size, ptr);
    if (type != "Flat" && type != "Doom Graphic")
        return nullptr;

    uint32_t       palSize = 0;
    const uint8_t* pal     = findPalette(&palSize);
    if (!pal)
        return nullptr; // no PLAYPAL (or equivalent) anywhere in this archive

    if (type == "Flat")
    {
        // androidDetectEntryType() only returns "Flat" for exactly 4096 or
        // 4160 bytes -- both comfortably cover the 64x64 = 4096 pixels
        // decodeFlat() reads, so no further size check needed here.
        std::vector<jint> pixels = decodeFlat(ptr, size, pal);
        return packImage(env, 64, 64, pixels);
    }

    // Doom Graphic: androidDetectEntryType() already sanity-checked that
    // size >= 8 + 4*width and that the first column offset lands inside
    // the entry, but decodeDoomGraphic() re-derives width from the header
    // itself and bounds-checks every column offset independently, so
    // there's no need to re-validate here.
    int w = 0, h = 0;
    std::vector<jint> pixels = decodeDoomGraphic(ptr, size, pal, &w, &h);
    return packImage(env, w, h, pixels);
}

// Returns the entry's raw bytes as-is, for entries classified "PNG
// Image" only -- standard PNG bytes decode directly via Android's
// BitmapFactory on the Kotlin side, so there's no need to decode them
// here the way Doom Graphic/Flat need to be (they're not a format any
// Android API understands on its own). Capped at 16 MB: generous for any
// texture a WAD realistically embeds, but a guard against handing a
// pathologically large buffer across the JNI boundary and into a
// JVM-heap Bitmap decode on a 3GB-RAM device with no way to recover from
// an OOM mid-decode.
extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryPng(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t       size  = 0;
    ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = entryDataPtr(index, &size, &entry);
    if (!ptr)
        return nullptr;

    if (androidDetectEntryType(entry->upperName(), size, ptr) != "PNG Image")
        return nullptr;

    constexpr uint32_t kMaxPngBytes = 16 * 1024 * 1024; // 16 MB
    if (size > kMaxPngBytes)
        return nullptr;

    jbyteArray result = env->NewByteArray(static_cast<jsize>(size));
    if (!result)
        return nullptr;
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte*>(ptr));
    return result;
}

// Returns a formatted multi-line info string for any of the nine audio
// lump types androidDetectEntryType() recognizes (see audioInfoFor()
// above), or null if the entry isn't audio, the archive isn't open, or
// the index is invalid. Deliberately info-only, not playback -- see the
// comment above the parser block for why.
extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_getEntryAudioInfo(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    uint32_t       size  = 0;
    ArchiveEntry*  entry = nullptr;
    const uint8_t* ptr   = entryDataPtr(index, &size, &entry);
    if (!ptr)
        return nullptr;

    const std::string type = androidDetectEntryType(entry->upperName(), size, ptr);
    const std::string info = audioInfoFor(type, ptr, size);
    if (info.empty())
        return nullptr;

    return env->NewStringUTF(info.c_str());
}

// -----------------------------------------------------------------------
// Phase 7 (ROADMAP.md) -- WAD Editor MVP: first real edit operations.
// Scope for this pass: rename, delete, and Save As. Not yet done (left
// for a later pass, see comments at each function): Export/Import/
// Replace/Add/Move entry, and Phase 8's full "write to temp file, verify,
// then replace" safe-save flow -- Save As here writes straight to
// wherever the user points the SAF picker, which is still safe in the
// sense the ROADMAP cares about (the *original* WAD is never touched
// during editing -- Kotlin opens it read-only and this session never
// holds a writable fd to it) but doesn't yet cover overwriting the same
// file in place with a verify-before-replace step.
// -----------------------------------------------------------------------

// Renames the entry at `index` to `newName`. Returns false (and leaves
// the entry unchanged) if the archive isn't open, the index is invalid,
// or WadArchive::renameEntry() itself rejects the name (e.g. name too
// long for the WAD directory's fixed 8-byte slot -- `force=false` here,
// so SLADE's own validation applies rather than silently truncating).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeRenameEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index,
        jstring newName) {

    if (!g_session.isOpen() || index < 0)
        return JNI_FALSE;

    auto* wad   = g_session.archive();
    auto* entry = wad->entryAt(static_cast<unsigned>(index));
    if (!entry)
        return JNI_FALSE;

    const char* nameChars = env->GetStringUTFChars(newName, nullptr);
    const bool  ok        = wad->renameEntry(entry, nameChars);
    env->ReleaseStringUTFChars(newName, nameChars);

    if (ok)
        g_session.markDirty();
    return ok ? JNI_TRUE : JNI_FALSE;
}

// Deletes the entry at `index`. Returns false under the same "nothing to
// do" conditions as renameEntry(). IMPORTANT for the Kotlin side: every
// index after the deleted one shifts down by one -- callers must refresh
// their entry list via listEntries() rather than just removing one row
// from a cached adapter list, or later taps will read the wrong entry.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeDeleteEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index) {

    if (!g_session.isOpen() || index < 0)
        return JNI_FALSE;

    auto* wad   = g_session.archive();
    auto* entry = wad->entryAt(static_cast<unsigned>(index));
    if (!entry)
        return JNI_FALSE;

    const bool ok = wad->removeEntry(entry);
    if (ok)
        g_session.markDirty();
    return ok ? JNI_TRUE : JNI_FALSE;
}

// Moves the entry at `index` to `newPosition` (Phase 7's last remaining
// MVP operation). `newPosition` is an insertion index computed by the
// Kotlin caller as `index - 1` (up) or `index + 1` (down) -- see
// MainActivity.moveEntryBy() -- NOT re-derived here from a "direction"
// enum, so this function stays a thin wrapper with no off-by-one logic
// of its own to get wrong.
//
// IMPORTANT (WadArchive::moveEntry / Archive::moveEntry, SLADE source):
// internally this removes the entry first, THEN inserts it at
// `newPosition` in the now-one-shorter list. That means `newPosition` is
// an index into the list AFTER removal, not the original list -- moving
// entry at index 2 down to `newPosition=3` does NOT swap it with the old
// index-3 entry the way it would if `newPosition` meant "the original
// index 3 slot"; walking through it by hand: removing index 2 first
// shifts the old index-3 entry down to sit at index 2, and inserting the
// moved entry at position 3 puts it right after that (unchanged) entry --
// i.e. exactly the adjacent swap "move down by one" should do. This only
// happens to fall out correctly for the +-1 adjacent-swap case the UI
// buttons use; a arbitrary drag-and-drop target index (future work, see
// ROADMAP.md) will need an explicit adjustment here, not just index+-1.
//
// addEntry() (called internally past the removal step) clamps an
// out-of-range position to "end of list" rather than failing -- so moving
// the second-to-last entry down (`newPosition == old entry count - 1`,
// which is one past the last valid index in the post-removal list) still
// correctly lands it at the very end instead of erroring out.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeMoveEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index,
        jint newPosition) {

    if (!g_session.isOpen() || index < 0 || newPosition < 0)
        return JNI_FALSE;

    auto* wad   = g_session.archive();
    auto* entry = wad->entryAt(static_cast<unsigned>(index));
    if (!entry)
        return JNI_FALSE;

    const bool ok = wad->moveEntry(entry, static_cast<unsigned>(newPosition));
    if (ok)
        g_session.markDirty();
    return ok ? JNI_TRUE : JNI_FALSE;
}

// Re-lists the currently-open session's entries in the same format as
// openWadFileFd() -- see buildEntryListArray()'s comment for why this
// needs to be a full re-list rather than an incremental patch. Returns
// an empty array (not null) if no archive is open, since Kotlin's
// existing parser (MainActivity.handleWadResult) only special-cases a
// single "ERROR: "-prefixed element, not null/empty.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeListEntries(
        JNIEnv* env,
        jobject /* this */) {

    if (!g_session.isOpen())
        return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    return buildEntryListArray(env);
}

// Whether the session has unsaved changes (Phase 6/9 dirty-state
// tracking). Kotlin polls this after every edit to update a "*" in the
// title/status rather than the native side pushing state -- there's no
// JNI-to-Kotlin callback path set up, and dirty state only ever changes
// as the direct result of a Kotlin-initiated call anyway (rename/delete/
// save), so polling right after each one is enough.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeIsDirty(
        JNIEnv* env,
        jobject /* this */) {

    return g_session.isDirty() ? JNI_TRUE : JNI_FALSE;
}

// Save As: serializes the current session to a MemChunk via WadArchive::
// write(MemChunk&) (rebuilds the WAD directory from the current in-memory
// entry list, per SLADE's own convention -- not a byte-for-byte copy of
// the original file), then writes that MemChunk's bytes to `fd` in one
// shot. `fd` is expected to come from a SAF ACTION_CREATE_DOCUMENT result
// opened "rwt" (the Kotlin side truncates/creates fresh, same detachFd()
// ownership-transfer contract as the read path in openWadFileFd) -- this
// function closes fd itself once done, on both success and failure paths.
// Clears the session's dirty flag on success only, so a failed save
// leaves the "*" up rather than silently discarding the fact that the
// save didn't actually happen.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeSaveToFd(
        JNIEnv* env,
        jobject /* this */,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    if (!g_session.isOpen())
    {
        close(fd);
        return JNI_FALSE;
    }

    // allowIwadOverwrite=true: see serializeSession()'s comment -- Save As
    // always writes to a brand-new file (MainActivity's saveAsLauncher
    // uses a separate SAF picker from the one the original was opened
    // through), so iwad_lock's protection against clobbering a real IWAD
    // in place doesn't apply to this code path.
    MemChunk out;
    if (!serializeSession(out, /*allowIwadOverwrite=*/true))
    {
        close(fd);
        return JNI_FALSE;
    }

    const uint8_t* data      = out.data();
    size_t         remaining = out.size();
    while (remaining > 0)
    {
        const ssize_t written = write(fd, data, remaining);
        if (written <= 0) // interrupted/error -- no retry-on-EINTR loop needed for a local file fd
        {
            close(fd);
            return JNI_FALSE;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
    close(fd);

    g_session.clearDirty();
    return JNI_TRUE;
}

// Phase 8 (Safe Save), step 1 of 2 -- see ArchiveSession::hasPendingSave()'s
// comment for the full reasoning on why this is split from step 2
// (nativeCommitSave) into two separate JNI calls instead of one function
// like nativeSaveToFd above. In short: Kotlin must not open the ORIGINAL
// document for writing until this step has already validated the new
// content, because opening a document in "rwt" mode truncates it
// immediately -- before either side has written a single byte -- so any
// validation done *after* that point would be validating over an already-
// destroyed original.
//
// Does the same serialize-and-prime work as nativeSaveToFd, but with
// allowIwadOverwrite=false (this WILL eventually overwrite the file the
// archive was opened from, unlike Save As -- exactly the case iwad_lock
// exists for), then re-parses the serialized bytes as a fresh WadArchive
// and sanity-checks entry count matches -- turning ROADMAP.md's "Reopen
// validation (пока только вручную)" into something automatic, at least
// for this one structural check.
//
// Returns null on success (with the validated bytes stashed in
// g_session's pendingSave for nativeCommitSave to pick up), or a non-null
// error string on failure -- distinct from the plain-boolean convention
// elsewhere, since "IWAD saving disabled" vs. "validation failed" vs. "no
// archive open" are meaningfully different things to tell the user here.
extern "C" JNIEXPORT jstring JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeValidateForSave(
        JNIEnv* env,
        jobject /* this */) {

    if (!g_session.isOpen())
        return env->NewStringUTF("Архив не открыт");

    auto* wad = g_session.archive();
    const unsigned expectedEntries = wad->numEntries();

    auto out = std::make_unique<MemChunk>();
    if (!serializeSession(*out, /*allowIwadOverwrite=*/false))
        return env->NewStringUTF(global::error.c_str());

    // Validation: re-parse the just-serialized bytes as an independent
    // WadArchive (own MemChunk, own object -- doesn't touch g_session at
    // all) rather than trusting that write() succeeding means the result
    // is actually readable. Cheap insurance against a WadArchive::write()
    // bug producing a directory SLADE itself can't parse back.
    MemChunk validateMc;
    validateMc.importMem(out->data(), static_cast<uint32_t>(out->size()));
    WadArchive validate;
    if (!validate.open(validateMc))
        return env->NewStringUTF(("Проверка не пройдена: " + global::error).c_str());

    if (validate.numEntries() != expectedEntries)
    {
        std::string msg = "Проверка не пройдена: ожидалось " + std::to_string(expectedEntries)
                           + " entries, получено " + std::to_string(validate.numEntries());
        return env->NewStringUTF(msg.c_str());
    }

    g_session.setPendingSave(std::move(out));
    return nullptr;
}

// Phase 8 (Safe Save), step 2 of 2 -- writes the bytes nativeValidateForSave
// already validated and stashed to `fd`. `fd` is expected to come from
// re-opening the ORIGINAL document (the one the archive was opened from)
// in "rwt" mode -- Kotlin's job is to have only done that AFTER step 1
// returned success, per this function's own file comment above. Closes
// fd itself, on both the success and failure path, same contract as
// nativeSaveToFd. Clears pendingSave either way (a half-written or
// failed attempt shouldn't be retried blind with stale bytes -- if it
// needs retrying, step 1 should run again first).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeCommitSave(
        JNIEnv* env,
        jobject /* this */,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    if (!g_session.isOpen() || !g_session.hasPendingSave())
    {
        close(fd);
        g_session.clearPendingSave();
        return JNI_FALSE;
    }

    MemChunk*      out       = g_session.pendingSave();
    const uint8_t* data      = out->data();
    size_t         remaining = out->size();
    while (remaining > 0)
    {
        const ssize_t written = write(fd, data, remaining);
        if (written <= 0)
        {
            close(fd);
            g_session.clearPendingSave();
            return JNI_FALSE;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
    close(fd);

    g_session.clearPendingSave();
    g_session.clearDirty();
    return JNI_TRUE;
}

// Phase 8: Discard Changes. Thin wrapper -- see ArchiveSession::
// discardChanges()'s comment for why this is safe/cheap (mc_ is never
// mutated by any edit operation, so "undo everything" is just reparsing
// it fresh). Returns false if nothing is open; Kotlin doesn't need to
// distinguish that from "discard itself failed" since both mean nothing
// changed, and the button that triggers this is disabled when
// !archiveOpen anyway.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeDiscardChanges(
        JNIEnv* env,
        jobject /* this */) {

    return g_session.discardChanges() ? JNI_TRUE : JNI_FALSE;
}

// -----------------------------------------------------------------------
// Export / Add / Replace (ROADMAP.md's remaining Phase 7 items).
// -----------------------------------------------------------------------

// Reads the whole contents of `fd` into memory via mmap, then closes fd.
// Returns nullptr (with *outSize == 0) on any failure. Caller owns the
// mapping and must munmap() it at *outSize bytes once done -- same
// fstat-then-mmap pattern openWadFileFd() already uses for the original
// WAD, reused here since Add/Replace both just need a picked file's whole
// contents once, to hand to ArchiveEntry::importMem() (which immediately
// copies them into its own storage -- the mapping's job ends there).
const void* mmapWholeFile(int fd, size_t* outSize)
{
    *outSize = 0;

    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size <= 0)
    {
        close(fd);
        return nullptr;
    }

    const size_t size   = static_cast<size_t>(st.st_size);
    void*        mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); // safe: the mapping (if it succeeded) stays valid regardless -- same reasoning as openWadFileFd()

    if (mapped == MAP_FAILED)
        return nullptr;

    *outSize = size;
    return mapped;
}

// Exports the entry at `index`'s current bytes to `fd` (a SAF
// ACTION_CREATE_DOCUMENT result, same "rwt"/detachFd() contract as
// saveToFd()). Uses entryDataPtr() -- NOT a raw offset lookup -- so
// exporting a just-Replaced-but-not-yet-saved entry exports its NEW
// content, not what used to be on disk.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeExportEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    uint32_t       size = 0;
    const uint8_t* data = entryDataPtr(index, &size);
    if (!data)
    {
        close(fd);
        return JNI_FALSE;
    }

    size_t remaining = size;
    while (remaining > 0)
    {
        const ssize_t written = write(fd, data, remaining);
        if (written <= 0)
        {
            close(fd);
            return JNI_FALSE;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
    close(fd);
    return JNI_TRUE;
}

// Adds a brand-new entry named `name` at the end of the archive, with
// content read whole from `fd` (a SAF ACTION_OPEN_DOCUMENT result, "r"
// mode -- read-only, same detachFd() contract as the read path in
// openWadFileFd()). WadArchive::addEntry() sanitizes `name` itself (wad-
// friendly: 8 chars max, no extension) per its own doc comment, so
// whatever the picked file's display name was is passed through as-is;
// no need to duplicate that sanitization on the Kotlin side.
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeAddEntry(
        JNIEnv* env,
        jobject /* this */,
        jstring name,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    if (!g_session.isOpen())
    {
        close(fd);
        return JNI_FALSE;
    }

    size_t      size = 0;
    const void* data = mmapWholeFile(fd, &size);
    if (!data)
        return JNI_FALSE;

    const char* nameChars = env->GetStringUTFChars(name, nullptr);
    auto        entry     = std::make_shared<ArchiveEntry>(nameChars, static_cast<uint32_t>(size));
    env->ReleaseStringUTFChars(name, nameChars);

    entry->importMem(data, static_cast<uint32_t>(size));
    munmap(const_cast<void*>(data), size);

    auto*      wad = g_session.archive();
    const bool ok  = static_cast<bool>(wad->addEntry(entry, wad->numEntries(), nullptr));
    if (ok)
        g_session.markDirty();
    return ok ? JNI_TRUE : JNI_FALSE;
}

// Replaces the entry at `index`'s content in place (name and position
// unchanged) with `fd`'s whole contents -- same read source/contract as
// addEntry() above. ArchiveEntry::importMem() updates the entry's size to
// match automatically; nothing else needs to be told about the new size
// (entryDataPtr()/buildEntryListArray() pick it up next time they read
// this entry, via the isLoaded() check added alongside this function).
extern "C" JNIEXPORT jboolean JNICALL
Java_com_oleglati_slade_13_1mobile_SladeNative_nativeReplaceEntry(
        JNIEnv* env,
        jobject /* this */,
        jint index,
        jint fdRaw) {

    const int fd = static_cast<int>(fdRaw);

    if (!g_session.isOpen() || index < 0)
    {
        close(fd);
        return JNI_FALSE;
    }

    auto* wad   = g_session.archive();
    auto* entry = wad->entryAt(static_cast<unsigned>(index));
    if (!entry)
    {
        close(fd);
        return JNI_FALSE;
    }

    size_t      size = 0;
    const void* data = mmapWholeFile(fd, &size);
    if (!data)
        return JNI_FALSE;

    const bool ok = entry->importMem(data, static_cast<uint32_t>(size));
    munmap(const_cast<void*>(data), size);

    if (ok)
        g_session.markDirty();
    return ok ? JNI_TRUE : JNI_FALSE;
}
