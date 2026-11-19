// Spatial3D — écriture WAV float 32 bits (WAVE_FORMAT_EXTENSIBLE avec masque de canaux)
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace s3d {

class WavWriter {
public:
    ~WavWriter() { close(); }
    bool open(const std::string& path, int channels, int rate, uint32_t channelMask);
    void write(const float* interleaved, size_t frames);
    void close();
private:
    FILE* f_ = nullptr;
    int ch_ = 0;
    uint64_t dataBytes_ = 0;
};

} // namespace s3d
