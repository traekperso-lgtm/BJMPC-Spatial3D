@echo off
rem Lanceur Spatial3D : glissez un fichier dessus, ou "Envoyer vers".
rem Sans fichier : affiche l'aide.
title Spatial3D
"%~dp0spatial3d-cli.exe" %*
echo.
pause
