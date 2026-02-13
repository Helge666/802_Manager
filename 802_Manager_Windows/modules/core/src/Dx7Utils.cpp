#include "core/Dx7Utils.h"

using namespace core;

namespace {
constexpr juce::uint8 kF0 = 0xF0;
constexpr juce::uint8 kF7 = 0xF7;
constexpr juce::uint8 kYamahaId = 0x43;
constexpr int kSingleSysexLength = 163;
constexpr int kVoiceDataLength = 155;
constexpr int kPackedVoiceLength = 128;
constexpr int kBankLength = 4104;
}

juce::uint8 Dx7Utils::calculateChecksum(const juce::MemoryBlock& data7bit)
{
    const juce::uint8* bytes = static_cast<const juce::uint8*>(data7bit.getData());
    const size_t size = data7bit.getSize();
    juce::uint32 sum = 0;
    for (size_t i = 0; i < size; ++i)
        sum += (bytes[i] & 0x7F);
    return static_cast<juce::uint8>((128 - (sum & 0x7F)) & 0x7F);
}

bool Dx7Utils::verifySingleVoiceSysex(const juce::MemoryBlock& sysex163,
                                      juce::String& message,
                                      juce::MemoryBlock& voiceData155)
{
    if ((int) sysex163.getSize() != kSingleSysexLength)
    {
        message = juce::String("Invalid SysEx length: got ") + juce::String((int)sysex163.getSize()) + ", expected 163";
        return false;
    }
    const juce::uint8* b = static_cast<const juce::uint8*>(sysex163.getData());
    if (b[0] != kF0 || b[kSingleSysexLength - 1] != kF7)
    {
        message = "Invalid start/end markers";
        return false;
    }
    const juce::uint8 expectedHeader[6] { kF0, kYamahaId, 0x00, 0x00, 0x01, 0x1B };
    if (!(b[0] == expectedHeader[0] && b[1] == expectedHeader[1] && b[3] == expectedHeader[3] && b[4] == expectedHeader[4] && b[5] == expectedHeader[5]))
    {
        if (b[0] != kF0 || b[1] != kYamahaId || !(b[3] == 0x00 && b[4] == 0x01 && b[5] == 0x1B))
        {
            message = "Invalid header";
            return false;
        }
    }

    const juce::uint8 fileChecksum = b[kSingleSysexLength - 2];
    voiceData155.reset();
    voiceData155.append(b + 6, (size_t) kVoiceDataLength);
    if ((int) voiceData155.getSize() != kVoiceDataLength)
    {
        message = "Internal error: Extracted voice data length != 155";
        return false;
    }
    const auto calc = calculateChecksum(voiceData155);
    if (calc != fileChecksum)
    {
        message = juce::String::formatted("Checksum mismatch: file 0x%02X, calc 0x%02X", fileChecksum, calc);
        return false;
    }
    message = "Valid single voice SysEx";
    return true;
}

bool Dx7Utils::isValidDx7Bank(const juce::MemoryBlock& bankData, juce::String& message)
{
    if ((int) bankData.getSize() != kBankLength)
    {
        message = juce::String("Invalid bank file size: got ") + juce::String((int)bankData.getSize()) + ", expected 4104";
        return false;
    }
    const juce::uint8* b = static_cast<const juce::uint8*>(bankData.getData());
    if (!(b[0] == kF0 && b[1] == kYamahaId && b[3] == 0x09 && b[4] == 0x20 && b[5] == 0x00))
    {
        if (!(b[0] == kF0 && b[1] == kYamahaId && (b[2] <= 0x0F) && b[3] == 0x09 && b[4] == 0x20 && b[5] == 0x00))
        {
            message = "Invalid header bytes for bank";
            return false;
        }
    }
    if (b[kBankLength - 1] != kF7)
    {
        message = "Invalid end marker";
        return false;
    }
    juce::MemoryBlock data;
    data.append(b + 6, (size_t) 4096);
    if ((int) data.getSize() != 4096)
    {
        message = "Unexpected data length for checksum";
        return false;
    }
    const juce::uint8 fileChecksum = b[4102];
    const auto calc = calculateChecksum(data);
    if (calc != fileChecksum)
    {
        message = juce::String::formatted("Bank checksum mismatch: file=0x%02X, calculated=0x%02X", fileChecksum, calc);
        return false;
    }
    message = "Valid DX7 32-voice bank file";
    return true;
}

