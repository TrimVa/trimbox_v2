@echo off
rem Lance la generation des voix TrimBox (Azure fr-FR-DeniseNeural)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0generer-voix.ps1"
