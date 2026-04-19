#include "audio_engine.h"
#include <chrono>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// WAV header layout (44 bytes, IEEE float32 mono)
// ---------------------------------------------------------------------------
static void write_u16le(std::FILE* f, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
    std::fwrite(b, 1, 2, f);
}

static void write_u32le(std::FILE* f, uint32_t v)
{
    uint8_t b[4] = {
        (uint8_t)(v & 0xFF),
        (uint8_t)((v >> 8)  & 0xFF),
        (uint8_t)((v >> 16) & 0xFF),
        (uint8_t)((v >> 24) & 0xFF)
    };
    std::fwrite(b, 1, 4, f);
}

void AudioEngine::write_wav_header()
{
    // RIFF chunk
    std::fwrite("RIFF", 1, 4, wav_file_);
    write_u32le(wav_file_, 0);           // file size - 8, patched on close
    std::fwrite("WAVE", 1, 4, wav_file_);
    // fmt chunk
    std::fwrite("fmt ", 1, 4, wav_file_);
    write_u32le(wav_file_, 16);          // chunk size
    write_u16le(wav_file_, 3);           // IEEE float
    write_u16le(wav_file_, 1);           // mono
    write_u32le(wav_file_, (uint32_t)sample_rate_);
    write_u32le(wav_file_, (uint32_t)sample_rate_ * 4); // byte rate
    write_u16le(wav_file_, 4);           // block align
    write_u16le(wav_file_, 32);          // bits per sample
    // data chunk
    std::fwrite("data", 1, 4, wav_file_);
    write_u32le(wav_file_, 0);           // data size, patched on close
}

void AudioEngine::patch_wav_header()
{
    uint32_t data_size = wav_samples_ * 4;
    uint32_t file_size_minus8 = 36 + data_size;

    std::fseek(wav_file_, 4, SEEK_SET);
    write_u32le(wav_file_, file_size_minus8);

    std::fseek(wav_file_, 40, SEEK_SET);
    write_u32le(wav_file_, data_size);
}

// ---------------------------------------------------------------------------
// AudioEngine
// ---------------------------------------------------------------------------
AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::init(int sample_rate)
{
    sample_rate_ = sample_rate;

    soundio_ = soundio_create();
    if (!soundio_)
    {
        std::printf("soundio_create failed\n");
        return false;
    }

    int err = soundio_connect(soundio_);
    if (err)
    {
        std::printf("soundio_connect: %s\n", soundio_strerror(err));
        return false;
    }
    soundio_flush_events(soundio_);

    int dev_idx = soundio_default_output_device_index(soundio_);
    if (dev_idx < 0)
    {
        std::printf("soundio: no output device found\n");
        return false;
    }

    device_ = soundio_get_output_device(soundio_, dev_idx);
    if (!device_)
    {
        std::printf("soundio: out of memory getting device\n");
        return false;
    }
    std::printf("Audio output device: %s\n", device_->name);

    outstream_ = soundio_outstream_create(device_);
    if (!outstream_)
    {
        std::printf("soundio: out of memory creating outstream\n");
        return false;
    }

    outstream_->format        = SoundIoFormatFloat32NE;
    outstream_->sample_rate   = sample_rate_;
    outstream_->layout        = *soundio_channel_layout_get_builtin(SoundIoChannelLayoutIdMono);
    outstream_->write_callback = write_callback;
    outstream_->userdata      = this;

    err = soundio_outstream_open(outstream_);
    if (err)
    {
        std::printf("soundio_outstream_open: %s\n", soundio_strerror(err));
        return false;
    }

    // Ring buffer: 2048 data-rate samples
    ring_buf_ = soundio_ring_buffer_create(soundio_, 2048 * (int)sizeof(AudioDataSample));
    if (!ring_buf_)
    {
        std::printf("soundio: ring buffer alloc failed\n");
        return false;
    }

    err = soundio_outstream_start(outstream_);
    if (err)
    {
        std::printf("soundio_outstream_start: %s\n", soundio_strerror(err));
        return false;
    }

    return true;
}

void AudioEngine::shutdown()
{
    stop_recording();

    if (outstream_)
    {
        soundio_outstream_destroy(outstream_);
        outstream_ = nullptr;
    }
    if (ring_buf_)
    {
        soundio_ring_buffer_destroy(ring_buf_);
        ring_buf_ = nullptr;
    }
    if (device_)
    {
        soundio_device_unref(device_);
        device_ = nullptr;
    }
    if (soundio_)
    {
        soundio_destroy(soundio_);
        soundio_ = nullptr;
    }
}

