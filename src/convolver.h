// Spatial3D — convolution partitionnée uniforme (UPOLS) pour les HRTF mesurées.
// La réponse (souvent 256 à 1000 points) est découpée en tranches de B échantillons : la latence
// n'est plus que d'un bloc (128 éch. = 2,7 ms à 48 kHz) au lieu de la longueur de la réponse.
// Toutes les sources partagent une seule FFT inverse ; les deux oreilles aussi
// (gauche en partie réelle, droite en partie imaginaire).
#pragma once
#include <complex>
#include <vector>

namespace s3d {

class Fft {
public:
    void init(int n);
    void run(std::complex<float>* a, bool inverse) const;
    int size() const { return n_; }
private:
    int n_ = 0;
    std::vector<std::complex<float>> tw_;
    std::vector<int> rev_;
};

class PartitionedConvolver {
public:
    // hl[s] / hr[s] : réponses gauche/droite de la source s
    void init(const std::vector<std::vector<float>>& hl, const std::vector<std::vector<float>>& hr, int block = 128);
    int block() const { return B_; }
    int partitions() const { return P_; }
    // in[s] : B échantillons de la source s ; outLR : B trames stéréo (ajoutées)
    void processBlock(const float* const* in, float* outLR);
private:
    struct Source {
        std::vector<std::complex<float>> H;      // P spectres (HL + j HR), M points chacun
        std::vector<std::complex<float>> fdl;    // ligne à retard fréquentielle : P spectres d'entrée
        std::vector<float> hist;                 // 2B derniers échantillons
    };
    int B_ = 128, M_ = 256, P_ = 1, pos_ = 0;
    Fft fft_;
    std::vector<Source> src_;
    std::vector<std::complex<float>> X_, Y_;
};

} // namespace s3d
