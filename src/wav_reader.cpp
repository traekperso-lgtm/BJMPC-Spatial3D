#include "wav_reader.h"
#include "layout.h"
#include <algorithm>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace s3d {

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Masque Windows par défaut quand le fichier n'en donne pas
static uint32_t defaultMask(int ch) {
    switch (ch) {
    case 1: return 0x4; case 2: return 0x3; case 3: return 0x7; case 4: return 0x33;
    case 5: return 0x607; case 6: return 0x60F; case 8: return 0x63F; case 10: return 0x5063F; case 12: return 0x2D63F;
    default: return 0;
    }
}

WavReader::~WavReader() { if (f_) fclose(f_); }

bool WavReader::open(const std::string& path, std::string& err) {
#ifdef _WIN32
    std::wstring w(MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], (int)w.size());
    f_ = _wfopen(w.c_str(), L"rb");
#else
    f_ = fopen(path.c_str(), "rb");
#endif
    if (!f_) { err = "Impossible d'ouvrir le fichier"; return false; }
    uint8_t h[12];
    if (fread(h, 1, 12, f_) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) {
        err = "Ce n'est pas un fichier WAV (Spatial3D lit le son de Windows en direct ; la ligne de commande accepte les .wav)";
        return false;
    }
    bool haveFmt = false;
    uint16_t tag = 0;
    for (;;) {
        uint8_t c[8];
        if (fread(c, 1, 8, f_) != 8) { err = "Fichier WAV incomplet (pas de donnees)"; return false; }
        const uint32_t size = rd32(c + 4);
        if (!memcmp(c, "fmt ", 4)) {
            std::vector<uint8_t> fmt(size);
            if (fread(fmt.data(), 1, size, f_) != size || size < 16) { err = "En-tete WAV invalide"; return false; }
            tag = rd16(&fmt[0]);
            ch_ = rd16(&fmt[2]);
            rate_ = (int)rd32(&fmt[4]);
            blockAlign_ = rd16(&fmt[12]);
            bits_ = rd16(&fmt[14]);
            if (tag == 0xFFFE && size >= 40) {
                mask_ = rd32(&fmt[20]);
                tag = rd16(&fmt[24]);          // 1 = PCM, 3 = flottant (début du GUID)
            }
            float_ = tag == 3;
            if (tag != 1 && tag != 3) { err = "Format WAV non gere (seuls PCM et flottant le sont)"; return false; }
            if (size & 1) fseek(f_, 1, SEEK_CUR);
            haveFmt = true;
        } else if (!memcmp(c, "data", 4)) {
            if (!haveFmt || ch_ <= 0 || blockAlign_ <= 0) { err = "En-tete WAV absent"; return false; }
            frames_ = size / blockAlign_;
            break;
        } else {
            fseek(f_, size + (size & 1), SEEK_CUR);
        }
    }
    int bitsSet = 0;
    for (uint32_t m = mask_; m; m &= m - 1) ++bitsSet;
    if (!mask_ || bitsSet != ch_) mask_ = defaultMask(ch_);
    ids_.clear();
    for (int b = 0; b < 32 && (int)ids_.size() < ch_; ++b) if (mask_ >> b & 1) ids_.push_back(b);
    while ((int)ids_.size() < ch_) ids_.push_back(-1);
    return true;
}

std::string WavReader::info() const {
    char b[160];
    snprintf(b, sizeof b, "WAV %s %d bits, %d canaux (%s), %d Hz", float_ ? "flottant" : "PCM", bits_, ch_,
             describeMask(mask_).c_str(), rate_);
    return b;
}

size_t WavReader::read(float* dst, size_t frames) {
    if (!f_ || pos_ >= frames_) return 0;
    frames = (size_t)std::min<uint64_t>(frames, frames_ - pos_);
    raw_.resize(frames * blockAlign_);
    const size_t got = fread(raw_.data(), blockAlign_, frames, f_);
    const int bytes = bits_ / 8;
    for (size_t i = 0; i < got * ch_; ++i) {
        const uint8_t* p = &raw_[i * bytes];
        float v = 0;
        if (float_) {
            if (bytes == 4) { float x; memcpy(&x, p, 4); v = x; }
            else if (bytes == 8) { double x; memcpy(&x, p, 8); v = (float)x; }
        } else if (bytes == 1) v = (p[0] - 128) / 128.f;
        else if (bytes == 2) v = (int16_t)rd16(p) / 32768.f;
        else if (bytes == 3) v = (int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) / 2147483648.f;
        else if (bytes == 4) v = (int32_t)rd32(p) / 2147483648.f;
        dst[i] = v;
    }
    pos_ += got;
    return got;
}

} // namespace s3d
