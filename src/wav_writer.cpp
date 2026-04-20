#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "wav_writer.h"
#include <cstdio>

bool WavWriter::open(const std::string& path)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (open_)
        return false;
    path_ = path;
    samples_.clear();
    open_ = true;
    return true;
}

void WavWriter::write_sample(float v)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!open_)
        return;
    samples_.push_back(v);
}

void WavWriter::close(float sample_rate)
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (!open_)
        return;
    open_ = false;

    drwav_data_format format;
    format.container     = drwav_container_riff;
    format.format        = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels      = 1;
    format.sampleRate    = (drwav_uint32)(sample_rate + 0.5f);
    format.bitsPerSample = 32;

    drwav wav;
    if (!drwav_init_file_write(&wav, path_.c_str(), &format, NULL))
    {
        std::printf("WAV: failed to open %s for writing\n", path_.c_str());
        samples_.clear();
        return;
    }

    drwav_write_pcm_frames(&wav, samples_.size(), samples_.data());
    drwav_uninit(&wav);

    std::printf("WAV: saved %zu samples @ %u Hz -> %s\n",
                samples_.size(), format.sampleRate, path_.c_str());
    samples_.clear();
}
