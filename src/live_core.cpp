#include "live_core.h"
#include <algorithm>
#include <cmath>

namespace s3d {

void SpscRing::init(size_t capacity) {
    size_t n = 1; while (n < capacity) n <<= 1;
    buf_.assign(n, 0.f);
    mask_ = n - 1;
    head_ = 0; tail_ = 0;
}

size_t SpscRing::available() const { return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire); }

size_t SpscRing::push(const float* x, size_t n) {
    const size_t h = head_.load(std::memory_order_relaxed), t = tail_.load(std::memory_order_acquire);
    n = std::min(n, buf_.size() - (h - t));
    for (size_t i = 0; i < n; ++i) buf_[(h + i) & mask_] = x[i];
    head_.store(h + n, std::memory_order_release);
    return n;
}

size_t SpscRing::pop(float* dst, size_t max) {
    const size_t t = tail_.load(std::memory_order_relaxed), h = head_.load(std::memory_order_acquire);
    const size_t n = std::min(max, h - t);
    for (size_t i = 0; i < n; ++i) dst[i] = buf_[(t + i) & mask_];
    tail_.store(t + n, std::memory_order_release);
    return n;
}

static double besselI0(double x) {
    double sum = 1, t = 1;
    for (int k = 1; k < 40; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); sum += t; if (t < 1e-12 * sum) break; }
    return sum;
}

void AdaptiveResampler::buildTable(double cutoff) {
    const int N = 2 * kHalf;
    const double beta = 7.0, i0b = besselI0(beta);
    tab_.assign((size_t)(kPhases + 1) * N, 0.f);
    for (int p = 0; p <= kPhases; ++p) {
        const double mu = (double)p / kPhases;          // position fractionnaire
        double sum = 0;
        float* row = &tab_[(size_t)p * N];
        for (int k = 0; k < N; ++k) {
            const double d = (k - (kHalf - 1)) - mu;     // distance au point voulu
            const double x = d * cutoff;
            const double sinc = std::fabs(x) < 1e-12 ? 1.0 : std::sin(3.14159265358979323846 * x) / (3.14159265358979323846 * x);
            const double r = d / kHalf;
            const double w = std::fabs(r) >= 1 ? 0.0 : besselI0(beta * std::sqrt(1 - r * r)) / i0b;
            row[k] = (float)(cutoff * sinc * w);
            sum += row[k];
        }
        for (int k = 0; k < N; ++k) row[k] = (float)(row[k] / sum);   // gain continu exactement 1
    }
}

void AdaptiveResampler::setNominalRatio(double ratio) {
    // descente de fréquence : coupure sous la nouvelle fréquence de Nyquist
    buildTable(0.915 * std::min(1.0, 1.0 / std::max(1e-3, ratio)));
}

void AdaptiveResampler::init(int channels, size_t capacityFrames) {
    ch_ = channels;
    buf_.reserve(capacityFrames * channels);   // les ajouts suivants ne réallouent pas
    coef_.assign(2 * kHalf, 0.f);
    if (tab_.empty()) buildTable(0.915);
    reset();
}

void AdaptiveResampler::reset() {
    buf_.assign((size_t)(kHalf - 1) * ch_, 0.f);   // historique nul pour le filtre
    pos_ = kHalf - 1;
}

void AdaptiveResampler::push(const std::vector<float>& x) { buf_.insert(buf_.end(), x.begin(), x.end()); }
void AdaptiveResampler::push(const float* x, size_t frames) { buf_.insert(buf_.end(), x, x + frames * ch_); }

double AdaptiveResampler::available() const {
    const double frames = (double)(buf_.size() / ch_);
    return std::max(0.0, frames - kHalf - pos_);
}

size_t AdaptiveResampler::pull(float* out, size_t frames, double ratio) {
    const size_t nIn = buf_.size() / ch_;
    const int N = 2 * kHalf;
    size_t n = 0;
    for (; n < frames; ++n) {
        const size_t i = (size_t)pos_;
        if (i + kHalf >= nIn) break;                     // plus assez d'entrée
        const double fp = (pos_ - (double)i) * kPhases;
        const int p = std::min((int)fp, kPhases - 1);
        const float t = (float)(fp - p);
        const float* r0 = &tab_[(size_t)p * N];
        const float* r1 = r0 + N;
        for (int k = 0; k < N; ++k) coef_[k] = r0[k] + t * (r1[k] - r0[k]);
        const float* x = &buf_[(i - (kHalf - 1)) * ch_];
        float* o = out + n * ch_;
        for (int c = 0; c < ch_; ++c) o[c] = 0.f;
        for (int k = 0; k < N; ++k) {
            const float g = coef_[k];
            const float* xk = x + (size_t)k * ch_;
            for (int c = 0; c < ch_; ++c) o[c] += g * xk[c];
        }
        pos_ += ratio;
    }
    // Compactage : on garde l'historique nécessaire au filtre
    const size_t ip = (size_t)pos_;
    const size_t keepFrom = ip >= (size_t)(kHalf - 1) ? ip - (kHalf - 1) : 0;
    if (keepFrom > 0) {
        buf_.erase(buf_.begin(), buf_.begin() + std::min(buf_.size(), keepFrom * ch_));
        pos_ -= (double)keepFrom;
    }
    return n;
}

