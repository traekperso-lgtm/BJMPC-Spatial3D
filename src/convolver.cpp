#include "convolver.h"
#include <algorithm>
#include <cmath>

namespace s3d {

static constexpr double kPi = 3.14159265358979323846;

void Fft::init(int n) {
    n_ = n; tw_.resize(n / 2); rev_.resize(n);
    for (int i = 0; i < n / 2; ++i) tw_[i] = std::polar(1.f, (float)(-2 * kPi * i / n));
    int bits = 0; while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev_[i] = r; }
}

void Fft::run(std::complex<float>* a, bool inverse) const {
    for (int i = 0; i < n_; ++i) if (i < rev_[i]) std::swap(a[i], a[rev_[i]]);
    for (int len = 2; len <= n_; len <<= 1) {
        const int half = len / 2, step = n_ / len;
        for (int i = 0; i < n_; i += len)
            for (int j = 0; j < half; ++j) {
                std::complex<float> w = tw_[j * step];
                if (inverse) w = std::conj(w);
                const std::complex<float> u = a[i + j], v = a[i + j + half] * w;
                a[i + j] = u + v; a[i + j + half] = u - v;
            }
    }
    if (inverse) { const float k = 1.f / n_; for (int i = 0; i < n_; ++i) a[i] *= k; }
}

void PartitionedConvolver::init(const std::vector<std::vector<float>>& hl, const std::vector<std::vector<float>>& hr, int block) {
    B_ = block; M_ = 2 * block;
    size_t N = 1;
    for (auto& h : hl) N = std::max(N, h.size());
    for (auto& h : hr) N = std::max(N, h.size());
    P_ = (int)((N + B_ - 1) / B_);
    fft_.init(M_);
    src_.assign(hl.size(), {});
    std::vector<std::complex<float>> tmp(M_);
    for (size_t s = 0; s < hl.size(); ++s) {
        auto& S = src_[s];
        S.H.assign((size_t)P_ * M_, {});
        for (int p = 0; p < P_; ++p) {
            std::fill(tmp.begin(), tmp.end(), std::complex<float>(0, 0));
            for (int k = 0; k < B_; ++k) {
                const size_t i = (size_t)p * B_ + k;
                const float l = i < hl[s].size() ? hl[s][i] : 0.f;
                const float r = i < hr[s].size() ? hr[s][i] : 0.f;
                tmp[k] = { l, r };
            }
            // linéarité : FFT(hl + j·hr) = HL + j·HR, le spectre combiné voulu
            fft_.run(tmp.data(), false);
            std::copy(tmp.begin(), tmp.end(), S.H.begin() + (size_t)p * M_);
        }
        S.fdl.assign((size_t)P_ * M_, {});
        S.hist.assign(M_, 0.f);
    }
    X_.assign(M_, {});
    Y_.assign(M_, {});
    pos_ = 0;
}

void PartitionedConvolver::processBlock(const float* const* in, float* outLR) {
    std::fill(Y_.begin(), Y_.end(), std::complex<float>(0, 0));
    for (size_t s = 0; s < src_.size(); ++s) {
        auto& S = src_[s];
        // fenêtre glissante de 2B échantillons (overlap-save)
        std::copy(S.hist.begin() + B_, S.hist.end(), S.hist.begin());
        std::copy(in[s], in[s] + B_, S.hist.begin() + B_);
        for (int i = 0; i < M_; ++i) X_[i] = S.hist[i];
        fft_.run(X_.data(), false);
        std::copy(X_.begin(), X_.end(), S.fdl.begin() + (size_t)pos_ * M_);
        // somme sur les tranches : entrée retardée de p blocs × tranche p de la réponse
        for (int p = 0; p < P_; ++p) {
            const int slot = (pos_ - p + P_) % P_;
            const std::complex<float>* x = &S.fdl[(size_t)slot * M_];
            const std::complex<float>* h = &S.H[(size_t)p * M_];
            for (int k = 0; k < M_; ++k) Y_[k] += x[k] * h[k];
        }
    }
    fft_.run(Y_.data(), true);
    for (int n = 0; n < B_; ++n) {
        outLR[2 * n] += Y_[B_ + n].real();
        outLR[2 * n + 1] += Y_[B_ + n].imag();
    }
    pos_ = (pos_ + 1) % P_;
}

} // namespace s3d
