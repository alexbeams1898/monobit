# OBS Studio bug-capture setup

OBS is installed + pre-configured. Daily workflow:

1. Run `games/selva-oscura/scripts/start-obs.bat` once per session.
   OBS minimizes to the system tray with replay buffer running.
2. Launch the game.
3. See a bug → press **F8** → file lands in
   `C:\Users\alexb\Videos\Captures\` named like
   `Replay 2026-05-24 22-31-24.mp4`.
4. Tell Claude: **"look at the latest"**.

If F8 conflicts with anything in-game, change it in OBS Settings →
Hotkeys → Save Replay.

## What's preconfigured

Already set by the in-repo config (don't change unless you're sure):

- **Source**: Game Capture bound to `selva-oscura.exe` (`Selva Game
  Capture` scene). Regardless of what window has focus, the recording
  always shows the game.
- **Output path**: `C:\Users\alexb\Videos\Captures`. Claude greps this
  path for the newest mp4.
- **Format**: MP4 / x264 / 60fps / source resolution.
- **Replay buffer**: 30 seconds, ~512MB RAM cap.
- **Save Replay hotkey**: F8.

## When the bug-capture workflow breaks

Symptoms + fixes:

| Symptom | Fix |
|---|---|
| Claude says "latest file is VS Code" / not the game | OBS isn't running, or the game-capture source lost its window binding. Re-run `start-obs.bat`. If still broken, re-open OBS via the tray, double-click `Selva Game Capture`, re-select `[selva-oscura.exe]: Selva Oscura` in the Window dropdown. |
| F8 doesn't save anything | Replay buffer isn't running. Right-click OBS tray icon → Start Replay Buffer. If that fails, the encoder probably failed; check the latest log in `%APPDATA%\obs-studio\logs\`. |
| No mp4 lands after F8 | Output path may be locked / wrong drive. Check OBS Settings → Output → Recording → Recording Path matches `C:\Users\alexb\Videos\Captures\`. |
| OBS crashes on launch | Delete `%APPDATA%\obs-studio\` and re-install. The configs at the canonical paths will be regenerated; re-run `start-obs.bat`. |

## Reset to clean state (nuclear option)

```bash
# Kill OBS
powershell -c "Get-Process obs64 -ErrorAction SilentlyContinue | Stop-Process -Force"
# Wipe config
rm -rf /c/Users/alexb/AppData/Roaming/obs-studio
# Reinstall (uses winget; no prompts)
"$LOCALAPPDATA/Microsoft/WindowsApps/winget.exe" install OBSProject.OBSStudio --silent --accept-source-agreements --accept-package-agreements
# Re-apply our config (Claude does this; tell it "redo OBS config")
```