juce::String Dx7Utils::extractPresetNameFromSysex(const juce::MemoryBlock& sysex163)
{
    if ((int) sysex163.getSize() != kSingleSysexLength)
        return "Unknown";
    const juce::uint8* b = static_cast<const juce::uint8*>(sysex163.getData());
    const int nameStart = 6 + 145;
    const int nameEnd = nameStart + 10;
    juce::String name;
    for (int i = nameStart; i < nameEnd; ++i)
        name += juce::String::charToString((juce::juce_wchar) (char) b[i]);
    name = name.trim();
    return name.isNotEmpty() ? name : juce::String("Unnamed");
}

juce::String Dx7Utils::extractPresetNameFromUnpacked(const juce::MemoryBlock& voiceData155)
{
    if ((int) voiceData155.getSize() != kVoiceDataLength)
        return "Unnamed";
    const juce::uint8* v = static_cast<const juce::uint8*>(voiceData155.getData());
    juce::String name;
    for (int i = 145; i < 155; ++i)
        name += juce::String::charToString((juce::juce_wchar) (char) v[i]);
    name = name.trim();
    return name.isNotEmpty() ? name : juce::String("Unnamed");
}

static inline std::pair<int, int> unpackByte11(juce::uint8 packed)
{
    const int rightCurve = (packed >> 2) & 0x03;
    const int leftCurve = packed & 0x03;
    return { leftCurve, rightCurve };
}

static inline std::pair<int, int> unpackByte12(juce::uint8 packed)
{
    const int detune = (packed >> 3) & 0x0F;
    const int rateScale = packed & 0x07;
    return { detune, rateScale };
}

static juce::MemoryBlock unpackOperatorBytes(const juce::uint8* packed17)
{
    juce::MemoryBlock params(21);
    auto* out = static_cast<juce::uint8*>(params.getData());
    std::memcpy(out, packed17, 11);
    auto curves = unpackByte11(packed17[11]);
    out[11] = (juce::uint8) curves.first;   // LCurve
    out[12] = (juce::uint8) curves.second;  // RCurve
    auto dRs = unpackByte12(packed17[12]);
    out[13] = (juce::uint8) dRs.second;     // Rate Scale
    out[14] = (juce::uint8) (packed17[13] & 0x03);            // AMS
    out[15] = (juce::uint8) ((packed17[13] >> 3) & 0x07);     // KVS
    out[16] = packed17[14];                              // Output Level
    out[17] = (juce::uint8) (packed17[15] & 0x01);            // Mode
    out[18] = (juce::uint8) ((packed17[15] >> 1) & 0x1F);     // Freq Coarse
    out[19] = packed17[16];                              // Freq Fine
    out[20] = (juce::uint8) dRs.first;                         // Detune
    return params;
}

juce::MemoryBlock Dx7Utils::unpackBankVoiceToSingle(const juce::MemoryBlock& packed128)
{
    jassert((int) packed128.getSize() == kPackedVoiceLength);
    if ((int) packed128.getSize() != kPackedVoiceLength)
        return {};

    juce::MemoryBlock unpacked(kVoiceDataLength);
    auto* out = static_cast<juce::uint8*>(unpacked.getData());
    const juce::uint8* in = static_cast<const juce::uint8*>(packed128.getData());
    int currentIndex = 0;

    for (int opIdx = 0; opIdx < 6; ++opIdx)
    {
        const int startPacked = opIdx * 17;
        const juce::MemoryBlock opParams = unpackOperatorBytes(in + startPacked);
        std::memcpy(out + currentIndex, opParams.getData(), 21);
        currentIndex += 21;
    }

    // Pitch EG 8 bytes (packed 102-109)
    std::memcpy(out + currentIndex, in + 102, 8);
    currentIndex += 8;

    // Algorithm (110)
    out[currentIndex++] = in[110] & 0x1F;

    // Feedback/Sync (111)
    const juce::uint8 pb111 = in[111];
    out[currentIndex++] = pb111 & 0x07;           // FB
    out[currentIndex++] = (pb111 >> 3) & 0x01;    // OSync

    // LFO (112-116)
    out[currentIndex++] = in[112]; // Speed
    out[currentIndex++] = in[113]; // Delay
    out[currentIndex++] = in[114]; // PMD
    out[currentIndex++] = in[115]; // AMD
    const juce::uint8 pb116 = in[116];
    out[currentIndex++] = pb116 & 0x01;                 // Sync
    out[currentIndex++] = (pb116 >> 1) & 0x07;          // Wave
    out[currentIndex++] = (pb116 >> 4) & 0x07;          // PMS

    // Transpose (117)
    out[currentIndex++] = in[117] & 0x3F;

    // Name (118-127)
    std::memcpy(out + currentIndex, in + 118, 10);
    currentIndex += 10;

    jassert(currentIndex == kVoiceDataLength);
    return unpacked;
}

