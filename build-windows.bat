@echo off
rem ============================================================================
rem  Spatial3D - installation automatique (double-clic)
rem  1. Installe CMake et les outils de compilation C++ Microsoft si absents (winget)
rem  2. Telecharge FFmpeg (build LGPL de BtbN)
rem  3. Compile et teste Spatial3D
rem  4. Propose VB-Cable (carte son virtuelle) pour le mode temps reel
rem  5. Installe dans %LOCALAPPDATA%\Spatial3D + raccourcis (Bureau, Envoyer vers, menu Demarrer)
rem     Spatial3D.exe = interface graphique ; spatial3d-cli.exe = ligne de commande
rem  Options passees a CMake : build-windows.bat -DS3D_WITH_MYSOFA=ON ...
rem ============================================================================
setlocal EnableExtensions EnableDelayedExpansion
title Spatial3D - installation
cd /d "%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSSETUP=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\setup.exe"
set "INSTALL=%LOCALAPPDATA%\Spatial3D"

echo.
echo  ==============================================
echo   Spatial3D - installation automatique
echo  ==============================================
echo.

rem ---------- 0. winget ----------
where winget >nul 2>&1
if errorlevel 1 (
  echo  winget est introuvable.
  echo  Installez "Programme d'installation d'application" depuis le Microsoft Store,
  echo  puis relancez ce fichier.
  goto :err
)

rem ---------- 1. CMake ----------
call :findcmake
if not defined CMAKE (
  echo [1/6] Installation de CMake...
  winget install -e --id Kitware.CMake --source winget --silent --accept-package-agreements --accept-source-agreements
  call :findcmake
)
if not defined CMAKE (
  echo  CMake n'a pas pu etre installe.
  goto :err
)
echo [1/6] CMake : OK

rem ---------- 2. Outils de compilation C++ ----------
call :findvs
if not defined VSPATH (
  echo [2/6] Installation des outils de compilation C++ Microsoft
  echo       environ 3 Go, de 5 a 20 minutes - acceptez la demande d'autorisation Windows
  winget install -e --id Microsoft.VisualStudio.2022.BuildTools --source winget --accept-package-agreements --accept-source-agreements --override "--quiet --wait --norestart --nocache --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
  call :findvs
)
if not defined VSPATH call :addvctools
if not defined VSPATH (
  echo  Les outils C++ ne sont pas disponibles. Si Windows demande un redemarrage,
  echo  redemarrez puis relancez ce fichier.
  goto :err
)
echo [2/6] Outils C++ : OK  (!VSPATH!)

rem ---------- 3. FFmpeg ----------
if not exist "deps\ffmpeg\include" (
  echo [3/6] Telechargement de FFmpeg...
  if not exist deps mkdir deps
  if exist deps\ffmpeg rmdir /s /q deps\ffmpeg
  curl -fL -o deps\ffmpeg.zip https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n7.1-latest-win64-lgpl-shared-7.1.zip
  if errorlevel 1 curl -fL -o deps\ffmpeg.zip https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl-shared.zip
  if errorlevel 1 (
    echo  Telechargement de FFmpeg impossible - verifiez la connexion Internet.
    goto :err
  )
  tar -xf deps\ffmpeg.zip -C deps || goto :err
  for /d %%D in (deps\ffmpeg-*) do move "%%D" deps\ffmpeg >nul
  del deps\ffmpeg.zip
)
echo [3/6] FFmpeg : OK

rem ---------- 4. Compilation + tests ----------
echo [4/6] Compilation...
"%CMAKE%" -S . -B build -A x64 -DFFMPEG_ROOT="%CD%\deps\ffmpeg" %*
if errorlevel 1 goto :err
"%CMAKE%" --build build --config Release
if errorlevel 1 goto :err
"%CTEST%" --test-dir build -C Release --output-on-failure
if errorlevel 1 goto :err
echo [4/6] Compilation et tests : OK

rem ---------- 5. VB-Cable (mode temps reel) ----------
set "REBOOT="
call :hasvbcable
if defined VBCABLE (
  echo [5/6] VB-Cable : deja installe
) else (
  echo.
  echo  Le mode TEMPS REEL spatialise tout le son de Windows (jeux, films, navigateur^).
  echo  Il a besoin de VB-Cable, une carte son virtuelle gratuite de VB-Audio ^(donationware^).
  choice /c ON /m " Installer VB-Cable maintenant"
  if not errorlevel 2 call :installvbcable
)

