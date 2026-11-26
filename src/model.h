// Spatial3D — structures partagées par le moteur et l'interface (portables)
#pragma once
#include "layout.h"
#include "sources.h"
#include <string>
#include <vector>

namespace s3d {

struct Settings {
    std::wstring outputId;            // vraie sortie imposée (ligne de commande --vers, non mémorisée) ;
                                      // vide = celle de Windows (voir resolveOutput)
    std::string layout = "auto";      // auto, casque, 2.0, 5.1, 7.1, 7.1.4...
    double spreadDeg = 0;
    double room = -1;                 // -1 = auto (15 % au casque, 0 sur enceintes)
    double gainDb = 0;
    std::string sofaPath;
    int marginMs = 6;
    bool keepDefault = false;         // temps réel : ne pas basculer Windows sur VB-Cable
    bool exclusive = false;           // temps réel : sortie WASAPI en mode exclusif
    bool lowLatency = true;           // période WASAPI minimale (IAudioClient3 en partagé)
    std::string speakerPos;           // positions réelles des enceintes (voir parseSpeakerOverrides)
    bool followCableVolume = true;    // appliquer le volume / la sourdine Windows de CABLE Input
    // préférences de l'interface
    bool liveOnLaunch = false;
    bool closeToTray = true;
    bool startWithWindows = false;
};

// Description de la scène pour l'affichage
struct Scene {
    Layout out;
    std::vector<VirtualSource> sources;
    std::vector<float> matrix;        // gains sortie x entrée (vide au casque)
    std::vector<int> inIds;           // identité de chaque canal d'entrée (vide = Ambisonique)
    int inChannels = 0, outChannels = 0;
    std::string inputName, outputName, plan;
};

} // namespace s3d
