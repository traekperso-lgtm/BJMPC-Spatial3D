#include "layout.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace s3d {

static bool has(uint64_t m, int id) { return id >= 0 && id < 64 && ((m >> id) & 1ULL); }
static uint64_t bit(int id) { return 1ULL << id; }

uint64_t Layout::mask() const {
    uint64_t m = 0;
    for (auto& s : speakers) if (s.id >= 0 && s.id < 64) m |= bit(s.id);
    return m;
}

int Layout::indexOf(int id) const {
    for (size_t i = 0; i < speakers.size(); ++i) if (speakers[i].id == id) return (int)i;
    return -1;
}

bool channelPosition(int id, uint64_t ctx, double& az, double& el, bool& lfe) {
    az = 0; el = 0; lfe = false;
    // Surround : si côtés ET arrières coexistent (7.1), côtés à ±90° et arrières à ±135° (ITU-R BS.2051),
    // sinon le couple présent est à ±110° (5.1 ITU-R BS.775).
    const bool sides = has(ctx, CH_SL) || has(ctx, CH_SR);
    const bool backs = has(ctx, CH_BL) || has(ctx, CH_BR);
    switch (id) {
    case CH_FL: case CH_STEREO_L: az = 30; break;
    case CH_FR: case CH_STEREO_R: az = -30; break;
    case CH_FC: az = 0; break;
    case CH_LFE: case CH_LFE2: lfe = true; break;
    case CH_BL: az = sides ? 135 : 110; break;
    case CH_BR: az = sides ? -135 : -110; break;
    case CH_FLC: az = 15; break;
    case CH_FRC: az = -15; break;
    case CH_BC: az = 180; break;
    case CH_SL: case CH_SDL: az = backs ? 90 : 110; break;
    case CH_SR: case CH_SDR: az = backs ? -90 : -110; break;
    case CH_WIDE_L: az = 60; break;
    case CH_WIDE_R: az = -60; break;
    case CH_TC: az = 0; el = 90; break;
    case CH_TFL: az = 45; el = 45; break;
    case CH_TFC: az = 0; el = 45; break;
    case CH_TFR: az = -45; el = 45; break;
    case CH_TBL: az = 135; el = 45; break;
    case CH_TBC: az = 180; el = 45; break;
    case CH_TBR: az = -135; el = 45; break;
    case CH_TSL: az = 90; el = 45; break;
    case CH_TSR: az = -90; el = 45; break;
    case CH_BFC: az = 0; el = -30; break;
    case CH_BFL: az = 45; el = -30; break;
    case CH_BFR: az = -45; el = -30; break;
    default: return false;
    }
    return true;
}

const char* channelName(int id) {
    switch (id) {
    case CH_FL: return "Avant G"; case CH_FR: return "Avant D"; case CH_FC: return "Centre";
    case CH_LFE: return "LFE"; case CH_BL: return "Arriere G"; case CH_BR: return "Arriere D";
    case CH_FLC: return "Avant G-centre"; case CH_FRC: return "Avant D-centre";
    case CH_BC: return "Arriere centre"; case CH_SL: return "Cote G"; case CH_SR: return "Cote D";
    case CH_TC: return "Plafond centre"; case CH_TFL: return "Haut avant G"; case CH_TFC: return "Haut avant C";
    case CH_TFR: return "Haut avant D"; case CH_TBL: return "Haut arriere G"; case CH_TBC: return "Haut arriere C";
    case CH_TBR: return "Haut arriere D"; case CH_STEREO_L: return "Stereo G"; case CH_STEREO_R: return "Stereo D";
    case CH_WIDE_L: return "Large G"; case CH_WIDE_R: return "Large D"; case CH_SDL: return "Surround direct G";
    case CH_SDR: return "Surround direct D"; case CH_LFE2: return "LFE 2"; case CH_TSL: return "Haut cote G";
    case CH_TSR: return "Haut cote D"; case CH_BFC: return "Bas avant C"; case CH_BFL: return "Bas avant G";
    case CH_BFR: return "Bas avant D";
    default: return "Inconnu";
    }
}

Layout layoutFromMask(uint64_t mask, const std::string& name) {
    Layout L;
    L.name = name.empty() ? describeMask(mask) : name;
    for (int id = 0; id < 64; ++id) {
        if (!has(mask, id)) continue;
        Speaker s; s.id = id;
        if (!channelPosition(id, mask, s.az, s.el, s.lfe)) continue;
        L.speakers.push_back(s);
    }
    return L;
}

struct Named { const char* name; uint64_t mask; };
static const uint64_t M_20 = bit(CH_FL) | bit(CH_FR);
static const uint64_t M_51 = M_20 | bit(CH_FC) | bit(CH_LFE) | bit(CH_SL) | bit(CH_SR);
static const uint64_t M_71 = M_51 | bit(CH_BL) | bit(CH_BR);
static const uint64_t M_TOP2 = bit(CH_TFL) | bit(CH_TFR);
static const uint64_t M_TOP4 = M_TOP2 | bit(CH_TBL) | bit(CH_TBR);

static const Named kNamed[] = {
    {"1.0", bit(CH_FC)},
    {"2.0", M_20},
    {"2.1", M_20 | bit(CH_LFE)},
    {"3.0", M_20 | bit(CH_FC)},
    {"3.1", M_20 | bit(CH_FC) | bit(CH_LFE)},
    {"quad", M_20 | bit(CH_BL) | bit(CH_BR)},
    {"5.0", M_51 & ~bit(CH_LFE)},
    {"5.1", M_51},
    {"7.1", M_71},
    {"5.1.2", M_51 | M_TOP2},
    {"5.1.4", M_51 | M_TOP4},
    {"7.1.2", M_71 | M_TOP2},
    {"7.1.4", M_71 | M_TOP4},
};

