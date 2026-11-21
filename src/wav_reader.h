// Spatial3D — lecture de fichiers WAV (sans dépendance), pour la ligne de commande et les tests.
// PCM 8/16/24/32 bits, flottant 32/64 bits, WAVE_FORMAT_EXTENSIBLE (masque de canaux).
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace s3d {

class WavReader {
public:
    ~WavReader();
    bool open(const std::string& pathUtf8, std::string& err);
    int channels() const { return ch_; }
    int sampleRate() const { return rate_; }
    const std::vector<int>& channelIds() const { return ids_; }   // ChannelId par canal (-1 = inconnu)
    double durationSec() const { return rate_ ? (double)frames_ / rate_ : 0; }
    std::string info() const;
    // Lit jusqu'à `frames` trames en float entrelacés ; 0 à la fin
    size_t read(float* dst, size_t frames);
private:
    FILE* f_ = nullptr;
    int ch_ = 0, rate_ = 0, bits_ = 0, blockAlign_ = 0;
    bool float_ = false;
    uint32_t mask_ = 0;
    uint64_t frames_ = 0, pos_ = 0;
    std::vector<int> ids_;
    std::vector<uint8_t> raw_;
};

} // namespace s3d
