#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include "mp4_writer.h"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void hr(HRESULT result, const char* message) {
    if (FAILED(result))
        throw std::runtime_error(std::string(message) + " HRESULT " +
                                 std::to_string(unsigned(result)));
}
int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 3,
                "Pass an E: output folder and optionally an MP4 to inspect");
        const bool inspect = argc == 3;
        const auto root = std::filesystem::absolute(argv[1]);
        std::filesystem::create_directories(root);
        std::array<std::uint8_t, 240 * 160 * 3> pixels{};
        for (unsigned p = 0; p < 240 * 160; ++p) {
            pixels[p * 3] = 220;
            pixels[p * 3 + 1] = 40;
            pixels[p * 3 + 2] = 70;
        }
        std::ofstream report(root / "decode.csv");
        report << "height,frames,audio_pairs,duration_100ns\n";
        for (auto height : {160u, 720u, 1080u}) {
            if (inspect && height != 160)
                continue;
            const auto path = inspect ? std::filesystem::absolute(argv[2])
                                      : root / (std::to_string(height) + "p.mp4");
            unsigned count = height == 160 ? 120 : 12;
            if (!inspect) {
                seed::Mp4Writer writer(path,
                                       height == 160   ? 240
                                       : height == 720 ? 1280
                                                       : 1920,
                                       height);
                std::uint64_t source = 0;
                for (unsigned frame = 0; frame < count; ++frame) {
                    writer.video(pixels.data());
                    const auto end = (std::uint64_t(frame) + 1) * seed::kReplayFrameCycles * 65536 /
                                     seed::kReplayClock;
                    std::vector<std::int16_t> audio;
                    for (; source < end; ++source) {
                        const auto value = std::int16_t(
                            8000 * std::sin(double(source) * 6.283185307179586 * 440 / 65536));
                        audio.push_back(value);
                        audio.push_back(value);
                    }
                    writer.audio(audio, 65536);
                }
                writer.finish();
            }
            hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "COM");
            auto plat = LoadLibraryW(L"mfplat.dll"), rw = LoadLibraryW(L"mfreadwrite.dll");
            require(plat && rw, "Media Foundation decoder missing");
            const auto startup =
                reinterpret_cast<decltype(&MFStartup)>(GetProcAddress(plat, "MFStartup"));
            const auto shutdown =
                reinterpret_cast<decltype(&MFShutdown)>(GetProcAddress(plat, "MFShutdown"));
            const auto type = reinterpret_cast<decltype(&MFCreateMediaType)>(
                GetProcAddress(plat, "MFCreateMediaType"));
            const auto attributes = reinterpret_cast<decltype(&MFCreateAttributes)>(
                GetProcAddress(plat, "MFCreateAttributes"));
            const auto create = reinterpret_cast<decltype(&MFCreateSourceReaderFromURL)>(
                GetProcAddress(rw, "MFCreateSourceReaderFromURL"));
            hr(startup(MF_VERSION, MFSTARTUP_FULL), "MFStartup");
            unsigned decoded = 0;
            std::uint64_t audio_pairs = 0;
            LONGLONG last = -1, duration = 0;
            {
                ComPtr<IMFAttributes> attr;
                hr(attributes(&attr, 1), "Decoder attributes");
                hr(attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE),
                   "Video conversion");
                ComPtr<IMFSourceReader> reader;
                hr(create(path.c_str(), attr.Get(), &reader), "Open MP4 video");
                ComPtr<IMFMediaType> native;
                hr(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &native),
                   "Video metadata");
                UINT32 width = 0, h = 0;
                hr(MFGetAttributeSize(native.Get(), MF_MT_FRAME_SIZE, &width, &h),
                   "Video dimensions");
                if (!inspect)
                    require(h == height && width == (height == 160   ? 240
                                                     : height == 720 ? 1280
                                                                     : 1920),
                            "MP4 dimensions differ");
                ComPtr<IMFMediaType> raw;
                hr(type(&raw), "Raw video type");
                raw->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                raw->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                hr(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE),
                   "Deselect streams");
                hr(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE),
                   "Select video");
                hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr,
                                               raw.Get()),
                   "Decode H.264 to RGB");
                for (;;) {
                    DWORD flags = 0;
                    LONGLONG time = 0;
                    ComPtr<IMFSample> sample;
                    hr(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags,
                                          &time, &sample),
                       "Decode video frame");
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                        break;
                    if (!sample)
                        continue;
                    require(time > last, "Non-monotonic video timestamps");
                    last = time;
                    LONGLONG length = 0;
                    sample->GetSampleDuration(&length);
                    duration = time + length;
                    if ((!inspect && !decoded) || (inspect && decoded == 369)) {
                        ComPtr<IMFMediaBuffer> buffer;
                        hr(sample->ConvertToContiguousBuffer(&buffer), "Decode RGB bytes");
                        BYTE* bytes = nullptr;
                        DWORD size = 0;
                        hr(buffer->Lock(&bytes, nullptr, &size), "Read RGB frame");
                        const auto at = (h / 2 * width + width / 2) * 4;
                        if (!inspect) {
                            require(size >= at + 4 && bytes[at + 2] > 180 && bytes[at + 1] < 90 &&
                                        bytes[at] < 110,
                                    "Video colors or pixel layout are wrong");
                        } else {
                            std::ofstream image(root / "frame.rgb", std::ios::binary);
                            const std::uint32_t dimensions[]{width, h};
                            image.write(reinterpret_cast<const char*>(dimensions),
                                        sizeof(dimensions));
                            for (unsigned y = 0; y < h; ++y)
                                for (unsigned x = 0; x < width; ++x) {
                                    const auto offset = (y * width + x) * 4;
                                    const char rgb[]{char(bytes[offset + 2]),
                                                     char(bytes[offset + 1]), char(bytes[offset])};
                                    image.write(rgb, 3);
                                }
                            height = h;
                        }
                        buffer->Unlock();
                    }
                    ++decoded;
                }
            }
            if (inspect)
                count = decoded;
            require(decoded == count, "MP4 decoder lost or duplicated video frames");
            require(std::llabs(duration - LONGLONG(seed::replay_video_time(count))) < 20000,
                    "Video duration drifted");
            {
                ComPtr<IMFSourceReader> reader;
                hr(create(path.c_str(), nullptr, &reader), "Open MP4 audio");
                ComPtr<IMFMediaType> raw;
                hr(type(&raw), "Raw audio type");
                raw->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
                raw->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
                raw->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
                raw->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, 48000);
                raw->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
                reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
                reader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
                hr(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr,
                                               raw.Get()),
                   "Decode AAC to PCM");
                std::uint64_t energy = 0;
                for (;;) {
                    DWORD flags = 0;
                    ComPtr<IMFSample> sample;
                    hr(reader->ReadSample(MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags,
                                          nullptr, &sample),
                       "Decode AAC packet");
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                        break;
                    if (!sample)
                        continue;
                    ComPtr<IMFMediaBuffer> buffer;
                    hr(sample->ConvertToContiguousBuffer(&buffer), "Decode audio bytes");
                    BYTE* bytes = nullptr;
                    DWORD size = 0;
                    hr(buffer->Lock(&bytes, nullptr, &size), "Read PCM bytes");
                    const auto* pcm = reinterpret_cast<const std::int16_t*>(bytes);
                    for (unsigned n = 0; n < size / 2; ++n)
                        energy += std::uint64_t(std::int64_t(pcm[n]) * pcm[n]);
                    audio_pairs += size / 4;
                    buffer->Unlock();
                }
                const auto wanted =
                    std::uint64_t(count) * seed::kReplayFrameCycles * 48000 / seed::kReplayClock;
                require(audio_pairs >= wanted && audio_pairs - wanted <= 2048,
                        "AAC duration differs from video");
                require(energy > (inspect ? 0 : audio_pairs * 1000000),
                        "MP4 audio is missing or silent");
            }
            shutdown();
            FreeLibrary(rw);
            FreeLibrary(plat);
            CoUninitialize();
            report << height << ',' << decoded << ',' << audio_pairs << ',' << duration << '\n';
            std::cout << "PASS decoded H.264/AAC " << height << "p: " << decoded << " frames\n"
                      << std::flush;
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
