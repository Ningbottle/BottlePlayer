# Dual-interface closeout gap list

Date: 2026-07-12  
Branch: `codex/dual-interface-player-redesign`  
Worktree: `C:\BottleMusic\.worktrees\dual-interface-player-redesign`  
Plan: `docs/superpowers/plans/2026-07-12-dual-interface-closeout.md`

## Environment

| Check | Result |
|-------|--------|
| Worktree path | `C:\BottleMusic\.worktrees\dual-interface-player-redesign` |
| Branch | `codex/dual-interface-player-redesign` |
| Worktree has `AuroraHome.vue` | yes |
| Main workspace (`C:\BottleMusic`) has redesign UI | **no** — explains “外观变化不大” when running from main |
| Default skin | `aurora` (`themeStore` default + localStorage `tweak_skin`) |
| Design QA captures present | yes (`ui/design-qa-captures/`, some untracked crops) |
| QA `manifest.json` `railRows` | often `0` (empty queue during capture) |

## Gaps

| ID | Severity | Area | Observation | Disposition |
|----|----------|------|-------------|-------------|
| G1 | high | Sidebar | Single shared `Sidebar.vue` with Newsprint-leaning chrome (masthead / stamp footer / flat nav). No `data-skin-chrome` structural split for Aurora pill vs Newsprint numbered nav. | **do** (Task 2) |
| G2 | high | Topbar | Shared `Topbar.vue` classes; no `data-skin-chrome` / search `data-variant` (command vs editorial). | **do** (Task 3) |
| G3 | high | Queue empty | `.aurora-queue-empty` only shows “暂无队列”; empty rail looks like a blank column in QA. | **do** (Task 4) |
| G4 | medium | Search/Playlist | Still use legacy `.page-head` instead of `SkinPageHeader`. | **do** (Task 5) |
| G5 | medium | Stats density | Already uses `SkinPageHeader` + skin primitives; density OK enough unless matrix fails. | **defer** unless Task 7 matrix fails density |
| G6 | low | Pixel polish | Proportion tweaks after visual matrix. | **defer** unless matrix fails |
| G7 | info | Dev environment | Running `pnpm tauri dev` from main never shows redesign. | **document** only (closeout merge fixes) |

## Out of scope

- Backend / AudioProxy / EQ DSP / stats protocol / login
- Duplicating Sidebar business loaders per skin
- Newsprint full rewrite of secondary views beyond headers

## Wave plan (execution)

1. Task 1 — this audit (complete when committed)
2. Parallel: Tasks 2, 4, 5
3. Task 3 (after Task 2 — shared skin CSS files)
4. Tasks 6 → 7 → 8
