#include "vbap.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

namespace s3d {

static constexpr double kPi = 3.14159265358979323846;
static double rad(double d) { return d * kPi / 180.0; }
static double deg(double r) { return r * 180.0 / kPi; }
static double wrap180(double a) { while (a > 180) a -= 360; while (a <= -180) a += 360; return a; }

Vec3 dirVec(double az, double el) {
    const double a = rad(az), e = rad(el);
    return { std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e) };
}
static Vec3 sub(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static Vec3 cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 norm(Vec3 a) { double n = std::sqrt(dot(a, a)); return n > 0 ? Vec3{ a.x / n, a.y / n, a.z / n } : a; }

static bool invert3(const double m[9], double inv[9]) {
    const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::fabs(det) < 1e-9) return false;
    const double id = 1.0 / det;
    inv[0] = (m[4] * m[8] - m[5] * m[7]) * id; inv[1] = (m[2] * m[7] - m[1] * m[8]) * id; inv[2] = (m[1] * m[5] - m[2] * m[4]) * id;
    inv[3] = (m[5] * m[6] - m[3] * m[8]) * id; inv[4] = (m[0] * m[8] - m[2] * m[6]) * id; inv[5] = (m[2] * m[3] - m[0] * m[5]) * id;
    inv[6] = (m[3] * m[7] - m[4] * m[6]) * id; inv[7] = (m[1] * m[6] - m[0] * m[7]) * id; inv[8] = (m[0] * m[4] - m[1] * m[3]) * id;
    return true;
}

Panner::Panner(const Layout& out) {
    outCount_ = (int)out.speakers.size();
    std::vector<int> real;
    for (int i = 0; i < outCount_; ++i) if (!out.speakers[i].lfe) real.push_back(i);
    if (real.empty()) return;

    for (int i : real) if (std::fabs(out.speakers[i].el) > 5.0) is3D_ = true;
    if (real.size() < 3) is3D_ = false;

    if (!is3D_) {
        // ---- 2D : paires adjacentes autour du cercle ----
        std::sort(real.begin(), real.end(), [&](int a, int b) {
            return wrap180(out.speakers[a].az) < wrap180(out.speakers[b].az); });
        for (int i : real) {
            map_.push_back(i);
            az_.push_back(wrap180(out.speakers[i].az));
            pos_.push_back(dirVec(out.speakers[i].az, 0));
        }
        const int n = (int)map_.size();
        surround_ = n >= 3;
        for (int i = 0; n >= 2 && i < n; ++i) {
            const int j = (i + 1) % n;
            double gap = az_[j] - az_[i]; if (gap <= 0) gap += 360;
            if (gap >= 179.9) { surround_ = false; continue; }
            Pair p{ i, j, {} };
            const Vec3 a = pos_[i], b = pos_[j];
            const double det = a.x * b.y - a.y * b.x;
            if (std::fabs(det) < 1e-9) continue;
            p.inv[0] = b.y / det; p.inv[1] = -a.y / det; p.inv[2] = -b.x / det; p.inv[3] = a.x / det;
            pairs_.push_back(p);
        }
        return;
    }

    // ---- 3D : enveloppe convexe des directions ----
    double minEl = 90, maxEl = -90;
    for (int i : real) {
        map_.push_back(i);
        pos_.push_back(dirVec(out.speakers[i].az, out.speakers[i].el));
        minEl = std::min(minEl, out.speakers[i].el);
        maxEl = std::max(maxEl, out.speakers[i].el);
    }
    // Haut-parleurs imaginaires pour couvrir le dessous / le dessus
    if (minEl > -10) { map_.push_back(-1); pos_.push_back({ 0, 0, -1 }); }
    if (maxEl < 10)  { map_.push_back(-1); pos_.push_back({ 0, 0, 1 }); }

    const int n = (int)pos_.size();
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            for (int k = j + 1; k < n; ++k) {
                const Vec3 nrm = cross(sub(pos_[j], pos_[i]), sub(pos_[k], pos_[i]));
                if (dot(nrm, nrm) < 1e-12) continue;
                const double d = dot(nrm, pos_[i]);
                if (std::fabs(d) < 1e-6) continue;               // plan passant par l'auditeur
                bool pos = false, neg = false;
                for (int m = 0; m < n && !(pos && neg); ++m) {
                    if (m == i || m == j || m == k) continue;
                    const double s = dot(nrm, pos_[m]) - d;
                    if (s > 1e-9) pos = true; else if (s < -1e-9) neg = true;
                }
                if (pos && neg) continue;                         // pas une face de l'enveloppe
                Tri t{ { i, j, k }, {} };
                const double m9[9] = { pos_[i].x, pos_[i].y, pos_[i].z, pos_[j].x, pos_[j].y, pos_[j].z, pos_[k].x, pos_[k].y, pos_[k].z };
                if (!invert3(m9, t.inv)) continue;
                tris_.push_back(t);
            }

    imagNeighbors_.assign(n, {});
    for (int p = 0; p < n; ++p) {
        if (map_[p] >= 0) continue;
        std::set<int> nb;
        for (auto& t : tris_) {
            if (t.v[0] != p && t.v[1] != p && t.v[2] != p) continue;
            for (int v : t.v) if (v != p && map_[v] >= 0) nb.insert(v);
        }
        imagNeighbors_[p].assign(nb.begin(), nb.end());
    }
}

