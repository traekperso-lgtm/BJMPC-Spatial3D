// Spatial3D — interface graphique (Dear ImGui), indépendante de la plateforme.
// L'hôte (Windows : gui_main.cpp) remplit UiModel à chaque image et exécute les UiActions.
#pragma once
#include "model.h"
#include <string>
#include <vector>

namespace s3d {

struct UiDevice {
    std::string name;
    std::wstring id;
    bool headphones = false;
    std::string layoutName;      // disposition détectée (« 7.1 », « casque »…)
};

struct UiModel {
    Settings settings;           // modifiable par l'interface
    std::vector<UiDevice> outputs;
    int outputSel = -1;          // index dans outputs (sortie réellement utilisée)
    bool vbCableInstalled = true;
    bool sofaSupported = false;
    std::string version = "0.7.0";

    // Temps réel
    bool liveRunning = false, liveIdle = true, liveStarting = false;
    double marginMs = 0, lowMs = 0, driftPpm = 0, inHz = 0;
    int underruns = 0, drops = 0, captureGlitches = 0;
    std::string liveError, liveWarning, liveOutputMode, liveCaptureMode;
    double capturePeriodMs = 0, processingMs = 0, outputBufferMs = 0, totalLatencyMs = 0;
    bool followVolume = false, windowsMuted = false;
    double windowsVolumeDb = 0, windowsVolumePct = 100;
    bool volumeDigital = false;
    Scene liveScene;
    float liveIn[32] = {}, liveOut[32] = {};

    Layout previewLayout;        // disposition affichée quand rien ne joue
    int forceTab = -1;           // sélectionne un onglet (0 temps réel, 1 réglages) à l'image suivante
};

struct UiActions {
    bool toggleLive = false;
    bool speakersMoved = false;      // enceintes déplacées à la souris (à appliquer en direct)
    bool virtualMoved = false;       // casque : haut-parleurs virtuels déplacés
    bool browseSofa = false;
    bool settingsChanged = false;    // à sauvegarder
    bool needRestart = false;        // réglage qui exige de relancer le moteur
    bool gainChanged = false;
    bool startWithWindowsChanged = false;
    bool restoreSound = false;
    bool openVbCableSite = false;
};

void uiSetupStyle(float scale);
void uiDraw(UiModel& m, UiActions& a, float scale);

} // namespace s3d