std::vector<std::string> knownLayoutNames() {
    std::vector<std::string> v;
    for (auto& n : kNamed) v.push_back(n.name);
    v.push_back("casque");
    return v;
}

bool layoutFromName(const std::string& raw, Layout& out) {
    std::string n = raw;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (n == "casque" || n == "headphones" || n == "binaural") {
        out = layoutFromMask(M_20, "casque (binaural)");
        out.headphones = true;
        return true;
    }
    if (n == "stereo") n = "2.0";
    if (n == "mono") n = "1.0";
    for (auto& k : kNamed) {
        if (n == k.name) { out = layoutFromMask(k.mask, k.name); return true; }
    }
    return false;
}

std::string describeMask(uint64_t mask) {
    for (auto& k : kNamed) if (k.mask == mask) return k.name;
    // Variante "5.1 (arrières)" de l'ancien KSAUDIO_SPEAKER_5POINT1
    const uint64_t m51back = M_20 | bit(CH_FC) | bit(CH_LFE) | bit(CH_BL) | bit(CH_BR);
    if (mask == m51back) return "5.1 (arrieres)";
    int n = 0; for (int i = 0; i < 64; ++i) if (has(mask, i)) ++n;
    return std::to_string(n) + " canaux (masque 0x" + [&] {
        char b[32]; snprintf(b, sizeof b, "%llX", (unsigned long long)mask); return std::string(b); }() + ")";
}

SpeakerOverrides parseSpeakerOverrides(const std::string& spec) {
    SpeakerOverrides o;
    std::stringstream ss(spec);
    for (std::string part; std::getline(ss, part, ';');) {
        const size_t bar = part.find('|');
        if (bar == std::string::npos || bar == 0) continue;
        auto& m = o[part.substr(0, bar)];
        std::stringstream ps(part.substr(bar + 1));
        for (std::string kv; std::getline(ps, kv, ',');) {
            const size_t c = kv.find(':');
            if (c == std::string::npos) continue;
            SpeakerPos p;
            p.az = atof(kv.c_str() + c + 1);
            const size_t at = kv.find('@', c);
            if (at != std::string::npos) p.dist = atof(kv.c_str() + at + 1);
            if (!std::isfinite(p.az) || !std::isfinite(p.dist)) continue;
            p.dist = std::min(kMaxSpeakerDist, std::max(kMinSpeakerDist, p.dist));
            m[atoi(kv.c_str())] = p;
        }
        if (m.empty()) o.erase(part.substr(0, bar));
    }
    return o;
}

std::string formatSpeakerOverrides(const SpeakerOverrides& o) {
    std::string s;
    for (auto& [name, m] : o) {
        if (m.empty()) continue;
        if (!s.empty()) s += ';';
        s += name + '|';
        bool first = true;
        for (auto& [id, p] : m) {
            char b[48];
            if (std::fabs(p.dist - kDefaultSpeakerDist) >= 0.005)
                snprintf(b, sizeof b, "%s%d:%.0f@%.2f", first ? "" : ",", id, p.az, p.dist);
            else
                snprintf(b, sizeof b, "%s%d:%.0f", first ? "" : ",", id, p.az);
            s += b; first = false;
        }
    }
    return s;
}

static double wrapAz(double a) {
    while (a > 180) a -= 360;
    while (a <= -180) a += 360;
    return a;
}

void applySpeakerOverrides(Layout& l, const std::string& spec) {
    if (l.headphones || spec.empty()) return;
    const auto o = parseSpeakerOverrides(spec);
    auto it = o.find(l.name);
    if (it == o.end()) return;
    for (int i = 0; i < (int)l.speakers.size(); ++i) {
        auto& sp = l.speakers[i];
        auto f = it->second.find(sp.id);
        if (f == it->second.end()) continue;
        sp.dist = f->second.dist;
        if (!sp.lfe && speakerMoveAllowed(l, i, f->second.az)) sp.az = wrapAz(f->second.az);
    }
}

bool speakerMoveAllowed(const Layout& l, int i, double az) {
    if (i < 0 || i >= (int)l.speakers.size() || l.speakers[i].lfe) return false;
    for (int k = 0; k < (int)l.speakers.size(); ++k) {
        const auto& o = l.speakers[k];
        if (k == i || o.lfe || std::fabs(o.el - l.speakers[i].el) > 10) continue;
        if (std::fabs(wrapAz(o.az - az)) < 5) return false;
    }
    return true;
}

DistanceAlignment computeDistanceAlignment(const Layout& l) {
    DistanceAlignment a;
    const size_t n = l.speakers.size();
    a.delaySec.assign(n, 0.0); a.gain.assign(n, 1.0);
    if (l.headphones || n == 0) return a;
    double dmax = 0;
    for (auto& sp : l.speakers) dmax = std::max(dmax, sp.dist);
    for (size_t i = 0; i < n; ++i) {
        const double d = l.speakers[i].dist;
        a.delaySec[i] = (dmax - d) / 343.0;          // vitesse du son à 20 °C
        a.gain[i] = d / dmax;                         // niveau en 1/r : la plus proche baisse
        if (std::fabs(dmax - d) > 0.005) a.active = true;
    }
    return a;
}

} // namespace s3d
