# T1 Media + Frontend Polish + Stability Plan

**Order:** 1 system media → 2 frontend → 3 stability  
**Branch:** `feat/t1-media-frontend-stability`

## Track 1 — T1b / T1c / T1-SMTC
- Store AppHandle; emit only under `#[cfg(not(test))]` (avoids cargo-test ENTRYPOINT crash)
- Tray menu: Play/Pause, Next, Prev, Show, Quit
- Global shortcuts: MediaPlayPause / MediaNextTrack / MediaPrevTrack (plugin)
- SMTC via `souvlaki` MediaControls (Windows); degrade if init fails
- Tests: session emit queue + unit tests; vitest bridge unchanged

## Track 2 — Frontend polish
- Offline / circuit_open: clearer banner + backend maps circuit to degraded UX
- Queue search filter (lightweight) if missing
- Aurora/Newsprint: fix any known shell a11y/offline copy

## Track 3 — Stability
- Native CTest: concurrent write + multi-thread RO read sees commit after barrier
- EchoShutdown / circuit tests already exist — extend if gaps

## Gates
vue-tsc, vitest, cargo test --lib, ctest, clippy -D warnings