juce::MemoryBlock Dx7Utils::packSingleToBankVoice(const juce::MemoryBlock& voiceData155)
{
    jassert((int) voiceData155.getSize() == kVoiceDataLength);
    if ((int) voiceData155.getSize() != kVoiceDataLength)
        return {};

    juce::MemoryBlock packed(kPackedVoiceLength);
    auto* out = static_cast<juce::uint8*>(packed.getData());
    const juce::uint8* v = static_cast<const juce::uint8*>(voiceData155.getData());

    const int singleOpLen = 21;
    const int packedOpLen = 17;

    for (int opIdx = 0; opIdx < 6; ++opIdx)
    {
        const int singleStart = opIdx * singleOpLen;
        const int packedStart = opIdx * packedOpLen;

        std::memcpy(out + packedStart, v + singleStart, 11); // EG R/L

        const juce::uint8 leftCurve = v[singleStart + 11] & 0x03;
        const juce::uint8 rightCurve = v[singleStart + 12] & 0x03;
        out[packedStart + 11] = (juce::uint8) ((rightCurve << 2) | leftCurve);

        const juce::uint8 detune = v[singleStart + 20] & 0x0F;
        const juce::uint8 rateScale = v[singleStart + 13] & 0x07;
        out[packedStart + 12] = (juce::uint8) ((detune << 3) | rateScale);

        const juce::uint8 kvs = v[singleStart + 15] & 0x07;
        const juce::uint8 ams = v[singleStart + 14] & 0x03;
        out[packedStart + 13] = (juce::uint8) ((kvs << 3) | ams);

        out[packedStart + 14] = v[singleStart + 16];

        const juce::uint8 mode = v[singleStart + 17] & 0x01;
        const juce::uint8 freqCoarse = v[singleStart + 18] & 0x1F;
        out[packedStart + 15] = (juce::uint8) ((freqCoarse << 1) | mode);

        out[packedStart + 16] = v[singleStart + 19];
    }

    int packedOffset = 6 * packedOpLen; // 102
    std::memcpy(out + packedOffset, v + 126, 8); // Pitch EG
    packedOffset += 8;

    const juce::uint8 alg = v[134] & 0x1F;
    const juce::uint8 feedback = v[135] & 0x07;
    const juce::uint8 oscSync = v[136] & 0x01;
    out[packedOffset] = alg;
    out[packedOffset + 1] = (juce::uint8) ((oscSync << 3) | feedback);
    packedOffset += 2;

    const juce::uint8 lfoSpd = v[137];
    const juce::uint8 lfoDel = v[138];
    const juce::uint8 lfoPmd = v[139];
    const juce::uint8 lfoAmd = v[140];
    const juce::uint8 lfoSync = v[141] & 0x01;
    const juce::uint8 lfoWave = v[142] & 0x07;
    const juce::uint8 lfoPms = v[143] & 0x07;
    out[packedOffset + 0] = lfoSpd;
    out[packedOffset + 1] = lfoDel;
    out[packedOffset + 2] = lfoPmd;
    out[packedOffset + 3] = lfoAmd;
    out[packedOffset + 4] = (juce::uint8) ((lfoPms << 4) | (lfoWave << 1) | lfoSync);
    packedOffset += 5;

    out[packedOffset++] = v[144]; // Transpose

    std::memcpy(out + packedOffset, v + 145, 10); // Name

    return packed;
}

juce::MemoryBlock Dx7Utils::createSinglePresetSysex(const juce::MemoryBlock& packed128)
{
    juce::MemoryBlock v155 = unpackBankVoiceToSingle(packed128);
    if ((int) v155.getSize() != kVoiceDataLength)
        return {};
    juce::MemoryBlock syx(kSingleSysexLength);
    auto* out = static_cast<juce::uint8*>(syx.getData());
    out[0] = kF0; out[1] = kYamahaId; out[2] = 0x00; out[3] = 0x00; out[4] = 0x01; out[5] = 0x1B;
    std::memcpy(out + 6, v155.getData(), (size_t) kVoiceDataLength);
    out[161] = calculateChecksum(v155);
    out[162] = kF7;
    return syx;
}
