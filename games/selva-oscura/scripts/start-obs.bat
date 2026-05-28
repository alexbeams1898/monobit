@echo off
REM Start OBS Studio in tray with replay buffer running so F8 saves the
REM last 30 seconds of the Selva Oscura window to
REM C:\Users\alexb\Videos\Captures\.
REM
REM Run this once per session BEFORE launching Selva. OBS stays in the
REM system tray; the game capture source auto-attaches when selva-oscura.exe
REM starts. F8 is the save-replay hotkey.
REM
REM If F8 conflicts with anything in-game, change it in OBS Settings ->
REM Hotkeys -> Save Replay.

cd /d "C:\Program Files\obs-studio\bin\64bit"
start "" obs64.exe --minimize-to-tray --disable-shutdown-check --startreplaybuffer
