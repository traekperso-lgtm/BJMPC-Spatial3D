# Spatial3D — spatialisation 3D pour Windows

**BJMPC Informatique** · v0.4.0 · C++17

Spatial3D regarde comment le PC est équipé (casque, stéréo, 5.1, 7.1, 7.1.4…) et place chaque son au
meilleur endroit possible sur **ce** matériel.

## L'application

Lancez **Spatial3D** (Bureau ou menu Démarrer). Trois onglets :

- **Temps réel** : un bouton « Activer le son 3D » spatialise tout le son de Windows (jeux, films,
  navigateur, musique) via VB-Cable. À droite, la **vue de la scène** vue de dessus : les sources
  (points orange) et les enceintes (carrés ; violet = hauteur) s'allument avec le son, les traits
  montrent quelle source alimente quelle enceinte. En dessous, les VU-mètres d'entrée et de sortie.
  L'état indique la latence ajoutée, la dérive d'horloge, la fréquence reçue et les coupures.
- **Lecteur** : ouvrez un film ou une musique (ou glissez-le sur la fenêtre, ou clic droit sur un
  fichier > Envoyer vers > Spatial3D). Lecture/pause, barre de progression cliquable.
- **Réglages** : sortie, disposition (automatique, casque, 5.1…), étalement, pièce virtuelle,
  HRTF `.sofa`, marge du temps réel, démarrage avec Windows, son 3D activé au lancement,
  fermeture dans la zone de notification.

Fermer la fenêtre la réduit dans la **zone de notification** (icône près de l'horloge : clic pour
rouvrir, clic droit pour activer/arrêter le son 3D ou quitter). **Quitter remet toujours la vraie
carte son en sortie par défaut.**

La ligne de commande reste disponible : `spatial3d-cli` (voir plus bas).

## Comment il décide du placement

1. **Lecture du fichier** (FFmpeg) : codec, nombre de canaux et surtout la *disposition déclarée*
   (5.1 côtés ou arrières, 7.1, 7.1.4, Ambisonique…). Chaque canal devient une **source virtuelle**
   placée à sa position normalisée (ITU-R BS.775 / BS.2051) : avant ±30°, surround 5.1 à ±110°,
   côtés 7.1 à ±90°, arrières à ±135°, hauteurs à 45° d'élévation…
2. **Lecture de la configuration Windows** (WASAPI) : périphérique par défaut, type (casque, enceintes,
   HDMI), format de mixage et la configuration choisie dans *Panneau de configuration > Son > Configurer*.
