#include "renderer.h"
#include "dsp.h"
#include "vbap.h"
#include "convolver.h"
#include "hrtf_builtin.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>
#include <cstdio>
#include <sstream>

#ifdef S3D_WITH_MYSOFA
#include <mysofa.h>
#endif

namespace s3d {

static constexpr double kPi = 3.14159265358979323846;

bool sofaSupported() {
#ifdef S3D_WITH_MYSOFA
    return true;
#else
    return false;
#endif
}

static std::string fmtDb(double g) {
    char b[32];
    if (g <= 1e-6) return "-inf";
    snprintf(b, sizeof b, "%+.1f dB", 20 * std::log10(g));
    return b;
}

static std::string speakerLabel(const Layout& L, int i) { return channelName(L.speakers[i].id); }

// =====================================================================================
//  Haut-parleurs : matrice de gains VBAP
// =====================================================================================
std::vector<float> buildSpeakerMatrix(const std::vector<VirtualSource>& src, int inCh,
                                      const Layout& out, const RenderOptions& opt) {
    const int O = (int)out.speakers.size();
    std::vector<float> M((size_t)O * inCh, 0.f);
    Panner panner(out);

    const int lfeOut = [&] { for (int i = 0; i < O; ++i) if (out.speakers[i].lfe) return i; return -1; }();
    const int fl = out.indexOf(CH_FL), fr = out.indexOf(CH_FR), fc = out.indexOf(CH_FC);

    for (const auto& s : src) {
        std::vector<double> g(O, 0.0);
        if (s.lfe) {
            if (lfeOut >= 0) g[lfeOut] = 1.0;
            else if (fl >= 0 && fr >= 0) { g[fl] = opt.lfeToMains; g[fr] = opt.lfeToMains; }
            else if (fc >= 0) g[fc] = opt.lfeToMains * std::sqrt(2.0);
            else for (int i = 0; i < O; ++i) g[i] = opt.lfeToMains / std::sqrt((double)O);
        } else {
            g = panner.gains(s.az, s.el, opt.spreadDeg);
        }
        for (int o = 0; o < O; ++o)
            for (int i = 0; i < inCh; ++i)
                M[(size_t)o * inCh + i] += (float)(g[o] * s.w[i]);
    }
    return M;
}

class MatrixRenderer : public Renderer {
public:
    MatrixRenderer(const std::vector<VirtualSource>& src, int inCh, const Layout& out, const RenderOptions& opt)
        : in_(inCh), out_((int)out.speakers.size()), layout_(out), src_(src), opt_(opt) {
        M_ = buildSpeakerMatrix(src, inCh, out, opt);
        Panner p(out);
        is3D_ = p.is3D();
        for (int o = 0; o < out_; ++o) {
            std::vector<std::pair<int, float>> row;
            for (int i = 0; i < in_; ++i) {
                const float g = M_[(size_t)o * in_ + i];
                if (std::fabs(g) > 1e-7f) row.push_back({ i, g });
            }
            rows_.push_back(std::move(row));
        }
    }
    int inChannels() const override { return in_; }
    int outChannels() const override { return out_; }
    void process(const float* in, float* out, size_t frames) override {
        for (size_t n = 0; n < frames; ++n) {
            const float* x = in + n * in_;
            float* y = out + n * out_;
            for (int o = 0; o < out_; ++o) {
                float acc = 0.f;
                for (auto& [i, g] : rows_[o]) acc += g * x[i];
                y[o] = acc;
            }
        }
    }
    std::string describe() const override {
        std::ostringstream ss;
        ss << "Rendu haut-parleurs (" << layout_.name << ", VBAP " << (is3D_ ? "3D" : "2D")
           << (opt_.spreadDeg > 0 ? ", etalement " + std::to_string((int)opt_.spreadDeg) + " deg" : "") << ")\n";
        const bool ambi = !src_.empty() && src_[0].label.rfind("Ambi", 0) == 0;
        if (ambi) {
            ss << "  Ambisonique decode sur 12 directions virtuelles puis VBAP (AllRAD)\n";
            for (int o = 0; o < out_; ++o) {
                ss << "  " << speakerLabel(layout_, o) << " <- ";
                for (int i = 0; i < in_ && i < 4; ++i) { char b[32]; snprintf(b, sizeof b, "%s%+.2f", i ? " " : "", M_[(size_t)o * in_ + i]); ss << b; }
                ss << (in_ > 4 ? " ..." : "") << "\n";
            }
            return ss.str();
        }
        Panner panner(layout_);
        for (const auto& s : src_) {
            char head[96];
            if (s.lfe) snprintf(head, sizeof head, "  %-18s (LFE)          -> ", s.label.c_str());
            else snprintf(head, sizeof head, "  %-18s az %4.0f el %3.0f -> ", s.label.c_str(), s.az, s.el);
            ss << head;
            // colonne d'entrée correspondant à cette source
            int col = 0; for (int i = 0; i < in_; ++i) if (s.w[i] != 0.f) { col = i; break; }
            bool first = true;
            for (int o = 0; o < out_; ++o) {
                const float g = M_[(size_t)o * in_ + col];
                if (g < 0.03f) continue;
                ss << (first ? "" : ", ") << speakerLabel(layout_, o) << " " << fmtDb(g);
                first = false;
            }
            if (first) ss << "(muet)";
            ss << "\n";
        }
        return ss.str();
    }
private:
    int in_, out_;
    Layout layout_;
    std::vector<VirtualSource> src_;
    RenderOptions opt_;
    std::vector<float> M_;
    std::vector<std::vector<std::pair<int, float>>> rows_;
    bool is3D_ = false;
};

// =====================================================================================
//  Casque : HRTF paramétrique (Brown & Duda 1998)
// =====================================================================================
class ParamEar {
public:
    void init(double fs, const Vec3& p, bool left, double az, double el) {
        const double a = 0.0875, c = 343.0;          // rayon de tête (m), vitesse du son
        const Vec3 ear{ 0, left ? 1.0 : -1.0, 0 };
        const double cosT = std::clamp(p.x * ear.x + p.y * ear.y + p.z * ear.z, -1.0, 1.0);
        const double theta = std::acos(cosT);        // angle source / axe de l'oreille

        // ITD (Woodworth / Brown-Duda), décalé pour rester positif, +2 éch. pour l'interpolation
        double T = theta < kPi / 2 ? -std::cos(theta) : (theta - kPi / 2);
        T = (T + 1.0) * a / c;
        delay_ = T * fs + 2.0;

        // Ombre de tête : H(s) = (1 + α τ s) / (1 + τ s), τ = a / 2c ; transformée bilinéaire
        const double thetaDeg = theta * 180 / kPi;
        const double alpha = 1.05 + 0.95 * std::cos(thetaDeg / 150.0 * kPi);
        const double tau = a / (2 * c), K = 2 * fs;
        const double a0 = 1 + tau * K;
        b0_ = (float)((1 + alpha * tau * K) / a0);
        b1_ = (float)((1 - alpha * tau * K) / a0);
        a1_ = (float)((1 - tau * K) / a0);

        // Échos du pavillon (indices d'élévation)
        static const double rho[5] = { 0.5, -1.0, 0.5, -0.25, 0.25 };
        static const double A[5] = { 1, 5, 5, 5, 5 };
        static const double B[5] = { 2, 4, 7, 11, 13 };
        static const double D[5] = { 1, 0.5, 0.5, 0.5, 0.5 };
        const double azEar = left ? az : -az;
        const double scale = fs / 44100.0;
        for (int k = 0; k < 5; ++k) {
            double t = A[k] * std::cos(azEar * kPi / 360.0) * std::sin(D[k] * (90.0 - el) * kPi / 180.0) + B[k];
            t = std::clamp(t * scale, 1.0, 60.0);
            tapI_[k] = (int)t; tapF_[k] = (float)(t - tapI_[k]);
            rho_[k] = (float)(rho[k] * 0.35);         // dosage modéré pour éviter la coloration
        }
        // Assombrissement des sources arrière (le pavillon masque les aigus)
        rear_ = (float)(0.55 * std::max(0.0, -p.x));
        lpA_ = (float)(1.0 - std::exp(-2 * kPi * 5000.0 / fs));
    }
    inline float tick(float x) {
        dl_[w_] = x;
        // lecture fractionnaire (Hermite)
        const int di = (int)delay_; const float f = (float)(delay_ - di);
        const int base = w_ - di - 1;
        const float ym1 = dl_[(base - 1) & 255], y0 = dl_[base & 255], y1 = dl_[(base + 1) & 255], y2 = dl_[(base + 2) & 255];
        const float mu = 1.f - f;
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        const float d = ((c3 * mu + c2) * mu + c1) * mu + y0;
        w_ = (w_ + 1) & 255;
        // ombre de tête
        const float s = b0_ * d + b1_ * x1_ - a1_ * y1_;
        x1_ = d; y1_ = s;
        // pavillon
        ph_[pw_] = s;
        float o = s;
        for (int k = 0; k < 5; ++k) {
            const float a = ph_[(pw_ - tapI_[k]) & 63], b = ph_[(pw_ - tapI_[k] - 1) & 63];
            o += rho_[k] * (a + tapF_[k] * (b - a));
        }
        pw_ = (pw_ + 1) & 63;
        // arrière
        lp_ += lpA_ * (o - lp_);
        return (1.f - rear_) * o + rear_ * lp_;
    }
private:
    float dl_[256] = {}; int w_ = 0; double delay_ = 2;
    float b0_ = 1, b1_ = 0, a1_ = 0, x1_ = 0, y1_ = 0;
    float ph_[64] = {}; int pw_ = 0;
    int tapI_[5] = {}; float tapF_[5] = {}; float rho_[5] = {};
    float rear_ = 0, lp_ = 0, lpA_ = 0.5f;
};


#ifdef S3D_WITH_MYSOFA
static int lastAbove(const std::vector<float>& h, float relThr) {
    float pk = 0; for (float v : h) pk = std::max(pk, std::fabs(v));
    for (size_t i = h.size(); i-- > 0;) if (std::fabs(h[i]) > pk * relThr) return (int)i;
    return (int)h.size() - 1;
}
#endif

// Pièce virtuelle « casque » : réflexions précoces d'une pièce de salon (méthode des sources images,
// 1er ordre) intégrées à la réponse de chaque haut-parleur virtuel. C'est ce qui sort le son de la
// tête : une HRTF seule, anéchoïque, sonne souvent « dans le crâne ».
struct EarlyReflection { double az, el, delaySec, gain; };
static std::vector<EarlyReflection> shoeboxReflections(double az, double el, double dist) {
    // pièce 6,6 x 6 x 2,7 m ; auditeur assis à 1,2 m, un peu en arrière du centre
    const double xf = 3.6, xb = -3.0, yl = 3.0, yr = -3.0, zf = -1.2, zc = 1.5;   // murs relatifs à l'auditeur
    const Vec3 u = dirVec(az, el);
    dist = std::clamp(dist, 0.3, 5.0);
    Vec3 S{ u.x * dist, u.y * dist, u.z * dist };
    S.x = std::clamp(S.x, xb + 0.2, xf - 0.2); S.y = std::clamp(S.y, yr + 0.2, yl - 0.2); S.z = std::clamp(S.z, zf + 0.2, zc - 0.2);
    const double d0 = std::sqrt(S.x * S.x + S.y * S.y + S.z * S.z);
    const Vec3 img[6] = { { 2 * xf - S.x, S.y, S.z }, { 2 * xb - S.x, S.y, S.z }, { S.x, 2 * yl - S.y, S.z },
                          { S.x, 2 * yr - S.y, S.z }, { S.x, S.y, 2 * zf - S.z }, { S.x, S.y, 2 * zc - S.z } };
    const double refl[6] = { 0.55, 0.6, 0.6, 0.6, 0.45, 0.5 };   // rideaux/canapé, murs, tapis, plafond
    std::vector<EarlyReflection> r;
    for (int k = 0; k < 6; ++k) {
        const Vec3& p = img[k];
        const double d = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        const double raz = std::atan2(p.y, p.x) * 180 / kPi, rel = std::asin(std::clamp(p.z / d, -1.0, 1.0)) * 180 / kPi;
        r.push_back({ raz, rel, (d - d0) / 343.0, refl[k] * d0 / d });
    }
    return r;
}

class BinauralRenderer : public Renderer {
public:
    bool init(const std::vector<VirtualSource>& src, int inCh, double fs, const RenderOptions& opt, std::string& err) {
        in_ = inCh; src_ = src; opt_ = opt; fs_ = fs;
        for (size_t i = 0; i < src.size(); ++i) (src[i].lfe ? lfe_ : spatial_).push_back((int)i);
        // Source des réponses : fichier .sofa choisi, sinon HRTF KEMAR intégrée
        std::function<bool(double, double, std::vector<float>&, std::vector<float>&)> hrir;
#ifdef S3D_WITH_MYSOFA
        MYSOFA_EASY* easy = nullptr;
        float sofaR = 1.f;
#endif
        if (!opt.sofaPath.empty()) {
#ifdef S3D_WITH_MYSOFA
            int len = 0, e = 0;
            easy = mysofa_open(opt.sofaPath.c_str(), (float)fs, &len, &e);
            if (!easy) { err = "Impossible d'ouvrir le fichier SOFA (code " + std::to_string(e) + ")"; return false; }
            const float* sp = easy->hrtf->SourcePosition.values;
            sofaR = std::sqrt(sp[0] * sp[0] + sp[1] * sp[1] + sp[2] * sp[2]);
            sofaLen_ = len;
            hrir = [&, len](double az, double el, std::vector<float>& L, std::vector<float>& R) {
                const Vec3 p = dirVec(az, el);
                L.assign(len, 0.f); R.assign(len, 0.f);
                float dl = 0, dr = 0;
                mysofa_getfilter_float(easy, (float)(p.x * sofaR), (float)(p.y * sofaR), (float)(p.z * sofaR), L.data(), R.data(), &dl, &dr);
                const int tail = std::max(lastAbove(L, 3e-4f), lastAbove(R, 3e-4f)) + 1;
                L.resize(tail); R.resize(tail);
                L.insert(L.begin(), (size_t)std::max(0L, std::lround(dl * fs)), 0.f);
                R.insert(R.begin(), (size_t)std::max(0L, std::lround(dr * fs)), 0.f);
                return true;
            };
            hrtfName_ = "SOFA";
#else
            err = "Ce binaire a ete compile sans libmysofa : choisissez l'HRTF integree (aucun fichier .sofa)";
            return false;
#endif
        } else {
            hrir = [fs](double az, double el, std::vector<float>& L, std::vector<float>& R) {
                return builtinHrir(az, el, fs, L, R);
            };
            sofaLen_ = 0;
            hrtfName_ = "KEMAR integree";
        }
        // Réponse binaurale de chaque haut-parleur virtuel : direct + réflexions précoces
        const double early = std::clamp(opt.room / 0.15, 0.0, 1.5);
        std::vector<std::vector<float>> hl, hr;
        std::vector<float> L, R, rl, rr;
        bool ok = true;
        for (int i : spatial_) {
            if (!hrir(src[i].az, src[i].el, L, R)) { ok = false; break; }
            if (early > 0) {
                double e2 = 1;
                const auto refl = shoeboxReflections(src[i].az, src[i].el, src[i].dist);
                size_t need = L.size();
                for (auto& r : refl) need = std::max(need, L.size() + (size_t)std::lround(r.delaySec * fs) + 1);
                L.resize(need, 0.f); R.resize(need, 0.f);
                for (auto& r : refl) {
                    if (!hrir(r.az, r.el, rl, rr)) continue;
                    const double g = r.gain * early;
                    e2 += g * g;
                    // matériaux : un peu d'aigu absorbé à chaque réflexion (passe-bas 1 pôle ~7 kHz)
                    const float a = (float)std::exp(-2 * kPi * 7000.0 / fs);
                    float yl = 0, yr = 0;
                    const size_t off = (size_t)std::lround(r.delaySec * fs);
                    for (size_t n = 0; n < rl.size() && off + n < need; ++n) {
                        yl = (1 - a) * rl[n] + a * yl; yr = (1 - a) * rr[n] + a * yr;
                        L[off + n] += (float)g * yl; R[off + n] += (float)g * yr;
                    }
                }
                // même sonie qu'en anéchoïque
                const float norm = (float)(1.0 / std::sqrt(e2));
                for (auto& v : L) v *= norm;
                for (auto& v : R) v *= norm;
            }
            // distance : niveau en 1/r par rapport à 2,5 m (borné à ±6 dB) ; la part de pièce, elle,
            // augmente naturellement avec l'éloignement (sources images)
            const float gd = (float)std::clamp(kDefaultSpeakerDist / std::max(0.1, src[i].dist), 0.5, 2.0);
            for (auto& v : L) v *= gd;
            for (auto& v : R) v *= gd;
            hl.push_back(L); hr.push_back(R);
        }
#ifdef S3D_WITH_MYSOFA
        if (easy) mysofa_close(easy);
#endif
        if (ok) {
            conv_.init(hl, hr, 128);   // blocs de 128 : 2,7 ms à 48 kHz
            outFifo_.assign((size_t)conv_.block() * 2, 0.f);   // latence d'un bloc
            inFifo_.reserve((size_t)16384 * in_);              // pas d'allocation dans le fil audio
            outFifo_.reserve((size_t)16384 * 2);
            useSofa_ = true;
            irLen_ = hl.empty() ? 0 : (int)hl[0].size();
        } else {
            // repli : modèle paramétrique de tête sphérique (données intégrées illisibles)
            for (int i : spatial_) {
                const Vec3 p = dirVec(src[i].az, src[i].el);
                ParamEar l, r;
                l.init(fs, p, true, src[i].az, src[i].el);
                r.init(fs, p, false, src[i].az, src[i].el);
                param_.push_back({ l, r });
            }
            hrtfName_.clear();
        }
        room_.init(fs);
        return true;
    }
    int inChannels() const override { return in_; }
    int outChannels() const override { return 2; }
    void process(const float* in, float* out, size_t frames) override {
        if (!useSofa_) { renderDirect(in, out, frames); return; }
        // Mode SOFA : accumulation par blocs, sortie retardée d'un bloc
        const int B = conv_.block();
        inFifo_.insert(inFifo_.end(), in, in + frames * in_);
        while (inFifo_.size() >= (size_t)B * in_) {
            blockOut_.assign((size_t)B * 2, 0.f);
            vk_.resize(spatial_.size());
            for (size_t k = 0; k < spatial_.size(); ++k) {
                v_.resize(B);
                mixVirtual(src_[spatial_[k]], inFifo_.data(), B);
                vk_[k] = v_;
            }
            vkp_.resize(vk_.size());
            for (size_t k = 0; k < vk_.size(); ++k) vkp_[k] = vk_[k].data();
            conv_.processBlock(vkp_.data(), blockOut_.data());
            addLfeAndRoom(inFifo_.data(), blockOut_.data(), B, &vk_);
            inFifo_.erase(inFifo_.begin(), inFifo_.begin() + (size_t)B * in_);
            outFifo_.insert(outFifo_.end(), blockOut_.begin(), blockOut_.end());
        }
        std::copy(outFifo_.begin(), outFifo_.begin() + frames * 2, out);
        outFifo_.erase(outFifo_.begin(), outFifo_.begin() + frames * 2);
    }
    void renderDirect(const float* in, float* out, size_t frames) {
        std::fill(out, out + frames * 2, 0.f);
        v_.resize(frames); mono_.assign(frames, 0.f);
        for (size_t k = 0; k < spatial_.size(); ++k) {
            mixVirtual(src_[spatial_[k]], in, frames);
            for (size_t n = 0; n < frames; ++n) mono_[n] += v_[n];
            auto& e = param_[k];
            for (size_t n = 0; n < frames; ++n) {
                out[2 * n] += e.first.tick(v_[n]);
                out[2 * n + 1] += e.second.tick(v_[n]);
            }
        }
        addLfeAndRoom(in, out, frames, nullptr);
    }
    // LFE vers les deux oreilles + envoi mono vers la pièce virtuelle
    void addLfeAndRoom(const float* in, float* out, size_t frames, const std::vector<std::vector<float>>* vk) {
        if (vk) {
            mono_.assign(frames, 0.f);
            for (auto& v : *vk) for (size_t n = 0; n < frames; ++n) mono_[n] += v[n];
        }
        v_.resize(frames);
        for (int i : lfe_) {
            mixVirtual(src_[i], in, frames);
            const float g = (float)opt_.lfeToMains;
            for (size_t n = 0; n < frames; ++n) { out[2 * n] += g * v_[n]; out[2 * n + 1] += g * v_[n]; }
        }
        const float sendNorm = spatial_.empty() ? 0.f : 1.f / std::sqrt((float)spatial_.size());
        for (auto& m : mono_) m *= sendNorm;
        room_.process(mono_.data(), out, frames, (float)opt_.room);
    }
    int latencyFrames() const override { return useSofa_ ? conv_.block() : 0; }
    std::string describe() const override {
        std::ostringstream ss;
        ss << "Rendu casque binaural (" << (useSofa_ ? "HRTF mesuree " + hrtfName_ + ", reponse " + std::to_string(irLen_) + " points avec reflexions precoces, latence " + std::to_string(conv_.block()) + " ech." : std::string("HRTF parametrique tete spherique"))
           << ", piece " << (int)std::lround(opt_.room * 100) << " %)\n";
        for (int i : spatial_) {
            char b[96]; snprintf(b, sizeof b, "  %-18s -> source virtuelle az %4.0f el %3.0f\n", src_[i].label.c_str(), src_[i].az, src_[i].el);
            ss << b;
        }
        for (int i : lfe_) ss << "  " << src_[i].label << " -> deux oreilles " << fmtDb(opt_.lfeToMains) << "\n";
        return ss.str();
    }
private:
    void mixVirtual(const VirtualSource& s, const float* in, size_t frames) {
        std::fill(v_.begin(), v_.end(), 0.f);
        for (int c = 0; c < in_; ++c) {
            const float w = s.w[c];
            if (w == 0.f) continue;
            for (size_t n = 0; n < frames; ++n) v_[n] += w * in[n * in_ + c];
        }
    }
    int in_ = 0; double fs_ = 48000;
    std::vector<VirtualSource> src_;
    RenderOptions opt_;
    std::vector<int> spatial_, lfe_;
    std::vector<std::pair<ParamEar, ParamEar>> param_;
    PartitionedConvolver conv_;
    std::vector<const float*> vkp_;
    bool useSofa_ = false;        // convolution de réponses mesurées (KEMAR intégrée ou .sofa)
    int sofaLen_ = 0, irLen_ = 0;
    std::string hrtfName_;
    RoomReverb room_;
    std::vector<float> v_, mono_, inFifo_, outFifo_, blockOut_;
    std::vector<std::vector<float>> vk_;
};

std::unique_ptr<Renderer> makeRenderer(const std::vector<VirtualSource>& src, int inCh, const Layout& out,
                                       double fs, const RenderOptions& opt, std::string& err) {
    if (out.headphones) {
        auto r = std::make_unique<BinauralRenderer>();
        if (!r->init(src, inCh, fs, opt, err)) return nullptr;
        return r;
    }
    if (out.speakers.empty()) { err = "Disposition de sortie vide"; return nullptr; }
    return std::make_unique<MatrixRenderer>(src, inCh, out, opt);
}

} // namespace s3d
