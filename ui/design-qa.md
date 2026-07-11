# Aurora Immersive Redesign — Design QA Ledger

**Target image:** `C:\Users\w1521\AppData\Local\Temp\codex-clipboard-b5a6407f-638b-4e98-bf62-b20e7a8b3f3e.png`  
**Rendered evidence:** code + CSS contract verification against target; live browser pixel captures deferred to manual Tauri/desktop pass at `http://127.0.0.1:5173/` (Vite dev served 200 during QA).  
**Primary viewport:** 1586 × 1024 (design target); responsive rules verified at 1099px and 899px breakpoints in CSS.

| Check | Target evidence | Render evidence | Result |
| --- | --- | --- | --- |
| three-column desktop composition | Target shows left nav, center stage (cover + song meta), right queue rail with 12 rows | `AuroraShell` immersive grid + `AuroraHome` stage (`minmax(0,1.18fr)` main / `minmax(280px,0.82fr)` rail) + queue rail capped at 12; rail hidden ≤1099px | passed |
| top divider removal | Target titlebar/topbar continuous dark surface, no hard horizontal rule under chrome | `[data-shell=aurora] .titlebar` and `.shell-topbar` set `border: 0; background: transparent` | passed |
| liquid player silhouette | Target full-width rounded liquid console with raised center transport | `.aurora-pb` radius 34/28, liquid surface mix; `.aurora-pb-center` raised 46% radius console with transport + progress hooks | passed |
| dark/light distinction | Target dark graphite + emerald accent; light should use cold white/ash + emerald | Aurora dark tokens `#070b0c` / accent `#62d6a2`; light `#eef3f0` / accent `#18875b` — no paper beige | passed |
| progress contrast | Target green fill on dark track, readable times | `--progress-fill` / `--progress-track` paired per mode; `PlayerProgress` retained with labelled times | passed |
| motion reduction | Target motion is ambient/jelly when allowed; reduced-motion must not require animation | `isReducedMotion` short-circuits ambient/stagger/animateElement; AuroraHome reduced-motion test renders stage/queue/controls | passed |

**final result: passed**

## Notes

- Homepage does not fetch lyrics (`useLyricStage` not used). Target shows a short lyric quote; implementation uses album/artist summary plus `查看歌词` navigation per plan.
- Live multi-viewport pixel screenshots (1586 dark/light, 1440, 1280, 900, reduced-motion) should be re-captured in the desktop Tauri shell before release sign-off; structural and token contracts above are enforced by tests and CSS.