3. **Placement** :
   - **Enceintes** → **VBAP** (Vector Base Amplitude Panning). Chaque source est jouée par la paire
     (ou le triangle, s'il y a des enceintes en hauteur) d'enceintes qui l'entoure, avec une puissance
     constante. Quand l'enceinte existe à la bonne place, le canal passe tel quel ; sinon une image
     fantôme est créée au bon angle. Des enceintes *imaginaires* ferment la sphère (sons sous l'horizon,
     zénith sans plafonnier). En stéréo, l'arrière est replié vers l'avant comme un downmix classique.
   - **Casque** → **binaural**. Chaque source virtuelle reçoit sa propre HRTF :
     modèle de tête sphérique (Brown & Duda : différence de temps entre oreilles, ombre de la tête,
     échos du pavillon pour la hauteur, assombrissement de l'arrière), ou **HRTF mesurée** au format
     `.sofa` si l'exe a été compilé avec libmysofa. Une petite pièce virtuelle (réverbération FDN)
     sort le son de la tête.
   - **LFE** → caisson s'il existe, sinon avant gauche/droite à −6 dB.
4. **Sécurité** : limiteur crête en sortie (pas de saturation quand on replie 12 canaux sur 2).

### Fichiers Ambisonique
Les fichiers **AmbiX** (ACN/SN3D, vidéos 360° YouTube, enregistrements Zoom H3-VR…) d'ordre 1 ou 2
sont décodés sur 12 directions uniformes (icosaèdre, pondération max-rE), elles-mêmes placées par VBAP
ou HRTF : c'est la méthode **AllRAD**, qui marche sur n'importe quelle disposition, même irrégulière.
FFmpeg reconnaît l'Ambisonique dans les MP4 (boîte SA3D) ; sinon forcez avec `--ambisonique`.

## Mode temps réel (tout le son de Windows)

```
Applis / jeux ──7.1──▶ CABLE Input ──▶ CABLE Output ──▶ Spatial3D ──▶ vraie sortie
(sortie par défaut)    (VB-Cable)                       VBAP / HRTF   (casque, 5.1, 7.1…)
```

Windows mélange le son de toutes les applis au format de la sortie *avant* qu'on puisse le récupérer :
au casque, il ne reste que de la stéréo, sans position. Spatial3D règle donc **VB-Cable en 7.1**
et en fait la sortie par défaut : les jeux et films y envoient du vrai 7.1, que Spatial3D capte et
spatialise vers la vraie carte son.

Au lancement (bouton « Activer le son 3D », ou `spatial3d-cli --temps-reel`) :
1. VB-Cable est passé en 7.1 si besoin (une autorisation administrateur peut être demandée la 1re fois) ;
2. la sortie actuelle est mémorisée comme « vraie sortie », VB-Cable devient la sortie par défaut ;
3. le son est capté, spatialisé et envoyé à la vraie sortie ;
4. **fermer la fenêtre (ou Ctrl+C) remet la vraie sortie par défaut.** Après un plantage :
   menu Démarrer > Spatial3D > *Spatial3D - retablir le son* (`spatial3d-cli --restaurer`), ou
   Réglages > « Rétablir le son normal ».

La ligne d'état affiche la marge de sécurité, le point bas du tampon, la dérive d'horloge estimée,
la fréquence réellement reçue de VB-Cable et les coupures :

```
  En direct | marge  20 ms (mini 18.7) | derive  +42 ppm | entree 48002 Hz | coupures 1 | pertes VB-Cable 0
```

VB-Cable livre le son par paquets irréguliers, et deux cartes son n'ont jamais exactement la même
horloge. Ce que fait Spatial3D pour éviter les pops :

- **rééchantillonnage continu** piloté par un régulateur PI qui vise le *point bas* du tampon (pas sa
  moyenne) ; correction plafonnée à ±0,2 % (inaudible) ;
- **marge auto-ajustée** : chaque coupure ajoute 10 ms, et la marge apprise est **mémorisée** pour le
  lancement suivant ;
- **fondus de 2 ms** à chaque discontinuité : si le tampon se vide, le son s'éteint en douceur puis
  revient en fondu ; s'il déborde (après une pause ou une sortie de veille), fondu enchaîné ;
- **limiteur à anticipation** (2 ms) : le gain baisse *avant* une crête, sans saut ;
- **priorité « Pro Audio »** (MMCSS) pour les fils audio : un jeu qui charge le processeur ne les
  retarde plus ;
- **sortie WASAPI exclusive** en option (Réglages > Temps réel, ou `--exclusif`) : Spatial3D parle
  directement à la carte son, sans le mélangeur de Windows ; repli automatique en partagé si la carte
  refuse (déjà utilisée, exclusif interdit dans ses propriétés).

La ligne « Pertes VB-Cable » compte les trous signalés par Windows *avant* Spatial3D : si elle monte,
c'est VB-Cable qui décroche (augmentez sa latence dans son panneau, *VB-Audio Virtual Cable Control
Panel*). Simulations : 10 min, paquets de 0 à 40 ms et dérive de 150 ppm → 1 coupure au démarrage puis
aucune ; calages et afflux provoqués → coupures et sauts sans aucun clic.

Options : `--vers "Realtek"` (choisir la vraie sortie, mémorisée), `--latence 40` (marge de départ plus
grande), `--sortie casque`, `--garder-sortie` (ne pas toucher à la sortie par défaut),
`--peripheriques` (liste des périphériques), `--exclusif` (sortie WASAPI exclusive).

Bon à savoir :
- Latence totale ≈ 30–50 ms : parfait pour films, musique et la plupart des jeux ; un peu juste
  pour du jeu compétitif.
- Les applis classiques sortent au plus du **7.1** : les hauteurs (7.1.4) ne sont pas transmises
  par VB-Cable. Elles restent disponibles en mode fichier.
- La plupart des applis suivent le changement de sortie par défaut ; sinon, relancez-les.
- Si VB-Cable refuse le réglage automatique : Panneau de configuration > Son > *CABLE Input* >
  Configurer > 7.1, puis onglet Enregistrement > *CABLE Output* > Propriétés > Avancé > 8 canaux.

## Ligne de commande (`spatial3d-cli`)

```
spatial3d-cli <fichier> [options]

  --sortie <disposition>  auto (defaut), casque, 2.0, 2.1, 3.0, quad, 5.0, 5.1, 7.1,
                          5.1.2, 5.1.4, 7.1.2, 7.1.4
  --wav <fichier.wav>     rendu dans un fichier au lieu de la lecture
  --etalement <degres>    elargit chaque source (0-90)
  --piece <0..1>          piece virtuelle au casque (defaut 0.15)
  --sofa <fichier.sofa>   HRTF mesuree pour le casque
  --ambisonique           force l'interpretation AmbiX
  --volume <dB>           gain general
  --info                  analyse + plan de placement, sans jouer
  --dispositions          liste les dispositions connues
```

Exemples :

```
spatial3d-cli concert.flac --info
spatial3d-cli film.mkv --sortie casque --sofa default.sofa
spatial3d-cli film.mkv --sortie 5.1 --wav film_51.wav
spatial3d-cli video360.mp4 --sortie casque
```

## Compiler

### Le plus simple : GitHub Actions
Poussez le dossier dans un dépôt GitHub : le workflow `.github/workflows/windows.yml` compile l'exe
(avec HRTF SOFA), lance les tests et publie un zip `Spatial3D-win64` (exe + DLL FFmpeg + fichiers `.sofa`)
dans l'onglet **Actions**.

### En local : double-clic sur `build-windows.bat`
Le script fait tout, sans rien installer à la main :

1. installe **CMake** et les **outils de compilation C++ Microsoft** s'ils manquent (via `winget`,
   environ 3 Go, Windows demande une fois l'autorisation administrateur) ;
2. télécharge **FFmpeg** (build LGPL de BtbN) ;
3. compile et lance les tests ;
4. propose d'installer **VB-Cable** pour le mode temps réel (Windows demande d'accepter le pilote,
   puis il faut **redémarrer**) ;
