#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include "mp4_writer.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace seed {
namespace {
using Microsoft::WRL::ComPtr;
void check(HRESULT result, const char* action) {
    if (FAILED(result)) {
        char code[32];
        std::snprintf(code, sizeof(code), " (0x%08lX)", static_cast<unsigned long>(result));
        throw std::runtime_error(std::string(action) + code);
    }
}
template <class T> T function(HMODULE module, const char* name) {
    auto p = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!p)
        throw std::runtime_error("Windows Media Foundation is unavailable.");
    return p;
}
// Resolve system APIs only when exporting. Ordinary play works without the
// optional Windows Media Feature Pack, and no encoder binaries are distributed.
struct MediaApi {
    HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    decltype(&MFStartup) startup = nullptr;
    decltype(&MFShutdown) shutdown = nullptr;
    decltype(&MFCreateMediaType) type = nullptr;
    decltype(&MFCreateAttributes) attributes = nullptr;
    decltype(&MFCreateSample) sample = nullptr;
    decltype(&MFCreateMemoryBuffer) buffer = nullptr;
    decltype(&MFCreateSinkWriterFromURL) writer = nullptr;
    bool com = false, started = false;
    MediaApi() {
        try {
            if (!plat || !rw)
                throw std::runtime_error(
                    "MP4 export needs Windows Media Foundation (Media Feature Pack on Windows N).");
            startup = function<decltype(startup)>(plat, "MFStartup");
            shutdown = function<decltype(shutdown)>(plat, "MFShutdown");
            type = function<decltype(type)>(plat, "MFCreateMediaType");
            attributes = function<decltype(attributes)>(plat, "MFCreateAttributes");
            sample = function<decltype(sample)>(plat, "MFCreateSample");
            buffer = function<decltype(buffer)>(plat, "MFCreateMemoryBuffer");
            writer = function<decltype(writer)>(rw, "MFCreateSinkWriterFromURL");
            const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (result != RPC_E_CHANGED_MODE) {
                check(result, "Cannot initialize video encoding");
                com = true;
            }
            check(startup(MF_VERSION, MFSTARTUP_FULL), "Cannot start Windows video encoding");
            started = true;
        } catch (...) {
            release();
            throw;
        }
    }
    void release() {
        if (started)
            shutdown();
        if (com)
            CoUninitialize();
        if (rw)
            FreeLibrary(rw);
        if (plat)
            FreeLibrary(plat);
        started = com = false;
        rw = plat = nullptr;
    }
    ~MediaApi() {
        release();
    }
};
std::uint8_t byte(int n) {
    return std::uint8_t(std::clamp(n, 0, 255));
}
} // namespace
struct Mp4Writer::Impl {
    MediaApi api; // Lives longer than all the COM objects below.
    ComPtr<IMFSinkWriter> writer;
    DWORD video_stream = 0, audio_stream = 0;
    unsigned width, height, source_rate = 0;
    std::uint64_t frames = 0, audio_written = 0, fraction = 0;
    std::array<std::int16_t, 2> previous{};
    std::vector<std::int16_t> pending;
    std::vector<std::uint8_t> nv12;
    bool finished = false;
    Impl(const std::filesystem::path& path, unsigned w, unsigned h) : width(w), height(h) {
        if (!((w == 240 && h == 160) || (w == 1280 && h == 720) || (w == 1920 && h == 1080)))
            throw std::runtime_error("Choose native, 720p or 1080p MP4 output.");
        nv12.resize(w * h * 3 / 2);
        ComPtr<IMFAttributes> attr;
        check(api.attributes(&attr, 2), "Cannot configure MP4 encoder");
        check(attr->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE),
              "Cannot configure export speed");
        check(api.writer(path.c_str(), nullptr, attr.Get(), &writer), "Cannot create MP4 file");
        ComPtr<IMFMediaType> output, input;
        check(api.type(&output), "Cannot create H.264 format");
        check(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Cannot configure video");
        check(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "H.264 encoding is unavailable");
        check(output->SetUINT32(MF_MT_AVG_BITRATE, w < 1000   ? 1500000
                                                   : h == 720 ? 8000000
                                                              : 14000000),
              "Cannot configure video quality");
        check(output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive),
              "Cannot configure progressive video");
        check(MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, w, h),
              "Cannot configure video size");
        check(MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, UINT32(kReplayClock),
                                  UINT32(kReplayFrameCycles)),
              "Cannot configure video timing");
        check(MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1),
              "Cannot configure aspect ratio");
        check(output->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601),
              "Cannot configure video color matrix");
        check(output->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235),
              "Cannot configure video color range");
        check(writer->AddStream(output.Get(), &video_stream), "Cannot add H.264 stream");
        check(api.type(&input), "Cannot configure raw video");
        check(output->CopyAllItems(input.Get()), "Cannot configure raw video");
        check(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12), "Cannot configure NV12 input");
        check(input->SetUINT32(MF_MT_DEFAULT_STRIDE, w), "Cannot configure video stride");
        check(writer->SetInputMediaType(video_stream, input.Get(), nullptr),
              "H.264 encoder cannot accept this size or frame rate");
        output.Reset();
        input.Reset();
        check(api.type(&output), "Cannot create AAC format");
        check(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Cannot configure audio");
        check(output->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC), "AAC encoding is unavailable");
        check(output->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2), "Cannot configure stereo audio");
        check(output->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000),
              "Cannot configure audio timing");
        check(output->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Cannot configure audio depth");
        check(output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 24000),
              "Cannot configure audio quality");
        check(writer->AddStream(output.Get(), &audio_stream), "Cannot add AAC stream");
        check(api.type(&input), "Cannot configure raw audio");
        check(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "Cannot configure PCM audio");
        check(input->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), "Cannot configure PCM audio");
        check(input->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2), "Cannot configure PCM channels");
        check(input->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000),
              "Cannot configure PCM timing");
        check(input->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "Cannot configure PCM depth");
        check(input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4), "Cannot configure PCM alignment");
        check(input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, 192000),
              "Cannot configure PCM bitrate");
        check(writer->SetInputMediaType(audio_stream, input.Get(), nullptr),
              "AAC encoder cannot accept stereo audio");
        check(writer->BeginWriting(), "Cannot begin MP4 encoding");
    }
    void sample(DWORD stream, const void* data, unsigned size, std::uint64_t start,
                std::uint64_t end) {
        ComPtr<IMFMediaBuffer> buffer;
        ComPtr<IMFSample> sample;
        check(api.buffer(size, &buffer), "Cannot allocate video/audio buffer");
        BYTE* dst = nullptr;
        check(buffer->Lock(&dst, nullptr, nullptr), "Cannot access video/audio buffer");
        std::memcpy(dst, data, size);
        buffer->Unlock();
        check(buffer->SetCurrentLength(size), "Cannot set sample size");
        check(api.sample(&sample), "Cannot create media sample");
        check(sample->AddBuffer(buffer.Get()), "Cannot add sample buffer");
        check(sample->SetSampleTime(start), "Cannot set sample time");
        check(sample->SetSampleDuration(end - start), "Cannot set sample duration");
        check(writer->WriteSample(stream, sample.Get()), "MP4 encoder could not write a sample");
    }
    void flush_audio(std::size_t pairs) {
        if (!pairs)
            return;
        sample(audio_stream, pending.data(), unsigned(pairs * 4), audio_written * 10000000 / 48000,
               (audio_written + pairs) * 10000000 / 48000);
        audio_written += pairs;
        pending.erase(pending.begin(), pending.begin() + pairs * 2);
    }
};
Mp4Writer::Mp4Writer(const std::filesystem::path& p, unsigned w, unsigned h)
    : impl_(std::make_unique<Impl>(p, w, h)) {}
