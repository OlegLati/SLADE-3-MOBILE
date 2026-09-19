#include "Main.h"
#include "ArchiveAudioPreview.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

namespace slade_mobile
{
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
    if (type != "GENMIDI Instruments" && (!data || size == 0))
        return "";

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


} // namespace slade_mobile
