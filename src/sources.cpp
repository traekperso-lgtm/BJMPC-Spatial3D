#include "sources.h"
#include "layout.h"
#include <cmath>

namespace s3d {

static constexpr double kPi = 3.14159265358979323846;

std::vector<VirtualSource> sourcesFromChannels(const std::vector<int>& ids) {
    uint64_t ctx = 0;
    for (int id : ids) if (id >= 0 && id < 64) ctx |= 1ULL << id;
    std::vector<VirtualSource> v;
    const int n = (int)ids.size();
    for (int i = 0; i < n; ++i) {
        VirtualSource s;
        s.w.assign(n, 0.f);
        s.w[i] = 1.f;
        if (!channelPosition(ids[i], ctx, s.az, s.el, s.lfe)) {
            s.az = 0; s.el = 0;   // canal inconnu : au centre
            s.label = "Canal " + std::to_string(i + 1) + " (inconnu)";
        } else {
            s.label = channelName(ids[i]);
        }
        v.push_back(std::move(s));
    }
    return v;
}

int ambisonicOrderForChannels(int ch) {
    for (int o = 1; o <= 7; ++o) if ((o + 1) * (o + 1) == ch) return o;
    return -1;
}

// Harmoniques sphériques réelles, normalisation SN3D, ordre ACN, jusqu'à l'ordre 2.
static void shSN3D(double az, double el, double y[9]) {
    const double a = az * kPi / 180, e = el * kPi / 180;
    const double ce = std::cos(e), se = std::sin(e), s3 = std::sqrt(3.0) / 2;
    y[0] = 1;
    y[1] = std::sin(a) * ce;
    y[2] = se;
    y[3] = std::cos(a) * ce;
    y[4] = s3 * std::sin(2 * a) * ce * ce;
    y[5] = s3 * std::sin(a) * std::sin(2 * e);
    y[6] = 0.5 * (3 * se * se - 1);
    y[7] = s3 * std::cos(a) * std::sin(2 * e);
    y[8] = s3 * std::cos(2 * a) * ce * ce;
}

std::vector<VirtualSource> sourcesFromAmbisonics(int inCh, int order) {
    if (order > 2) order = 2;            // les composantes d'ordre > 2 sont ignorées
    if (order < 1) order = 1;
    const int nsh = (order + 1) * (order + 1);

    // Icosaèdre : 12 directions, t-design d'ordre 5 (exact jusqu'à l'ordre ambisonique 2)
    const double phi = (1 + std::sqrt(5.0)) / 2;
    const double raw[12][3] = {
        {0, 1, phi}, {0, -1, phi}, {0, 1, -phi}, {0, -1, -phi},
        {1, phi, 0}, {-1, phi, 0}, {1, -phi, 0}, {-1, -phi, 0},
        {phi, 0, 1}, {-phi, 0, 1}, {phi, 0, -1}, {-phi, 0, -1} };

    // Pondération max-rE (Zotter & Frank)
    const double rE = std::cos(137.9 / (order + 1.51) * kPi / 180);
    const double gw[3] = { 1.0, rE, order >= 2 ? 0.5 * (3 * rE * rE - 1) : 0.0 };
    const int ordOf[9] = { 0, 1, 1, 1, 2, 2, 2, 2, 2 };

    std::vector<VirtualSource> v;
    const double K = 12;
    for (int k = 0; k < 12; ++k) {
        const double x = raw[k][0], y = raw[k][1], z = raw[k][2];
        const double r = std::sqrt(x * x + y * y + z * z);
        VirtualSource s;
        s.az = std::atan2(y, x) * 180 / kPi;
        s.el = std::asin(z / r) * 180 / kPi;
        s.label = "Ambi " + std::to_string(k + 1);
        s.w.assign(inCh, 0.f);
        double sh[9]; shSN3D(s.az, s.el, sh);
        // Décodeur par échantillonnage : p_k = 1/K * Σ (2n+1) g_n Y_SN3D(d_k) b_SN3D
        for (int c = 0; c < nsh && c < inCh; ++c) {
            const int n = ordOf[c];
            s.w[c] = (float)((2 * n + 1) * gw[n] * sh[c] / K);
        }
        v.push_back(std::move(s));
    }
    // Normalisation en énergie : une onde plane encodée doit produire une énergie totale de 1
    double b[9]; shSN3D(0, 0, b);
    double E = 0;
    for (auto& s : v) { double p = 0; for (int c = 0; c < nsh && c < inCh; ++c) p += s.w[c] * b[c]; E += p * p; }
    if (E > 1e-12) { const float k = (float)(1.0 / std::sqrt(E)); for (auto& s : v) for (auto& w : s.w) w *= k; }
    return v;
}

void applyVirtualSpeakerOverrides(std::vector<VirtualSource>& src, const std::vector<int>& ids, const std::string& spec) {
    if (spec.empty() || ids.size() != src.size()) return;
    const auto o = parseSpeakerOverrides(spec);
    auto it = o.find(kVirtualSpeakersKey);
    if (it == o.end()) return;
    for (size_t k = 0; k < src.size(); ++k) {
        auto f = it->second.find(ids[k]);
        if (f == it->second.end() || src[k].lfe) continue;
        src[k].az = f->second.az;
        src[k].dist = f->second.dist;
    }
}

} // namespace s3d
