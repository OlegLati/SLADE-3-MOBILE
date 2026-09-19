#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ArchiveAudioPreview.h"

namespace
{
void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void putLE16(std::vector<uint8_t>& data, size_t offset, uint16_t value)
{
    data[offset + 0] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void putLE32(std::vector<uint8_t>& data, size_t offset, uint32_t value)
{
    data[offset + 0] = static_cast<uint8_t>(value);
    data[offset + 1] = static_cast<uint8_t>(value >> 8);
    data[offset + 2] = static_cast<uint8_t>(value >> 16);
    data[offset + 3] = static_cast<uint8_t>(value >> 24);
}

void putBE16(std::vector<uint8_t>& data, size_t offset, uint16_t value)
{
    data[offset + 0] = static_cast<uint8_t>(value >> 8);
    data[offset + 1] = static_cast<uint8_t>(value);
}

void putBE64(std::vector<uint8_t>& data, size_t offset, uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        data[offset + i] = static_cast<uint8_t>(value >> (56 - i * 8));
}

void expectContains(const std::string& text, const char* needle, const char* message)
{
    check(text.find(needle) != std::string::npos, message);
}

std::vector<uint8_t> makeWav()
{
    // 0.1 s of mono 8-bit PCM at 8000 Hz.
    std::vector<uint8_t> wav(44 + 800, 0);
    wav[0] = 'R'; wav[1] = 'I'; wav[2] = 'F'; wav[3] = 'F';
    putLE32(wav, 4, static_cast<uint32_t>(wav.size() - 8));
    wav[8] = 'W'; wav[9] = 'A'; wav[10] = 'V'; wav[11] = 'E';

    wav[12] = 'f'; wav[13] = 'm'; wav[14] = 't'; wav[15] = ' ';
    putLE32(wav, 16, 16);
    putLE16(wav, 20, 1);
    putLE16(wav, 22, 1);
    putLE32(wav, 24, 8000);
    putLE32(wav, 28, 8000);
    putLE16(wav, 32, 1);
    putLE16(wav, 34, 8);

    wav[36] = 'd'; wav[37] = 'a'; wav[38] = 't'; wav[39] = 'a';
    putLE32(wav, 40, 800);
    return wav;
}

std::vector<uint8_t> makeDmx()
{
    std::vector<uint8_t> data(8, 0);
    putLE16(data, 2, 11025);
    putLE32(data, 4, 11025);
    return data;
}

std::vector<uint8_t> makePcSpeaker()
{
    std::vector<uint8_t> data(4, 0);
    putLE16(data, 2, 123);
    return data;
}

std::vector<uint8_t> makeMidi()
{
    std::vector<uint8_t> data(14, 0);
    data[0] = 'M'; data[1] = 'T'; data[2] = 'h'; data[3] = 'd';
    putBE16(data, 8, 1);
    putBE16(data, 10, 2);
    putBE16(data, 12, 96);
    return data;
}

std::vector<uint8_t> makeMus()
{
    std::vector<uint8_t> data(16, 0);
    data[0] = 'M'; data[1] = 'U'; data[2] = 'S'; data[3] = 0x1A;
    putLE16(data, 4, 32);
    putLE16(data, 6, 64);
    putLE16(data, 8, 2);
    putLE16(data, 10, 1);
    putLE16(data, 12, 4);
    return data;
}

std::vector<uint8_t> makeFlac()
{
    std::vector<uint8_t> data(42, 0);
    data[0] = 'f'; data[1] = 'L'; data[2] = 'a'; data[3] = 'C';
    data[4] = 0x80; // last STREAMINFO block
    data[5] = 0;
    data[6] = 0;
    data[7] = 34;

    const uint64_t packed = (static_cast<uint64_t>(44100) << 44)
                            | (static_cast<uint64_t>(1) << 41)
                            | (static_cast<uint64_t>(15) << 36)
                            | 44100;
    putBE64(data, 18, packed);
    return data;
}

std::vector<uint8_t> makeMp3()
{
    return {0xFF, 0xFB, 0x90, 0x00};
}

std::vector<uint8_t> makeOgg()
{
    std::vector<uint8_t> data(58, 0);
    data[0] = 'O'; data[1] = 'g'; data[2] = 'g'; data[3] = 'S';
    data[4] = 0;
    data[26] = 1;
    data[27] = 0x01;
    data[28] = 'v'; data[29] = 'o'; data[30] = 'r'; data[31] = 'b'; data[32] = 'i'; data[33] = 's';
    data[38] = 2;
    putLE32(data, 39, 44100);
    uint64_t granule = 44100;
    for (int i = 0; i < 8; ++i)
        data[6 + i] = static_cast<uint8_t>(granule >> (8 * i));
    return data;
}

void testWav()
{
    const auto data = makeWav();
    const std::string info = slade_mobile::audioInfoFor(
            "WAV Sound", data.data(), static_cast<uint32_t>(data.size()));
    expectContains(info, "8000 Hz", "WAV reports sample rate");
    expectContains(info, "PCM", "WAV reports PCM");
    expectContains(info, "0:00.10", "WAV reports duration");
}

void testDmx()
{
    const auto data = makeDmx();
    const std::string info = slade_mobile::audioInfoFor(
            "DMX Sound", data.data(), static_cast<uint32_t>(data.size()));
    expectContains(info, "11025 Hz", "DMX reports sample rate");
    expectContains(info, "Сэмплов: 11025", "DMX reports sample count");
    expectContains(info, "0:01.00", "DMX reports duration");
}

void testPcSpeaker()
{
    const auto data = makePcSpeaker();
    const std::string info = slade_mobile::audioInfoFor(
            "PC Speaker Sound", data.data(), static_cast<uint32_t>(data.size()));
    expectContains(info, "Тиков: 123", "PC Speaker reports tick count");
    expectContains(info, "Длительность: не определяется", "PC Speaker reports no fixed duration");
}

void testMidi()
{
    const auto data = makeMidi();
    const std::string info = slade_mobile::audioInfoFor(
            "MIDI", data.data(), static_cast<uint32_t>(data.size()));
    expectContains(info, "Тип: 1", "MIDI reports format type");
    expectContains(info, "Дорожек: 2", "MIDI reports track count");
    expectContains(info, "96 тиков/четверть", "MIDI reports PPQ division");
}

void testMus()
{
    const auto data = makeMus();
    const std::string info = slade_mobile::audioInfoFor(
            "MUS Music", data.data(), static_cast<uint32_t>(data.size()));
    expectContains(info, "Длина партитуры: 32 байт", "MUS reports score length");
    expectContains(info, "Каналы: 2 (+ 1 вторичных)", "MUS reports channels");
    expectContains(info, "Инструментов: 4", "MUS reports instrument count");
}

void testGenMidi()
{
    const std::string info = slade_mobile::audioInfoFor(
            "GENMIDI Instruments", nullptr, 1234);
    expectContains(info, "Размер: 1234 байт", "GENMIDI reports entry size");
    expectContains(info, "воспроизводить нечего", "GENMIDI identifies itself as instrument data");
}

void testFlac()
{
    const auto data = makeFlac();
    const std::string info = slade_mobile::audioInfoFor(
            "FLAC Audio", data.data(), static_cast<uint32_t>(data.size()));
    expectContains(info, "Каналы: 2", "FLAC reports channels");
    expectContains(info, "44100 Hz", "FLAC reports sample rate");
    expectContains(info, "16 бит", "FLAC reports bit depth");
    expectContains(info, "0:01.00", "FLAC reports duration");
}

void testMp3()
{
    const auto data = makeMp3();
    const std::string info = slade_mobile::audioInfoFor(
            "MP3 Audio", data.data(), static_cast<uint32_t>(data.size()));
    expectContains(info, "44100 Hz", "MP3 reports sample rate");
    expectContains(info, "128 kbps", "MP3 reports bitrate");
}

void testOgg()
{
    const auto data = makeOgg();
    const std::string info = slade_mobile::audioInfoFor(
            "OGG Audio", data.data(), static_cast<uint32_t>(data.size()));
    expectContains(info, "Каналы: 2", "OGG reports channels");
    expectContains(info, "44100 Hz", "OGG reports sample rate");
    expectContains(info, "0:01.00", "OGG reports duration");
}

void testTruncatedInputs()
{
    const uint8_t tiny[] = {0};

    const char* truncatedTypes[] = {
        "WAV Sound",
        "DMX Sound",
        "PC Speaker Sound",
        "MIDI",
        "MUS Music",
        "FLAC Audio",
        "OGG Audio",
    };

    for (const char* type : truncatedTypes)
    {
        const std::string info = slade_mobile::audioInfoFor(type, tiny, 1);
        check(!info.empty(), "truncated audio still returns diagnostic text");
        expectContains(info, "обрезан", type);
    }

    const std::string mp3Info = slade_mobile::audioInfoFor("MP3 Audio", tiny, 1);
    expectContains(mp3Info, "Не найден валидный MP3-фрейм", "MP3 reports missing frame");

    check(slade_mobile::audioInfoFor("Unknown", tiny, 1).empty(),
          "unknown type returns empty result");
    check(slade_mobile::audioInfoFor("WAV Sound", nullptr, 0).empty(),
          "null audio data returns empty result");
}

void testGenMidiZeroSize()
{
    const std::string info = slade_mobile::audioInfoFor(
            "GENMIDI Instruments", nullptr, 0);
    expectContains(info, "Размер: 0 байт", "GENMIDI accepts zero-size table");
}
}

int main()
{
    testWav();
    testDmx();
    testPcSpeaker();
    testMidi();
    testMus();
    testGenMidi();
    testFlac();
    testMp3();
    testOgg();
    testTruncatedInputs();
    testGenMidiZeroSize();

    std::cout << "native_audio_preview_tests: PASS\n";
    return 0;
}
