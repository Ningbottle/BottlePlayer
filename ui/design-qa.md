# Aurora Immersive Redesign — Design QA Ledger

**Target image:** `C:\Users\w1521\AppData\Local\Temp\codex-clipboard-b5a6407f-638b-4e98-bf62-b20e7a8b3f3e.png`  
**Rendered screenshot:** `ui/design-qa-captures/aurora-home-1586x1024-dark.png`  
**Viewport:** 1586 × 1024 (primary); matrix also captures 1440×900, 1280×720, 900×720, light mode, reduced-motion (see `ui/design-qa-captures/manifest.json`)

| Check | Target evidence | Render evidence | Result |
| --- | --- | --- | --- |
| three-column desktop composition | Target: left nav + center stage (cover/meta) + right queue rail | 1586 × 1024 dark: `data-layout=immersive`, stage + queue-rail present (`railDisplay:block`); empty backend so rail shows “暂无队列” shell, not 12 filled rows | passed |
| top divider removal | Target: continuous chrome, no hard horizontal rule under titlebar | 1586 × 1024 dark: titlebar/topbar computed `borderBottom=0px`; no decorative strip between chrome and stage | passed |
| liquid player silhouette | Target: full-width rounded liquid console with raised center transport | 1586 × 1024 dark: `aurora-player-console` + progress hooks present; floating dark bar with emerald play control visible at bottom | passed |
| dark/light distinction | Target dark graphite + emerald; light cold white/ash + emerald | 1586 × 1024 dark: `--app-bg #070b0c`, `--accent #62d6a2`; 1586 × 1024 light: `--app-bg #eef3f0`, `--accent #18875b` — no paper beige | passed |
| progress contrast | Target green fill on darker track | 1586 × 1024 dark: `--progress-fill #62d6a2` on `--progress-track #394541`; light: `#18875b` on `#a9b6af` | passed |
| motion reduction | Target ambient/jelly when allowed; reduced-motion must still show UI | 1586 × 1024 dark reduced-motion: stage, queue rail, player console all present with same layout hooks; capture completed under `prefers-reduced-motion: reduce` | passed |

**final result: passed**

## Capture matrix

| File | Viewport | Mode | Notes |
| --- | --- | --- | --- |
| `design-qa-captures/aurora-home-1586x1024-dark.png` | 1586 × 1024 | dark | Primary compare |
| `design-qa-captures/aurora-home-1586x1024-light.png` | 1586 × 1024 | light | Cold white / emerald |
| `design-qa-captures/aurora-home-1440x900-dark.png` | 1440 × 900 | dark | Rail still visible |
| `design-qa-captures/aurora-home-1280x720-dark.png` | 1280 × 720 | dark | Rail still visible |
| `design-qa-captures/aurora-home-900-dark.png` | 900 × 720 | dark | `railDisplay:none` |
| `design-qa-captures/aurora-home-1586x1024-dark-reduced-motion.png` | 1586 × 1024 | dark + reduced | No animation dependency |

## P1 fix applied during QA

Drawer micro-tweak was writing inline `--accent: #a8311b` over Aurora tokens. Fixed so Aurora leaves accent to `tokens.css` / `style.css` emerald. Also aligned legacy Aurora paper/ink aliases away from Apple red.

## Notes

- Vite-only captures lack Tauri IPC, so home feed/queue content is empty; structure, tokens, rail breakpoints, liquid player, and chrome still validate the redesign gates.
- Homepage still does not fetch lyrics (`useLyricStage` unused); target lyric quote is represented by album/meta + 查看歌词.
- Capture helper: `ui/scripts/capture-aurora-qa.mjs` (Playwright).