5. installe le programme dans `%LOCALAPPDATA%\Spatial3D` et crée les raccourcis **Spatial3D**
   (Bureau, menu Démarrer) et **Spatial3D (son 3D)** dans *Envoyer vers*. Une version déjà ouverte
   est fermée puis la sortie audio normale est remise.

Ensuite : lancez Spatial3D depuis le Bureau. Relancer le script met à jour l'installation.

> Si le dossier vient d'un zip téléchargé, faites d'abord **clic droit sur le zip > Propriétés >
> Débloquer**, sinon Windows SmartScreen bloque le `.bat`.
> Prérequis : Windows 10 (1809+) ou 11 avec `winget` (« Programme d'installation d'application »
> du Microsoft Store, présent par défaut sur Windows 11).

HRTF SOFA en local : `vcpkg install libmysofa:x64-windows` puis
`build-windows.bat -DS3D_WITH_MYSOFA=ON -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake`
(le zip produit par GitHub Actions inclut déjà le support SOFA).

Sous Linux (développement du moteur, rendu `--wav` uniquement) :
`cmake -S . -B build && cmake --build build && ./build/s3d_tests`

## Architecture

| Fichier | Rôle |
|---|---|
| `src/gui_main.cpp` | Application Windows : fenêtre Direct3D 11, zone de notification, glisser-déposer, instance unique |
| `src/ui.*` | Interface Dear ImGui (portable) : onglets, vue de la scène, VU-mètres, réglages |
| `src/engine.*` | Moteurs pilotables : `LiveEngine` (temps réel) et `FilePlayer` (lecture, pause, recherche) ; réglages persistants |
| `src/decoder.*` | Lecture/décodage FFmpeg, disposition des canaux, rééchantillonnage à la fréquence de Windows |
| `src/layout.*` | Dispositions (masques identiques à `SPEAKER_*` de Windows et `AV_CHAN_*` de FFmpeg) et positions normalisées |
| `src/sources.*` | Sources virtuelles : 1 par canal, ou décodage Ambisonique |
| `src/vbap.*` | VBAP 2D/3D, enveloppe convexe, enceintes imaginaires, étalement MDAP |
| `src/renderer.*` | Rendu enceintes (matrice de gains) et casque (HRTF paramétrique ou SOFA par FFT) |
| `src/dsp.*` | Pièce virtuelle FDN, limiteur |
| `src/wasapi_output.*` | Périphériques Windows, lecture et capture WASAPI, réglage 7.1 et sortie par défaut (IPolicyConfig) |
| `src/live.*` | Mode temps réel : VB-Cable → moteur → vraie sortie, restauration à la fermeture |
| `src/live_core.*` | Tampon, rééchantillonneur adaptatif, régulateur de dérive d'horloge (portable, testé) |
| `tests/test_core.cpp` | 38 vérifications (identités, downmix, hauteurs, Ambisonique, ITD/ILD, dérive d'horloge, limiteur, absence de clics) |

## Limites connues

- **Menu « Son spatial » de Windows** : Windows n'autorise pas un éditeur indépendant à y ajouter son
  format (réservé à Microsoft, Dolby, DTS). Spatial3D est donc un lecteur/moteur, pas une entrée de ce menu.
  En mode casque, **désactivez Windows Sonic / Dolby Atmos for Headphones** pour éviter un double traitement.
- **Dolby Atmos / DTS:X** : FFmpeg ne décode que le « lit » 7.1 des pistes TrueHD Atmos et E-AC-3 JOC ;
  les objets (et leurs métadonnées de position) ne sont pas accessibles sans licence Dolby/DTS.
- Mode SOFA : latence d'un bloc (~21 ms à 48 kHz) ; les dernières millisecondes du fichier sont coupées.
- Pas encore de sélection de piste audio : la « meilleure » piste du fichier est prise.

## Pistes pour la suite

- Calibration : angles et distances réels des enceintes (retards et niveaux)
- Suivi de tête (webcam ou capteur) pour le casque
- Pilote de carte son virtuelle maison (sans VB-Cable, jusqu'au 7.1.4) — nécessite une signature Microsoft
