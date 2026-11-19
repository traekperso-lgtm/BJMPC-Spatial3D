// Spatial3D — dispositions de haut-parleurs
// Convention : azimut en degrés, positif = vers la GAUCHE, 0 = devant.
//              élévation en degrés, positive = vers le haut.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace s3d {

// Valeurs identiques aux bits Windows SPEAKER_* (ksmedia.h) et aux AV_CHAN_* de FFmpeg.
enum ChannelId : int {
    CH_UNKNOWN = -1,
    CH_FL = 0, CH_FR = 1, CH_FC = 2, CH_LFE = 3, CH_BL = 4, CH_BR = 5,
    CH_FLC = 6, CH_FRC = 7, CH_BC = 8, CH_SL = 9, CH_SR = 10,
    CH_TC = 11, CH_TFL = 12, CH_TFC = 13, CH_TFR = 14, CH_TBL = 15, CH_TBC = 16, CH_TBR = 17,
    // Extensions FFmpeg
    CH_STEREO_L = 29, CH_STEREO_R = 30, CH_WIDE_L = 31, CH_WIDE_R = 32,
    CH_SDL = 33, CH_SDR = 34, CH_LFE2 = 35, CH_TSL = 36, CH_TSR = 37,
    CH_BFC = 38, CH_BFL = 39, CH_BFR = 40,
};

struct Speaker {
    int id = CH_UNKNOWN;
    double az = 0, el = 0;
    bool lfe = false;
};

struct Layout {
    std::string name;
    std::vector<Speaker> speakers;   // dans l'ordre des canaux
    bool headphones = false;
    uint64_t mask() const;
    int indexOf(int id) const;       // -1 si absent
};

// Position nominale d'un canal, en tenant compte des autres canaux présents (contextMask).
bool channelPosition(int id, uint64_t contextMask, double& az, double& el, bool& lfe);
const char* channelName(int id);

Layout layoutFromMask(uint64_t mask, const std::string& name = "");
bool layoutFromName(const std::string& name, Layout& out);   // "2.0", "5.1", "7.1.4", "casque"...
std::string describeMask(uint64_t mask);
std::vector<std::string> knownLayoutNames();

} // namespace s3d
