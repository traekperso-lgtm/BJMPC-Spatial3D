#include "wav_writer.h"
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace s3d {

static void put16(FILE* f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }
static void put32(FILE* f, uint32_t v) { for (int i = 0; i < 4; ++i) fputc((v >> (8 * i)) & 0xFF, f); }

bool WavWriter::open(const std::string& path, int channels, int rate, uint32_t mask) {
#ifdef _WIN32
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], (int)w.size());
    f_ = _wfopen(w.c_str(), L"wb");
#else
    f_ = fopen(path.c_str(), "wb");
#endif
    if (!f_) return false;
    ch_ = channels; dataBytes_ = 0;
    fwrite("RIFF", 1, 4, f_); put32(f_, 0); fwrite("WAVE", 1, 4, f_);
    fwrite("fmt ", 1, 4, f_); put32(f_, 40);
    put16(f_, 0xFFFE);                       // WAVE_FORMAT_EXTENSIBLE
    put16(f_, (uint16_t)channels);
    put32(f_, (uint32_t)rate);
    put32(f_, (uint32_t)rate * channels * 4);
    put16(f_, (uint16_t)(channels * 4));
    put16(f_, 32);
    put16(f_, 22);
    put16(f_, 32);                           // bits valides
    put32(f_, mask);
    static const uint8_t floatGuid[16] = { 0x03,0,0,0, 0,0, 0x10,0, 0x80,0, 0,0xAA,0,0x38,0x9B,0x71 };
    fwrite(floatGuid, 1, 16, f_);
    fwrite("data", 1, 4, f_); put32(f_, 0);
    return true;
}

void WavWriter::write(const float* x, size_t frames) {
    if (!f_) return;
    fwrite(x, sizeof(float), frames * ch_, f_);   // little-endian (x86/ARM)
    dataBytes_ += frames * ch_ * sizeof(float);
}

void WavWriter::close() {
    if (!f_) return;
    const uint32_t data = (uint32_t)std::min<uint64_t>(dataBytes_, 0xFFFFFFFFull - 68);
    fseek(f_, 4, SEEK_SET); put32(f_, 60 + data);
    fseek(f_, 64, SEEK_SET); put32(f_, data);
    fclose(f_); f_ = nullptr;
}

} // namespace s3d