void AudioEngine::push_sample(int64_t timestamp_us, float value)
{
    if (!ring_buf_)
        return;
    if (soundio_ring_buffer_free_count(ring_buf_) < (int)sizeof(AudioDataSample))
        return;  // drop sample — ring buffer full
    AudioDataSample* dst = (AudioDataSample*)soundio_ring_buffer_write_ptr(ring_buf_);
    dst->timestamp_us = timestamp_us;
    dst->value        = value;
    soundio_ring_buffer_advance_write_ptr(ring_buf_, (int)sizeof(AudioDataSample));
}

bool AudioEngine::start_recording(const std::string& path)
{
    std::lock_guard<std::mutex> lk(wav_mutex_);
    if (wav_file_)
        return false;  // already recording
    wav_file_ = std::fopen(path.c_str(), "wb");
    if (!wav_file_)
    {
        std::printf("Audio: failed to open WAV file: %s\n", path.c_str());
        return false;
    }
    wav_samples_ = 0;
    write_wav_header();
    std::printf("Audio: recording to %s\n", path.c_str());
    return true;
}

void AudioEngine::stop_recording()
{
    std::lock_guard<std::mutex> lk(wav_mutex_);
    if (!wav_file_)
        return;
    patch_wav_header();
    std::fclose(wav_file_);
    wav_file_ = nullptr;
    std::printf("Audio: recording stopped (%u samples)\n", wav_samples_);
}

bool AudioEngine::is_recording() const
{
    // wav_mutex_ not needed for a simple pointer check in UI thread
    return wav_file_ != nullptr;
}

// ---------------------------------------------------------------------------
// Write callback — soundio thread
// ---------------------------------------------------------------------------
void AudioEngine::write_callback(SoundIoOutStream* outstream,
                                 int /*frame_count_min*/, int frame_count_max)
{
    AudioEngine* ae = (AudioEngine*)outstream->userdata;

    if (!ae->clock_init_)
    {
        ae->audio_time_us_ = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        ae->clock_init_ = true;
    }

    int64_t us_per_sample = 1000000LL / ae->sample_rate_;

    // Local batch buffer for WAV writes — avoids per-sample mutex overhead.
    // 8192 frames @ 44100 Hz = ~186 ms max, well above any reasonable callback size.
    float wav_buf[8192];
    int   wav_buf_count = 0;

    int frames_left = frame_count_max;
    while (frames_left > 0)
    {
        int frame_count = frames_left;
        SoundIoChannelArea* areas;
        int err = soundio_outstream_begin_write(outstream, &areas, &frame_count);
        if (err || frame_count == 0)
            break;

        for (int i = 0; i < frame_count; i++)
        {
            // Advance prev_sample_ through ring buffer: consume all samples whose
            // timestamp has already passed audio_time, keeping the most recent one.
            while (soundio_ring_buffer_fill_count(ae->ring_buf_) >= (int)sizeof(AudioDataSample))
            {
                AudioDataSample* peek = (AudioDataSample*)soundio_ring_buffer_read_ptr(ae->ring_buf_);
                if (peek->timestamp_us <= ae->audio_time_us_)
                {
                    ae->prev_sample_ = *peek;
                    soundio_ring_buffer_advance_read_ptr(ae->ring_buf_, (int)sizeof(AudioDataSample));
                }
                else
                {
                    ae->next_sample_ = *peek;  // peek without consuming
                    break;
                }
            }

            float sample;
            int64_t span = ae->next_sample_.timestamp_us - ae->prev_sample_.timestamp_us;
            if (span > 0)
            {
                float t = (float)(ae->audio_time_us_ - ae->prev_sample_.timestamp_us) / (float)span;
                if (t < 0.f) t = 0.f;
                if (t > 1.f) t = 1.f;
                sample = ae->prev_sample_.value + t * (ae->next_sample_.value - ae->prev_sample_.value);
            }
            else
            {
                sample = ae->prev_sample_.value;  // hold on underrun
            }

            *(float*)(areas[0].ptr + areas[0].step * i) = sample;
            ae->audio_time_us_ += us_per_sample;

            if (wav_buf_count < 8192)
                wav_buf[wav_buf_count++] = sample;
        }

        soundio_outstream_end_write(outstream);
        frames_left -= frame_count;
    }

    // Batch WAV write — one mutex acquisition per callback invocation
    if (wav_buf_count > 0)
    {
        std::lock_guard<std::mutex> lk(ae->wav_mutex_);
        if (ae->wav_file_)
        {
            std::fwrite(wav_buf, sizeof(float), wav_buf_count, ae->wav_file_);
            ae->wav_samples_ += wav_buf_count;
        }
    }
}
