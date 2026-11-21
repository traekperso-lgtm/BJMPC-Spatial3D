#include "dsp.h"
#include <algorithm>
#include <cmath>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__)
#include <xmmintrin.h>
#define S3D_HAS_SSE 1
#endif

namespace s3d {

void enableFlushToZero() {
#ifdef S3D_HAS_SSE
    _mm_setcsr(_mm_getcsr() | 0x8040);   // FTZ (bit 15) + DAZ (bit 6)
#endif
}

void RoomReverb::init(double fs, double rt60, double predelayMs) {
    // Longueurs premières entre elles (ms) : densité d'échos sans coloration marquée
    static const double ms[N] = { 23.1, 27.7, 31.3, 36.7, 40.9, 45.3, 51.1, 57.7 };
    for (int i = 0; i < N; ++i) {
        const size_t len = std::max<size_t>(8, (size_t)(ms[i] * 1e-3 * fs));
        line_[i].assign(len, 0.f);
        pos_[i] = 0;
        fb_[i] = (float)std::pow(10.0, -3.0 * (ms[i] * 1e-3) / rt60);
        lp_[i] = 0.f;
    }
    pre_.assign(std::max<size_t>(1, (size_t)(predelayMs * 1e-3 * fs)), 0.f);
    prePos_ = 0;
}

void RoomReverb::process(const float* in, float* outLR, size_t frames, float wet) {
    if (wet <= 0.f || pre_.empty()) return;
    const float norm = 1.f / std::sqrt((float)N);
    for (size_t n = 0; n < frames; ++n) {
        const float x = pre_[prePos_];
        pre_[prePos_] = in[n];
        prePos_ = (prePos_ + 1) % pre_.size();

        float o[N];
        for (int i = 0; i < N; ++i) {
            float v = line_[i][pos_[i]];
            lp_[i] += (1.f - damp_) * (v - lp_[i]);   // amortissement des aigus
            o[i] = lp_[i];
        }
        // Hadamard 8x8 rapide
        float h[N]; std::copy(o, o + N, h);
        for (int len = 1; len < N; len <<= 1)
            for (int i = 0; i < N; i += len << 1)
                for (int j = i; j < i + len; ++j) { const float a = h[j], b = h[j + len]; h[j] = a + b; h[j + len] = a - b; }
        for (int i = 0; i < N; ++i) {
            line_[i][pos_[i]] = x + fb_[i] * h[i] * norm;
            pos_[i] = (pos_[i] + 1) % line_[i].size();
        }
        outLR[2 * n]     += wet * 0.5f * (o[0] + o[2] + o[4] + o[6]);
        outLR[2 * n + 1] += wet * 0.5f * (o[1] + o[3] + o[5] + o[7]);
    }
}

void Limiter::init(double fs, int channels, float thr, double releaseMs, double lookaheadMs) {
    ch_ = channels; thr_ = thr; env_ = 1.f; minEnv_ = 1.f;
    rel_ = (float)(1.0 - std::exp(-1.0 / (releaseMs * 1e-3 * fs)));
    L_ = std::max(1, (int)std::lround(lookaheadMs * 1e-3 * fs));
    delay_.assign((size_t)(L_ - 1) * ch_, 0.f);
    dpos_ = 0;
    minq_.clear(); minq_.reserve(4096 + (size_t)L_ + 16); minHead_ = 0;
    avg_.assign((size_t)L_, 1.f);
    apos_ = 0; avgSum_ = L_;
    n_ = 0;
    frame_.assign((size_t)ch_, 0.f);
}

void Limiter::process(float* x, size_t frames) {
    for (size_t f = 0; f < frames; ++f, ++n_) {
        float* fr = x + f * ch_;
        float pk = 0.f;
        for (int c = 0; c < ch_; ++c) pk = std::max(pk, std::fabs(fr[c]));
        const float req = pk > thr_ ? thr_ / pk : 1.f;
        // minimum glissant sur L_ trames (file monotone)
        while (minq_.size() > minHead_ && minq_.back().second >= req) minq_.pop_back();
        minq_.push_back({ n_, req });
        while (minq_[minHead_].first <= n_ - L_) ++minHead_;
        if (minHead_ > 4096) { minq_.erase(minq_.begin(), minq_.begin() + minHead_); minHead_ = 0; }
        const float m = minq_[minHead_].second;
        // moyenne glissante du minimum : rampe douce, toujours <= gain requis au moment de la crête
        avgSum_ += m - avg_[apos_];
        avg_[apos_] = m;
        apos_ = (apos_ + 1) % avg_.size();
        const float s = (float)std::min(1.0, avgSum_ / L_);
        if (s < env_) env_ = s; else env_ += (s - env_) * rel_;
        minEnv_ = std::min(minEnv_, env_);
        // sortie retardée de L_-1 trames
        if (L_ > 1) {
            float* d = &delay_[dpos_ * ch_];
            for (int c = 0; c < ch_; ++c) { const float in = fr[c]; fr[c] = d[c] * env_; d[c] = in; }
            dpos_ = (dpos_ + 1) % (size_t)(L_ - 1);
        } else {
            for (int c = 0; c < ch_; ++c) fr[c] *= env_;
        }
    }
}

float Limiter::reductionDb() const { return 20.f * std::log10(std::max(minEnv_, 1e-6f)); }

} // namespace s3d