rem ---------- 6. Installation + raccourcis ----------
echo [6/6] Installation dans %INSTALL%
if not exist "%INSTALL%" mkdir "%INSTALL%"
rem Une version precedente tourne ? On la ferme (la sortie audio est remise juste apres)
set "WASRUNNING="
tasklist /fi "imagename eq Spatial3D.exe" 2>nul | find /i "Spatial3D.exe" >nul && set "WASRUNNING=1"
if defined WASRUNNING taskkill /f /im Spatial3D.exe >nul 2>&1
if defined WASRUNNING timeout /t 1 /nobreak >nul
if not exist build\Release\Spatial3D.exe (echo  Introuvable : build\Release\Spatial3D.exe & goto :err)
if not exist build\Release\spatial3d-cli.exe (echo  Introuvable : build\Release\spatial3d-cli.exe & goto :err)
copy /y build\Release\Spatial3D.exe "%INSTALL%\" >nul || (echo  Copie impossible de Spatial3D.exe : fichier utilise ? & goto :err)
copy /y build\Release\spatial3d-cli.exe "%INSTALL%\" >nul || (echo  Copie impossible de spatial3d-cli.exe : fichier utilise ? & goto :err)
copy /y build\Release\*.dll "%INSTALL%\" >nul
copy /y Spatial3D-lancer.bat "%INSTALL%\" >nul
copy /y Spatial3D-temps-reel.bat "%INSTALL%\" >nul
copy /y README.md "%INSTALL%\" >nul
for %%F in (*.sofa) do copy /y "%%F" "%INSTALL%\" >nul
if defined WASRUNNING "%INSTALL%\spatial3d-cli.exe" --restaurer >nul 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\raccourcis.ps1 -Install "%INSTALL%"

echo.
echo  ==============================================
echo   Installation terminee
echo  ==============================================
echo   Lancez "Spatial3D" depuis le Bureau ou le menu Demarrer.
echo   Clic droit sur un film ^> Envoyer vers ^> Spatial3D pour le lire en 3D.
if defined REBOOT (
  echo.
  echo   *** REDEMARREZ l'ordinateur avant d'utiliser le mode temps reel ***
) else (
  choice /c ON /m " Lancer Spatial3D maintenant"
  if not errorlevel 2 start "" "%INSTALL%\Spatial3D.exe"
)
echo.
pause
exit /b 0

rem ============================================================================
:findcmake
set "CMAKE="
set "CTEST="
for /f "delims=" %%C in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%C"
if not defined CMAKE if exist "%ProgramFiles%\CMake\bin\cmake.exe" set "CMAKE=%ProgramFiles%\CMake\bin\cmake.exe"
if defined CMAKE for %%I in ("!CMAKE!") do set "CTEST=%%~dpIctest.exe"
exit /b 0

:findvs
set "VSPATH="
if exist "%VSWHERE%" for /f "usebackq delims=" %%P in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%P"
exit /b 0

rem Visual Studio present mais sans le C++ : on ajoute la charge de travail (droits admin)
:addvctools
if not exist "%VSWHERE%" exit /b 0
set "ANYVS="
for /f "usebackq delims=" %%P in (`call "%VSWHERE%" -latest -products * -property installationPath`) do set "ANYVS=%%P"
if not defined ANYVS exit /b 0
echo       Ajout du C++ a l'installation Visual Studio existante...
powershell -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '%VSSETUP%' -ArgumentList 'modify','--installPath','\"%ANYVS%\"','--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended','--quiet','--norestart'"
call :findvs
exit /b 0

:hasvbcable
set "VBCABLE="
powershell -NoProfile -Command "if (Get-CimInstance Win32_SoundDevice | Where-Object { $_.Name -like '*VB-Audio Virtual Cable*' }) { exit 0 } else { exit 1 }"
if not errorlevel 1 set "VBCABLE=1"
exit /b 0

:installvbcable
echo       Telechargement de VB-Cable...
set "VBDIR=%TEMP%\Spatial3D-vbcable"
if exist "%VBDIR%" rmdir /s /q "%VBDIR%"
mkdir "%VBDIR%"
curl -fL -o "%VBDIR%\vbcable.zip" https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip
if errorlevel 1 (
  echo       Telechargement impossible : installez-le depuis https://vb-audio.com/Cable/
  exit /b 0
)
tar -xf "%VBDIR%\vbcable.zip" -C "%VBDIR%"
set "VBSETUP=%VBDIR%\VBCABLE_Setup_x64.exe"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" for %%E in ("%VBDIR%\*arm64*.exe") do set "VBSETUP=%%~fE"
if not exist "%VBSETUP%" (
  echo       Programme d'installation VB-Cable introuvable dans l'archive.
  exit /b 0
)
echo       Acceptez l'autorisation administrateur PUIS l'installation du pilote VB-Audio.
powershell -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '%VBSETUP%' -ArgumentList '-i','-h'"
call :hasvbcable
if defined VBCABLE (
  echo [5/6] VB-Cable : installe
) else (
  echo [5/6] VB-Cable : installe - visible apres redemarrage
)
set "REBOOT=1"
exit /b 0

:err
echo.
echo  *** ECHEC de l'installation - voir les messages ci-dessus ***
echo.
pause
exit /b 1