void AdaptiveResampler::dropOldest(double keep) {
    const double avail = available();
    if (avail <= keep) return;
    const size_t drop = (size_t)(avail - keep);
    const size_t nIn = buf_.size() / ch_;
    const size_t from = std::min(nIn, drop);
    buf_.erase(buf_.begin(), buf_.begin() + from * ch_);
    if (buf_.size() < (size_t)(kHalf - 1) * ch_) reset();
}

void DriftController::init(double inRate, double outRate, double marginMs, double maxMarginMs) {
    inRate_ = inRate; outRate_ = outRate; nominal_ = inRate / outRate;
    margin_ = marginMs * 1e-3 * inRate; maxMargin_ = maxMarginMs * 1e-3 * inRate;
    winLow_ = 1e18; winTime_ = 0; lastLow_ = margin_; integ_ = 0; adj_ = 0;
}

void DriftController::onUnderrun() {
    margin_ = std::min(maxMargin_, margin_ + 0.010 * inRate_);
    winLow_ = 1e18; winTime_ = 0;
}

double DriftController::update(double residual, size_t outFrames) {
    winLow_ = std::min(winLow_, residual);
    winTime_ += outFrames / outRate_;
    if (winTime_ >= 0.5) {                       // une décision par demi-seconde
        lastLow_ = winLow_;
        const double err = (winLow_ - margin_) / inRate_;            // secondes d'avance
        // Intégrale = dérive réelle des horloges (quelques dizaines de ppm en général)
        integ_ = std::clamp(integ_ + 0.02 * err * winTime_, -0.0015, 0.0015);
        // Proportionnel : résorbe un excédent en ~10 s ; plafond ±0,2 % (inaudible)
        adj_ = std::clamp(0.1 * err + integ_, -0.002, 0.002);
        winLow_ = 1e18; winTime_ = 0;
    }
    return nominal_ * (1.0 + adj_);
}

void LiveInputStage::init(int channels, double inRate, double outRate, int marginMs) {
    ch_ = channels; inRate_ = inRate;
    F_ = std::max(16, (int)std::lround(0.002 * outRate));
    rs_.init(channels, (size_t)(inRate * 2));
    rs_.setNominalRatio(inRate / outRate);
    old_.reserve((size_t)F_ * channels);
    dc_.init(inRate, outRate, marginMs);
    priming_ = true; fadeIn_ = 0; underruns_ = 0; drops_ = 0;
}

void LiveInputStage::produce(float* out, size_t frames, bool sourceIdle) {
    std::fill(out, out + frames * ch_, 0.f);
    if (priming_) {
        if (rs_.available() < dc_.primeFrames()) return;
        priming_ = false;
        fadeIn_ = F_;
    }
    const double ratio = dc_.ratio();

    // Débordement (l'appli a repris après une pause, sortie de veille…) : on saute en avant,
    // en fondu enchaîné entre l'ancienne position et la nouvelle.
    size_t xf = 0;
    if (rs_.available() > dc_.marginFrames() + 0.25 * inRate_) {
        old_.assign((size_t)F_ * ch_, 0.f);
        xf = rs_.pull(old_.data(), (size_t)F_, ratio);
        rs_.dropOldest(dc_.primeFrames());
        ++drops_;
    }

    // Pas assez pour ce bloc + la réserve de fondu : on joue le reste en fondu de sortie.
    size_t n = frames;
    bool under = false;
    if (rs_.available() < (frames + F_) * ratio + 3) {
        under = true;
        n = std::min(frames, (size_t)std::max(0.0, (rs_.available() - 3.0) / ratio));
    }
    const size_t got = rs_.pull(out, n, ratio);

    if (xf) {
        const size_t k = std::min(xf, got);
        for (size_t i = 0; i < k; ++i) {
            const float t = (float)(i + 1) / (float)(k + 1);
            for (int c = 0; c < ch_; ++c) out[i * ch_ + c] = old_[i * ch_ + c] * (1.f - t) + out[i * ch_ + c] * t;
        }
    }
    if (fadeIn_ > 0) {
        for (size_t i = 0; i < got && fadeIn_ > 0; ++i, --fadeIn_) {
            const float g = (float)(F_ - fadeIn_ + 1) / (float)(F_ + 1);
            for (int c = 0; c < ch_; ++c) out[i * ch_ + c] *= g;
        }
    }
    if (under) {
        const size_t k = std::min(got, (size_t)F_);
        for (size_t i = 0; i < k; ++i) {
            const float g = (float)(k - i) / (float)(k + 1);
            for (int c = 0; c < ch_; ++c) out[(got - k + i) * ch_ + c] *= g;
        }
        if (!sourceIdle) { ++underruns_; dc_.onUnderrun(); }
        rs_.reset();
        priming_ = true;
        fadeIn_ = 0;
    } else {
        dc_.update(rs_.available(), frames);
    }
}

} // namespace s3d
