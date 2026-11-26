# Spatial3D — spatialisation 3D pour Windows

**BJMPC Informatique** · v0.6.1 · C++17

Spatial3D regarde comment le PC est équipé (casque, stéréo, 5.1, 7.1, 7.1.4…) et place chaque son de
Windows au meilleur endroit possible sur **ce** matériel, en temps réel. Le moteur est conçu pour être
**le plus léger possible, avec la plus faible latence**, sans rien céder sur la fidélité : aucune
dépendance lourde (plus de FFmpeg), fils audio sans verrou ni allocation, convolution HRTF partitionnée
et périodes WASAPI minimales.

## L'application

Lancez **Spatial3D** (Bureau ou menu Démarrer). Deux onglets :

- **Temps réel** : un bouton « Activer le son 3D » spatialise tout le son de Windows (jeux, films,
  navigateur, musique) via VB-Cable. À droite, la **vue de la scène** vue de dessus : les sources
  (points orange) et les enceintes (carrés ; violet = hauteur) s'allument avec le son, les traits
  montrent quelle source alimente quelle enceinte. La vue est à l'échelle (anneaux de 1 à 4 m).
  **Glissez une enceinte** pour la placer comme dans votre pièce, en **angle et en distance** : le
  placement est recalculé en direct (fondu enchaîné, sans clic) et mémorisé par disposition ; les
  distances différentes sont **compensées comme sur un ampli home-cinéma** (les enceintes plus proches
  sont retardées et baissées pour que tout arrive ensemble et au même niveau à votre place).
  Double-clic ou « Positions d'origine » pour revenir aux angles ITU à 2,5 m.
  En dessous, les VU-mètres d'entrée et de sortie.
  L'état indique la **latence estimée de bout en bout**, la marge, la dérive d'horloge, la fréquence
  reçue, les coupures et les modes WASAPI obtenus (capture et sortie).
  La sortie audio et la disposition (automatique, casque, 5.1…) se choisissent ici.
- **Réglages** : l'essentiel seulement — WASAPI (latence minimale, sortie exclusive, bascule de
  la sortie Windows) et démarrage (avec Windows, son 3D au lancement, zone de notification).
  Le reste est réglé pour la fidélité, sans curseur : image nette (aucun étalement), pièce virtuelle
  automatique au casque seulement, marge apprise toute seule, volume réglé dans Windows et
  synchronisé avec la carte. En ligne de commande, `--etalement`, `--piece` et `--sofa` restent
  disponibles.

Pour lire un film ou une musique, utilisez votre lecteur habituel (par exemple BJMPC Player) :
Spatial3D spatialise tout ce que joue Windows.

Fermer la fenêtre la réduit dans la **zone de notification** (icône près de l'horloge : clic pour
rouvrir, clic droit pour activer/arrêter le son 3D ou quitter). **Quitter remet toujours la vraie
carte son en sortie par défaut.**

La ligne de commande reste disponible : `spatial3d-cli` (voir plus bas).

## Comment il décide du placement

1. **Lecture de l'entrée** : nombre de canaux et *disposition déclarée* (format 7.1 de VB-Cable, ou
   masque de canaux d'un fichier WAV). Chaque canal devient une **source virtuelle**
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
   - **Casque** → **binaural mesuré**. Chaque source virtuelle reçoit la réponse d'une vraie tête :
     l'**HRTF KEMAR** (MIT Media Lab, 710 directions) est **intégrée au programme**, égalisée en champ
     diffus (le timbre reste neutre, seule la direction change). Un fichier `.sofa` d'une autre tête
     peut la remplacer (version compilée avec libmysofa). À chaque haut-parleur virtuel s'ajoutent les
     **réflexions précoces d'un salon** (murs, sol, plafond, méthode des sources images, chacune avec
     sa propre direction HRTF), puis une réverbération FDN discrète : le son sort de la tête au lieu
     de rester « dans le crâne ». Le tout est appliqué par **convolution partitionnée uniforme** (blocs
     de 128 échantillons : 2,7 ms de latence, les deux oreilles en une seule FFT complexe ; environ 2 %
     d'un cœur pour du 7.1).
   - **LFE** → caisson s'il existe, sinon avant gauche/droite à −6 dB.
4. **Sécurité** : limiteur crête en sortie (pas de saturation quand on replie 12 canaux sur 2).

### Fichiers Ambisonique
Les fichiers **AmbiX** (ACN/SN3D, vidéos 360° YouTube, enregistrements Zoom H3-VR…) d'ordre 1 ou 2
sont décodés sur 12 directions uniformes (icosaèdre, pondération max-rE), elles-mêmes placées par VBAP
ou HRTF : c'est la méthode **AllRAD**, qui marche sur n'importe quelle disposition, même irrégulière.
En ligne de commande, forcez l'interprétation AmbiX d'un WAV 4 ou 9 canaux avec `--ambisonique`.

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
  moyenne) ; correction plafonnée à ±0,2 % (inaudible). Le rééchantillonneur est un **sinc fenêtré
  Kaiser polyphase** (48 points) : réponse plate à ±0,001 dB jusqu'à 18 kHz et rapport signal/bruit
  de 93 à 98 dB, là où l'ancienne interpolation cubique tombait à 29 dB à 10 kHz (−2,3 dB à 18 kHz) ;