Mp4Writer::~Mp4Writer() = default;
void Mp4Writer::video(const std::uint8_t* rgb) {
    auto& s = *impl_;
    if (s.finished || !rgb)
        throw std::runtime_error("Invalid MP4 video frame.");
    // Nearest-neighbour scaling and pillarboxing preserve the original 3:2 art.
    const unsigned game_w = s.height * 3 / 2, left = (s.width - game_w) / 2;
    auto pixel = [&](unsigned x, unsigned y) {
        if (x < left || x >= left + game_w)
            return std::array<int, 3>{0, 0, 0};
        const auto at = ((y * 160 / s.height) * 240 + (x - left) * 240 / game_w) * 3;
        return std::array<int, 3>{rgb[at], rgb[at + 1], rgb[at + 2]};
    };
    for (unsigned y = 0; y < s.height; y += 2)
        for (unsigned x = 0; x < s.width; x += 2) {
            int u = 0, v = 0;
            for (unsigned dy = 0; dy < 2; ++dy)
                for (unsigned dx = 0; dx < 2; ++dx) {
                    const auto c = pixel(x + dx, y + dy);
                    const auto r = c[0], g = c[1], b = c[2];
                    s.nv12[(y + dy) * s.width + x + dx] =
                        byte(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
                    u += ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                    v += ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                }
            const auto at = s.width * s.height + (y / 2) * s.width + x;
            s.nv12[at] = byte((u + 2) / 4);
            s.nv12[at + 1] = byte((v + 2) / 4);
        }
    s.sample(s.video_stream, s.nv12.data(), unsigned(s.nv12.size()), replay_video_time(s.frames),
             replay_video_time(s.frames + 1));
    ++s.frames;
}
void Mp4Writer::audio(std::span<const std::int16_t> input, unsigned rate) {
    auto& s = *impl_;
    if (s.finished || input.size() % 2 || rate < 8000 || rate > 192000)
        throw std::runtime_error("Invalid replay audio format.");
    if (s.source_rate && s.source_rate != rate)
        s.fraction = s.fraction * rate / s.source_rate;
    s.source_rate = rate;
    for (std::size_t i = 0; i < input.size(); i += 2) {
        s.fraction += 48000;
        while (s.fraction >= rate) {
            s.fraction -= rate;
            const auto weight = 48000 - std::min<std::uint64_t>(s.fraction, 48000);
            for (unsigned c = 0; c < 2; ++c)
                s.pending.push_back(std::int16_t((std::int64_t(s.previous[c]) * (48000 - weight) +
                                                  std::int64_t(input[i + c]) * weight) /
                                                 48000));
        }
        s.previous = {input[i], input[i + 1]};
    }
    // Keep a short tail so the final native audio quantum can be trimmed or padded.
    if (s.pending.size() / 2 > 4800 + 2048)
        s.flush_audio(s.pending.size() / 2 - 4800);
}
void Mp4Writer::finish() {
    auto& s = *impl_;
    if (s.finished || !s.frames)
        throw std::runtime_error("No video frames were exported.");
    const auto target = s.frames * kReplayFrameCycles * 48000 / kReplayClock;
    if (s.audio_written > target)
        throw std::runtime_error("Replay audio exceeded its video timeline.");
    if (s.pending.size() / 2 > target - s.audio_written)
        s.pending.resize(std::size_t(target - s.audio_written) * 2);
    s.flush_audio(s.pending.size() / 2);
    while (s.audio_written < target) {
        s.pending.assign(std::size_t(std::min<std::uint64_t>(2048, target - s.audio_written)) * 2,
                         0);
        s.flush_audio(s.pending.size() / 2);
    }
    check(s.writer->Finalize(), "Cannot finalize MP4 file (check free disk space)");
    s.finished = true;
}
} // namespace seed
