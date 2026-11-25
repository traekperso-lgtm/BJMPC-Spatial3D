#include "hrtf_builtin.h"
#include "vbap.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace s3d {

extern const unsigned char kHrtfKemar[];
extern const size_t kHrtfKemarSize;

namespace {
struct Table {
    bool ok = false;
    int count = 0, taps = 0;
    double fs = 48000;
    std::vector<float> az, el, data;     // data : [dir][oreille][taps]
    std::vector<Vec3> dir;
};

uint32_t rd32(const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
int16_t rd16(const unsigned char* p) { return (int16_t)(p[0] | (p[1] << 8)); }

const Table& table() {
    static Table t;
    static std::once_flag once;
    std::call_once(once, [] {
        const unsigned char* p = kHrtfKemar;
        if (kHrtfKemarSize < 24 || memcmp(p, "S3DH", 4) != 0 || rd32(p + 4) != 1) return;
        const int M = (int)rd32(p + 8), T = (int)rd32(p + 12);
        const double fs = rd32(p + 16);
        float scale; uint32_t sb = rd32(p + 20); memcpy(&scale, &sb, 4);
        if ((size_t)24 + (size_t)M * (4 + 4 * T) > kHrtfKemarSize) return;
        t.count = M; t.taps = T; t.fs = fs;
        t.az.resize(M); t.el.resize(M); t.dir.resize(M); t.data.resize((size_t)M * 2 * T);
        const unsigned char* q = p + 24;
        for (int m = 0; m < M; ++m) {
            t.az[m] = rd16(q) / 10.f; t.el[m] = rd16(q + 2) / 10.f; q += 4;
            t.dir[m] = dirVec(t.az[m], t.el[m]);
            for (int k = 0; k < 2 * T; ++k, q += 2) t.data[(size_t)m * 2 * T + k] = rd16(q) * scale;
        }
        t.ok = true;
    });
    return t;
}
} // namespace

int builtinHrirCount() { return table().count; }

std::vector<float> resampleIr(const std::vector<float>& x, double fromHz, double toHz) {
    if (std::fabs(fromHz - toHz) < 0.5 || x.empty()) return x;
    const double r = toHz / fromHz;
    const size_t n = (size_t)std::ceil(x.size() * r);
    const double cut = std::min(1.0, r);                 // anti-repliement en descente
    const int half = 24;
    std::vector<float> y(n, 0.f);
    for (size_t i = 0; i < n; ++i) {
        const double t = i / r;                          // position dans x
        const long c = (long)std::floor(t);
        double acc = 0;
        for (long k = c - half + 1; k <= c + half; ++k) {
            if (k < 0 || k >= (long)x.size()) continue;
            const double d = t - k, a = d * cut;
            const double s = std::fabs(a) < 1e-9 ? 1.0 : std::sin(3.14159265358979 * a) / (3.14159265358979 * a);
            const double w = std::fabs(d) >= half ? 0.0 : 0.5 * (1 + std::cos(3.14159265358979 * d / half));
            acc += x[k] * s * w * cut;
        }
        y[i] = (float)acc;
    }
    // une réponse impulsionnelle conserve son gain en amplitude : compenser le pas temporel
    for (auto& v : y) v *= (float)(1.0 / r);
    return y;
}

bool builtinHrir(double az, double el, double fs, std::vector<float>& L, std::vector<float>& R, double* gotAz, double* gotEl) {
    const Table& t = table();
    if (!t.ok) return false;
    const Vec3 v = dirVec(az, el);
    int best = 0; double bd = -2;
    for (int m = 0; m < t.count; ++m) {
        const double d = v.x * t.dir[m].x + v.y * t.dir[m].y + v.z * t.dir[m].z;
        if (d > bd) { bd = d; best = m; }
    }
    const float* p = &t.data[(size_t)best * 2 * t.taps];
    L.assign(p, p + t.taps);
    R.assign(p + t.taps, p + 2 * t.taps);
    if (std::fabs(fs - t.fs) >= 0.5) { L = resampleIr(L, t.fs, fs); R = resampleIr(R, t.fs, fs); }
    if (gotAz) *gotAz = t.az[best];
    if (gotEl) *gotEl = t.el[best];
    return true;
}

} // namespace s3d
