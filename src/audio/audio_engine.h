#pragma once
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include "soundio/soundio.h"

struct AudioDataSample {
    int64_t timestamp_us;
    float   value;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool init(int sample_rate = 44100);
    void shutdown();

    void push_sample(int64_t timestamp_us, float value);

    bool start_recording(const std::string& path);
    void stop_recording();
    bool is_recording() const;

    int sample_rate_ = 44100;

private:
    static void write_callback(SoundIoOutStream* outstream,
                               int frame_count_min, int frame_count_max);

    SoundIo*           soundio_   = nullptr;
    SoundIoDevice*     device_    = nullptr;
    SoundIoOutStream*  outstream_ = nullptr;
    SoundIoRingBuffer* ring_buf_  = nullptr;

    // interpolation state — write callback thread only, no lock needed
    AudioDataSample prev_sample_   = {0, 0.f};
    AudioDataSample next_sample_   = {0, 0.f};
    int64_t         audio_time_us_ = 0;
    bool            clock_init_    = false;

    // WAV
    std::FILE* wav_file_    = nullptr;
    uint32_t   wav_samples_ = 0;
    std::mutex wav_mutex_;

    void write_wav_header();
    void patch_wav_header();
};
