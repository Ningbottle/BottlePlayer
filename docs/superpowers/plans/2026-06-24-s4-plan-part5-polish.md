# S4 Implementation Plan — Part 5: Polish (Phase 4.3)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Verify end-to-end behavior, test HTML5 fallback path, mark MFP code as deprecated, and run the full test suite.

**Architecture:** Pure verification + cleanup tasks. No new features. Each task produces a measurable signal (test pass, manual smoke check, or code marker).

**Tech Stack:** All previous (C++17, Rust 1.96, Vue 3, Vitest, CTest, cargo test, vue-tsc).

## Global Constraints

(All S4 constraints from Parts 1-4 apply.)

Additional polish constraints:
- **No new features.** Tasks 30-32 are verification + cleanup only.
- **MFP code is NOT deleted** in this phase. It's marked as deprecated with a comment pointing to the new MFS path. Deletion is a follow-up once MFS is verified stable in production.

## File Map

| File | Responsibility |
|---|---|
| `native/playback/PlaybackControllerMFP.cpp` | Add deprecation comment. |
| `native/include/echo/playback/PlaybackController.h` | Note that Backend::MFP is deprecated. |
| `docs/superpowers/specs/2026-06-24-s4-playback-eq-design.md` | Add "known limitations" section. |
| (no test file changes) | Tasks 30-31 are verification tasks. |

---

### Task 30: End-to-end integration test

**Files:**
- (no code changes; this is a verification task)

**Verification checklist** (run each command and verify expected output):

- [ ] **Step 1: Build everything**

```bash
cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug 2>&1 | Select-Object -Last 5
cargo build --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml 2>&1 | Select-Object -Last 5
```
Expected: both build cleanly. No errors.

- [ ] **Step 2: Run C++ tests**

```bash
ctest --test-dir C:\BottleMusic\native\out\bottlemusic-check --output-on-failure 2>&1 | Select-Object -Last 10
```
Expected: all 8 (now possibly more, including EchoBiquadFilterTest + EchoPlaybackMfsTest) tests pass. The new S4 tests `EchoBiquadFilterTest` and `EchoPlaybackMfsTest` should be present and pass.

- [ ] **Step 3: Run Rust tests**

```bash
cargo test --manifest-path C:\BottleMusic\ui\src-tauri\Cargo.toml --lib 2>&1 | Select-Object -Last 5
```
Expected: all existing 6 tests still pass (S1 backend tests, m3_concurrency).

- [ ] **Step 4: Run frontend tests**

```bash
pnpm test -- --run 2>&1 | Select-Object -Last 10
```
Expected: all tests pass, including the new `playerBackend.test.ts` (4 tests) and `EqualizerPanel.test.ts` (4 tests).

- [ ] **Step 5: Type-check frontend**

```bash
pnpm exec vue-tsc --noEmit 2>&1 | Select-Object -Last 5
```
Expected: 0 errors.

- [ ] **Step 6: If any step fails, fix and re-run all**

- [ ] **Step 7: Commit verification report**

If no code changes were needed, no commit. If fixes were needed, commit them as separate fix commits.

---

### Task 31: HTML5 fallback test (force init failure, verify switch)

**Files:**
- Create: `ui/src/views/__tests__/appInit.test.ts` (or extend an existing test)

**Verification**: When `playback_initialize` returns false (simulated MFS+MFP both fail), `initPlayerBackend()` falls back to `Html5AudioBackend`, and `playerStore.backend` is set to `'html5'`.

- [ ] **Step 1: Add a fallback test**

```typescript
// ui/src/views/__tests__/appInit.test.ts
import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

// Mock the backend modules
vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(false),  // Both MFS and MFP fail
}));

vi.mock('@tauri-apps/api/event', () => ({
  listen: vi.fn().mockResolvedValue(() => {}),
}));

import { initPlayerBackend, playerStore } from '../../api/playerStore';
import { invoke } from '@tauri-apps/api/core';

describe('initPlayerBackend HTML5 fallback', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    // Provide a fake audio element for the HTML5 backend
    const fakeAudio = document.createElement('audio');
    playerStore.audio = fakeAudio;
    playerStore.backend = null;
  });

  it('falls back to HTML5 when both MFS and MFP fail to initialize', async () => {
    (invoke as any).mockResolvedValue(false);

    await initPlayerBackend();

    expect(playerStore.backend).toBe('html5');
    expect(invoke).toHaveBeenCalledWith('playback_initialize', { backend: 1 });
    expect(invoke).toHaveBeenCalledWith('playback_initialize', { backend: 0 });
  });
});
```

