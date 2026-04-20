#pragma once
#include <mutex>
#include <string>
#include <vector>

class WavWriter {
public:
    bool open(const std::string& path);
    void write_sample(float v);
    void close(float sample_rate);
    bool is_open() const { return open_; }

private:
    std::string        path_;
    std::vector<float> samples_;
    bool               open_ = false;
    std::mutex         mutex_;
};
