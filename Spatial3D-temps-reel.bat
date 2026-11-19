@echo off
rem Spatial3D en direct : le son de Windows passe par VB-Cable puis est spatialise vers la vraie sortie.
rem Fermer cette fenetre (ou Ctrl+C) remet la sortie audio normale.
title Spatial3D - temps reel
"%~dp0spatial3d-cli.exe" --temps-reel %*
if errorlevel 1 pause