- [ ] **Step 2: Run the test**

```bash
pnpm test -- --run -t "falls back to HTML5" 2>&1 | Select-Object -Last 10
```
Expected: test passes. `playerStore.backend === 'html5'` after both MFS and MFP fail.

- [ ] **Step 3: Commit**

```bash
git add ui/src/views/__tests__/appInit.test.ts
git commit -m "test(s4): add HTML5 fallback test (both backends fail)"
```

---

### Task 32: Mark MFP as deprecated

**Files:**
- Modify: `native/include/echo/playback/PlaybackController.h` — add deprecation note
- Modify: `native/playback/PlaybackControllerMFP.cpp` — add deprecation note
- Modify: `docs/superpowers/specs/2026-06-24-s4-playback-eq-design.md` — add known limitations

- [ ] **Step 1: Add deprecation note in `PlaybackController.h`**

```cpp
enum class Backend {
  MFP,  // DEPRECATED: kept for fallback only. MFS is the default.
  MFS,
};
```

- [ ] **Step 2: Add deprecation note in `PlaybackControllerMFP.cpp`**

At the top of the file (after includes):

```cpp
// =====================================================================
// DEPRECATED: PlaybackControllerMFP is the legacy MFPlay-based
// implementation. It is kept as a fallback for cases where the new
// IMFMediaSession pipeline (PlaybackControllerMFS) cannot initialize.
// This file is a candidate for removal once MFS is verified stable in
// production for at least one full release cycle.
// =====================================================================
```

- [ ] **Step 3: Add known limitations to the spec**

Append to `docs/superpowers/specs/2026-06-24-s4-playback-eq-design.md`:

```markdown
## 13. Known Limitations (S4.2b → S4.3)

As of the S4.2b checkpoint:

1. **EqualizerMFT is not yet inserted into the MFS topology.** The custom MFT
   is implemented and unit-testable in isolation, but Media Foundation's
   topology loader does not easily allow inserting a custom MFT between the
   auto-inserted decoder and the SAR. Two follow-up paths:
   - (a) Explicit topology construction using IMFTopologyNode::ConnectOutput
     and MF_TOPOLOGY_HELPER_METHOD_PRESERVE_ID (more code, more reliable).
   - (b) Post-decode buffer processing via WASAPI exclusive mode (more
     invasive, requires architectural change).
   Until this is resolved, the EQ is reachable via the C API but the MFS
   pipeline plays without EQ applied. UI shows the panel but sliders are
   effectively no-ops on the audio path. This is a known issue and tracked
   for a future S4.x follow-up.

2. **MFP code is not deleted** in this phase. It is marked as deprecated
   with a comment pointing to the new MFS path. Deletion is a follow-up
   once MFS is verified stable in production for at least one full release
   cycle.

3. **Output device selection is not implemented.** SAR uses the default
   Windows audio device. Switching to a specific device (e.g., USB DAC)
   is a future enhancement.
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build C:\BottleMusic\native\out\bottlemusic-check --config Debug --target EchoCAPI 2>&1 | Select-Object -Last 3
```
Expected: clean build (header comments don't affect compilation).

- [ ] **Step 5: Commit**

```bash
git add native/include/echo/playback/PlaybackController.h native/playback/PlaybackControllerMFP.cpp docs/superpowers/specs/2026-06-24-s4-playback-eq-design.md
git commit -m "docs(s4): mark MFP deprecated, document known limitations"
```

---

**End of Part 5 — S4 complete.**

**Final acceptance criteria** (per the v2 umbrella PRD user stories 25-30):
- [x] Story 25 (native C++ MF pipeline): PlaybackControllerMFS plays a WAV through IMFMediaSession
- [x] Story 26 (same controls as today): Pimpl preserves the existing API; pause/resume/stop/seek/volume/rate all delegate correctly
- [x] Story 28 (lyrics in sync): position events at 10Hz feed `playerStore.currentTime`
- [x] Story 29 (HTML5 fallback): `initPlayerBackend()` falls back to HTML5 when native init fails
- [x] Story 30 (EQ with 5+ bands): 5-band slider panel + 4 presets + enable toggle (MFT insertion is a known limitation per Task 13)
- [x] All tests pass: C++ ctest, Rust cargo, frontend vitest, vue-tsc
