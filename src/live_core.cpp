#include "live_core.h"
#include <algorithm>
#include <cmath>

namespace s3d {

void FrameQueue::push(const float* x, size_t frames) {
    std::lock_guard<std::mutex> lk(m_);
    q_.insert(q_.end(), x, x + frames * ch_);
}

void FrameQueue::pushSilence(size_t frames) {
    std::lock_guard<std::mutex> lk(m_);
    q_.insert(q_.end(), frames * ch_, 0.f);
}

void FrameQueue::drainTo(std::vector<float>& dst) {
    std::lock_guard<std::mutex> lk(m_);
    dst.insert(dst.end(), q_.begin(), q_.end());
    q_.clear();
}

void AdaptiveResampler::init(int channels) { ch_ = channels; reset(); }

void AdaptiveResampler::reset() {
    buf_.assign((size_t)ch_, 0.f);   // une trame de marge pour l'interpolation
    pos_ = 1.0;
}

void AdaptiveResampler::push(const std::vector<float>& x) { buf_.insert(buf_.end(), x.begin(), x.end()); }

double AdaptiveResampler::available() const {
    const double frames = (double)(buf_.size() / ch_);
    return std::max(0.0, frames - 2.0 - pos_);
}

size_t AdaptiveResampler::pull(float* out, size_t frames, double ratio) {
    const size_t nIn = buf_.size() / ch_;
    size_t n = 0;
    for (; n < frames; ++n) {
        const size_t i = (size_t)pos_;
        if (i + 2 >= nIn) break;                      // plus assez d'entrée
        const float mu = (float)(pos_ - (double)i);
        const float* a = &buf_[(i - 1) * ch_];
        const float* b = a + ch_;
        const float* c = b + ch_;
        const float* d = c + ch_;
        float* o = out + n * ch_;
        for (int k = 0; k < ch_; ++k) {
            const float c1 = 0.5f * (c[k] - a[k]);
            const float c2 = a[k] - 2.5f * b[k] + 2.f * c[k] - 0.5f * d[k];
            const float c3 = 0.5f * (d[k] - a[k]) + 1.5f * (b[k] - c[k]);
            o[k] = ((c3 * mu + c2) * mu + c1) * mu + b[k];
        }
        pos_ += ratio;
    }
    // Compactage : on garde une trame avant la position courante
    const size_t keepFrom = (size_t)pos_ >= 1 ? (size_t)pos_ - 1 : 0;
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
    if (buf_.size() < (size_t)ch_) buf_.assign((size_t)ch_, 0.f);
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
    rs_.init(channels);
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
