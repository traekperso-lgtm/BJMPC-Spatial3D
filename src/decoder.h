// Spatial3D — lecture du fichier source (FFmpeg : MP3, FLAC, WAV, AAC, AC-3, E-AC-3, DTS, TrueHD, Opus, MKV, MP4…)
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace s3d {

class Decoder {
public:
    Decoder();
    ~Decoder();
    // outRate = 0 : fréquence d'origine ; sinon rééchantillonnage
    bool open(const std::string& pathUtf8, int outRate, std::string& err);
    int channels() const;
    int sampleRate() const;                       // fréquence de sortie du décodeur
    const std::vector<int>& channelIds() const;   // ChannelId par canal (-1 = inconnu)
    bool ambisonic() const;                       // le fichier se déclare Ambisonique
    double durationSec() const;
    std::string info() const;
    // Lit jusqu'à `frames` trames (float entrelacés). Retourne 0 à la fin.
    size_t read(float* dst, size_t frames);
    // Repositionne la lecture (secondes). À appeler depuis le fil qui lit.
    bool seek(double seconds);
private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace s3d
