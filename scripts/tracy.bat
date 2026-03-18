@echo off
:: Launch the Tracy profiler GUI.
:: Run this first, then launch the game — Tracy connects automatically.
::
:: Requires the game to be built with -DTRACY_ENABLE=ON (set in .vscode/settings.json).
:: Tracy binary lives in tools/tracy/ (gitignored — re-download if missing).

set TRACY=%~dp0..\tools\tracy\tracy-profiler.exe

if not exist "%TRACY%" (
    echo Tracy not found at tools\tracy\tracy-profiler.exe
    echo Download windows-0.11.1.zip from https://github.com/wolfpld/tracy/releases/tag/v0.11.1
    echo and extract it to tools\tracy\
    pause
    exit /b 1
)

echo Launching Tracy profiler - connect your game build to start recording.
start "" "%TRACY%"
