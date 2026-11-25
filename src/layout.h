// Spatial3D — dispositions de haut-parleurs
// Convention : azimut en degrés, positif = vers la GAUCHE, 0 = devant.
//              élévation en degrés, positive = vers le haut.
#pragma once
#include <cstdint>
#include <map>
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

constexpr double kDefaultSpeakerDist = 2.5;   // distance d'écoute par défaut (m)
constexpr double kMinSpeakerDist = 0.5, kMaxSpeakerDist = 5.0;

struct Speaker {
    int id = CH_UNKNOWN;
    double az = 0, el = 0;
    double dist = kDefaultSpeakerDist;   // distance réelle à l'auditeur (m), réglable à la souris
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

// Positions réelles des enceintes, réglées à la souris dans l'interface.
// Texte « 5.1|0:35@2.10,1:-35;7.1|9:100 » : par disposition, identifiant de canal -> azimut en degrés
// et, après @, distance en mètres (absente = distance par défaut).
struct SpeakerPos { double az = 0, dist = kDefaultSpeakerDist; };
using SpeakerOverrides = std::map<std::string, std::map<int, SpeakerPos>>;
SpeakerOverrides parseSpeakerOverrides(const std::string& spec);
std::string formatSpeakerOverrides(const SpeakerOverrides& o);
void applySpeakerOverrides(Layout& l, const std::string& spec);   // sans effet au casque
// Azimut acceptable pour l'enceinte i (au moins 5° de ses voisines de même étage) ?
bool speakerMoveAllowed(const Layout& l, int i, double az);

// Alignement des distances (comme un ampli home-cinéma) : les enceintes plus proches sont retardées
// et atténuées pour que tous les sons arrivent ensemble et au même niveau à la place d'écoute.
struct DistanceAlignment { std::vector<double> delaySec, gain; bool active = false; };
DistanceAlignment computeDistanceAlignment(const Layout& l);

} // namespace s3d
