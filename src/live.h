// Spatial3D — mode temps réel en ligne de commande (Windows)
#pragma once
#include "engine.h"
#include <atomic>
#include <string>

namespace s3d {

struct LiveArgs {
    std::string inputName;        // (réservé) autre entrée que VB-Cable
    std::string outputName;       // vraie sortie (partie du nom ; mémorisée)
    std::string sortie = "auto";  // disposition forcée
    std::string sofa;
    double spread = 0, room = -1, gainDb = 0;
    int latencyMs = 6;            // marge initiale (augmente seule après une coupure)
    bool keepDefault = false;     // ne pas basculer la sortie par défaut de Windows sur VB-Cable
    bool exclusive = false;       // sortie WASAPI exclusive
    bool standardLatency = false; // désactive la période WASAPI minimale
};

int runLive(const LiveArgs& a, std::atomic<bool>& stop);
int listDevices();
int restoreOutput();

} // namespace s3d