bool Panner::gains2D(double az, std::vector<double>& g) const {
    const Vec3 p = dirVec(az, 0);
    for (auto& pr : pairs_) {
        const double g0 = p.x * pr.inv[0] + p.y * pr.inv[2];
        const double g1 = p.x * pr.inv[1] + p.y * pr.inv[3];
        if (g0 >= -1e-6 && g1 >= -1e-6) {
            g[pr.a] += std::max(0.0, g0);
            g[pr.b] += std::max(0.0, g1);
            return true;
        }
    }
    return false;
}

std::vector<double> Panner::pointGains(const Vec3& pIn) const {
    std::vector<double> g(pos_.size(), 0.0);
    if (pos_.empty()) return g;
    if (pos_.size() == 1) { g[0] = 1; return g; }

    if (!is3D_) {
        const double horiz = std::sqrt(pIn.x * pIn.x + pIn.y * pIn.y);
        if (horiz < 1e-3) { std::fill(g.begin(), g.end(), 1.0); return g; }   // zénith/nadir : partout
        double az = deg(std::atan2(pIn.y, pIn.x));
        if (gains2D(az, g)) return g;
        if (!surround_) {
            // Disposition frontale (ex. stéréo) : on replie l'arrière vers l'avant…
            if (std::fabs(az) > 90) az = (az > 0 ? 180 - az : -180 - az);
            if (gains2D(az, g)) return g;
            // … puis on colle au haut-parleur le plus proche si on est hors de l'arc couvert.
            int best = 0; double bd = 1e9;
            for (size_t i = 0; i < az_.size(); ++i) {
                const double d = std::fabs(wrap180(az - az_[i]));
                if (d < bd) { bd = d; best = (int)i; }
            }
            g[best] = 1;
        }
        return g;
    }

    const Vec3 p = norm(pIn);
    int best = -1; double bestMin = -1e9; double bg[3] = { 0, 0, 0 };
    for (size_t t = 0; t < tris_.size(); ++t) {
        const auto& tr = tris_[t];
        double gg[3];
        for (int c = 0; c < 3; ++c) gg[c] = p.x * tr.inv[c] + p.y * tr.inv[3 + c] + p.z * tr.inv[6 + c];
        const double mn = std::min({ gg[0], gg[1], gg[2] });
        if (mn > bestMin) { bestMin = mn; best = (int)t; std::copy(gg, gg + 3, bg); }
        if (mn >= -1e-6) break;
    }
    if (best >= 0)
        for (int c = 0; c < 3; ++c) g[tris_[best].v[c]] += std::max(0.0, bg[c]);
    return g;
}

std::vector<double> Panner::finalize(std::vector<double> g) const {
    // Redistribution des haut-parleurs imaginaires vers leurs voisins réels
    for (size_t p = 0; p < g.size(); ++p) {
        if (map_[p] >= 0 || g[p] <= 0) continue;
        const auto& nb = imagNeighbors_[p];
        if (!nb.empty()) for (int v : nb) g[v] += g[p] / std::sqrt((double)nb.size());
        g[p] = 0;
    }
    std::vector<double> out(outCount_, 0.0);
    double pw = 0;
    for (size_t p = 0; p < g.size(); ++p) if (map_[p] >= 0) { out[map_[p]] += g[p]; }
    for (double v : out) pw += v * v;
    if (pw > 1e-12) { const double k = 1.0 / std::sqrt(pw); for (double& v : out) v *= k; }
    return out;
}

std::vector<double> Panner::gains(double az, double el, double spread) const {
    const Vec3 p = dirVec(az, el);
    if (spread <= 0.01) return finalize(pointGains(p));

    // MDAP : centre + couronne de 8 directions à spread/2
    Vec3 u = std::fabs(p.z) < 0.99 ? norm(cross(p, { 0, 0, 1 })) : norm(cross(p, { 1, 0, 0 }));
    Vec3 w = cross(p, u);
    const double h = rad(std::min(spread, 180.0) * 0.5);
    std::vector<double> acc(outCount_, 0.0);
    auto add = [&](const Vec3& d) { auto g = finalize(pointGains(d)); for (int i = 0; i < outCount_; ++i) acc[i] += g[i]; };
    add(p);
    for (int k = 0; k < 8; ++k) {
        const double phi = 2 * kPi * k / 8;
        const Vec3 r{ std::cos(phi) * u.x + std::sin(phi) * w.x, std::cos(phi) * u.y + std::sin(phi) * w.y, std::cos(phi) * u.z + std::sin(phi) * w.z };
        add({ std::cos(h) * p.x + std::sin(h) * r.x, std::cos(h) * p.y + std::sin(h) * r.y, std::cos(h) * p.z + std::sin(h) * r.z });
    }
    double pw = 0; for (double v : acc) pw += v * v;
    if (pw > 1e-12) { const double k = 1.0 / std::sqrt(pw); for (double& v : acc) v *= k; }
    return acc;
}

} // namespace s3d