- **marge auto-ajustée** : départ à 6 ms, chaque coupure l'augmente, et la marge apprise est
  **mémorisée** pour le lancement suivant ;
- **fondus de 2 ms** à chaque discontinuité : si le tampon se vide, le son s'éteint en douceur puis
  revient en fondu ; s'il déborde (après une pause ou une sortie de veille), fondu enchaîné ;
- **volume synchronisé avec la vraie carte** : VB-Cable ignore le curseur de volume et le bouton
  sourdine de *CABLE Input*. Spatial3D recopie curseur et sourdine sur la sortie réelle **dans les deux
  sens** (barre des tâches, touches du clavier, panneau de la carte) : même position, même sourdine,
  et c'est la carte qui règle le volume, comme sans Spatial3D. En exclusif sur une carte sans volume
  matériel, Spatial3D l'applique lui-même avec une rampe sans clic (option dans Réglages) ;
- **limiteur à anticipation** (1,5 ms) : le gain baisse *avant* une crête, sans saut ;
- **transfert capture → rendu sans verrou** (anneau à un producteur et un consommateur) et **aucune
  allocation** dans les fils audio ; dénormaux désactivés (FTZ/DAZ) pour éviter les pics de calcul ;
- **latence minimale** (activée par défaut, Réglages > Temps réel, ou `--latence-standard` pour la
  couper) : sous Windows 10/11, capture et sortie partagées passent par **IAudioClient3** à la période
  la plus courte du pilote (souvent 2,7 ms au lieu de 10 ms) ; en exclusif, période matérielle minimale.
  Repli automatique sur les périodes standard si le pilote ne le permet pas ;
- **priorité « Pro Audio »** (MMCSS) pour les fils audio : un jeu qui charge le processeur ne les
  retarde plus ;
- **sortie WASAPI exclusive** en option (Réglages > Temps réel, ou `--exclusif`) : Spatial3D parle
  directement à la carte son, sans le mélangeur de Windows ; repli automatique en partagé si la carte
  refuse (déjà utilisée, exclusif interdit dans ses propriétés).

**Vérifier le mode WASAPI** : l'état (onglet Temps réel) affiche le format réellement envoyé, par exemple
« exclusif — entier 24 bits, 48000 Hz, 2 canaux, période 3.0 ms (directement à la carte son) » ou
« partagé faible latence (IAudioClient3) — flottant 32 bits, 48000 Hz, 2 canaux, période 2.7 ms… ».
La ligne « Latence estimée » additionne période de capture + marge + traitement (bloc HRTF, limiteur) +
tampon de sortie ; survolez-la pour le détail. En exclusif, Spatial3D
disparaît aussi du mélangeur de volume de Windows et les autres sons de cette carte se taisent.

La ligne « Pertes VB-Cable » compte les trous signalés par Windows *avant* Spatial3D : si elle monte,
c'est VB-Cable qui décroche (augmentez sa latence dans son panneau, *VB-Audio Virtual Cable Control
Panel*). Simulations : 10 min, paquets de 0 à 40 ms et dérive de 150 ppm → 1 coupure au démarrage puis
aucune ; calages et afflux provoqués → coupures et sauts sans aucun clic.

Options : `--vers "Realtek"` (choisir la vraie sortie, mémorisée), `--latence 40` (marge de départ plus
grande), `--sortie casque`, `--garder-sortie` (ne pas toucher à la sortie par défaut),
`--peripheriques` (liste des périphériques), `--exclusif` (sortie WASAPI exclusive),
`--latence-standard` (périodes WASAPI de 10 ms).

Bon à savoir :
- Latence de bout en bout typique : ≈ 15–25 ms avec un pilote compatible faible latence (affichée en
  direct), ≈ 30–40 ms en périodes standard. VB-Cable ajoute sa propre latence interne, réglable dans
  son panneau.
- Les applis classiques sortent au plus du **7.1** : les hauteurs (7.1.4) ne sont pas transmises
  par VB-Cable. Elles restent disponibles pour un rendu de fichier WAV en ligne de commande.
- La plupart des applis suivent le changement de sortie par défaut ; sinon, relancez-les.
- Si VB-Cable refuse le réglage automatique : Panneau de configuration > Son > *CABLE Input* >
  Configurer > 7.1, puis onglet Enregistrement > *CABLE Output* > Propriétés > Avancé > 8 canaux.

## Ligne de commande (`spatial3d-cli`)

```
spatial3d-cli <fichier.wav> [options]

  Lit les WAV PCM 8/16/24/32 bits et flottants 32/64 bits, y compris WAVE_FORMAT_EXTENSIBLE
  (masque de canaux 5.1, 7.1, 7.1.4…). Pour un autre format, convertissez d'abord en WAV.

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
spatial3d-cli concert.wav --info
spatial3d-cli film_71.wav --sortie casque --sofa default.sofa
spatial3d-cli film_714.wav --sortie 5.1 --wav film_51.wav
spatial3d-cli ambix.wav --ambisonique --sortie casque
spatial3d-cli --temps-reel                 (son de Windows en direct, sans interface)
```

## Compiler

### Le plus simple : GitHub Actions
Poussez le dossier dans un dépôt GitHub : le workflow `.github/workflows/windows.yml` compile l'exe
(avec HRTF SOFA), lance les tests et publie un zip `Spatial3D-win64` (exe + fichiers `.sofa`) dans
l'onglet **Actions** ; chaque nouvelle version crée aussi une *Release*.

### En local : double-clic sur `build-windows.bat`
Le script fait tout, sans rien installer à la main :

1. installe **CMake** et les **outils de compilation C++ Microsoft** s'ils manquent (via `winget`,
   environ 3 Go, Windows demande une fois l'autorisation administrateur) ;
2. compile et lance les tests ;
3. propose d'installer **VB-Cable** pour le mode temps réel (Windows demande d'accepter le pilote,
   puis il faut **redémarrer**) ;
4. installe le programme dans `%LOCALAPPDATA%\Spatial3D` et crée les raccourcis **Spatial3D**
   (Bureau, menu Démarrer). Une version déjà ouverte est fermée puis la sortie audio normale est
   remise ; les anciennes DLL FFmpeg et l'ancien raccourci *Envoyer vers* sont supprimés.

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
| `src/gui_main.cpp` | Application Windows : fenêtre Direct3D 11 (rafraîchissement économe), zone de notification, instance unique |
| `src/ui.*` | Interface Dear ImGui (portable) : onglets, vue de la scène, VU-mètres, réglages |
| `src/engine.*` | `LiveEngine` (temps réel, anneau sans verrou, latence estimée) ; réglages persistants |
| `src/wav_reader.*` / `wav_writer.*` | Lecture et écriture WAV (PCM/flottant, EXTENSIBLE) pour la ligne de commande |
| `src/layout.*` | Dispositions (masques identiques à `SPEAKER_*` de Windows) et positions normalisées |
| `src/sources.*` | Sources virtuelles : 1 par canal, ou décodage Ambisonique |
| `src/vbap.*` | VBAP 2D/3D, enveloppe convexe, enceintes imaginaires, étalement MDAP |
| `src/renderer.*` | Rendu enceintes (matrice de gains) et casque (HRTF mesurée + réflexions précoces) |
| `src/hrtf_builtin.*` | HRTF KEMAR intégrée (`res/hrtf_kemar_48k.bin`, fabriquée par `tools/make_hrtf.py`) |
| `src/convolver.*` | FFT et convolution partitionnée uniforme (overlap-save, bloc de 128) |
| `src/dsp.*` | Pièce virtuelle FDN, limiteur à anticipation, FTZ/DAZ |
| `src/wasapi_output.*` | Périphériques Windows, lecture et capture WASAPI (IAudioClient3 faible latence, exclusif), réglage 7.1 et sortie par défaut (IPolicyConfig) |
| `src/live.*` | Mode temps réel : VB-Cable → moteur → vraie sortie, restauration à la fermeture |
| `src/live_core.*` | Anneau SPSC, rééchantillonneur sinc polyphase, régulateur de dérive d'horloge (portable, testé) |
| `tests/test_core.cpp` | 61 vérifications (HRTF, distances, fidélité du rééchantillonneur, positions d’enceintes, identités, downmix, hauteurs, Ambisonique, ITD/ILD, dérive, limiteur, absence de clics, anneau, convolution, WAV) |

## Limites connues

- **Menu « Son spatial » de Windows** : Windows n'autorise pas un éditeur indépendant à y ajouter son
  format (réservé à Microsoft, Dolby, DTS). Spatial3D est donc un moteur à part, pas une entrée de ce menu.
  En mode casque, **désactivez Windows Sonic / Dolby Atmos for Headphones** pour éviter un double traitement.
- **Dolby Atmos / DTS:X** : les applis ne transmettent que le « lit » 7.1 ; les objets (et leurs
  métadonnées de position) ne sont pas accessibles sans licence Dolby/DTS.
- Mode SOFA : latence d'un bloc (2,7 ms à 48 kHz).

## Pistes pour la suite

- Calibration : angles et distances réels des enceintes (retards et niveaux)
- Suivi de tête (webcam ou capteur) pour le casque
- Pilote de carte son virtuelle maison (sans VB-Cable, jusqu'au 7.1.4) — nécessite une signature Microsoft

## Crédits

HRTF : *Bill Gardner et Keith Martin, « HRTF Measurements of a KEMAR Dummy-Head Microphone », MIT Media
Lab, 1994* — données fournies sans restriction d'usage, citation des auteurs demandée.
