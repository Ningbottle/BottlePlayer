# Remaining Architecture Debt Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close five remaining structural debts on a dedicated branch, on two parallel tracks (frontend / native), without cross-layer mixed commits.

**Architecture:** Frontend track is serial (① circuit buckets → ④ POST writes + tighten 405 → ② playerStore split). Native track is serial (③ watchdog A→B→C → ⑤a deadline single-source → F EchoContext aggregation). Tracks share no files and may proceed in parallel.

**Tech Stack:** Vue 3 / TypeScript (`ui/src`), Tauri Rust (`ui/src-tauri`), C++20 native (`native/`), CMake, vitest, CTest, cargo.

**Branch:** `refactor/remaining-architecture-debt` (from audit closeout `7cd18c43`)  
**Worktree:** `.worktrees/remaining-architecture-debt`

## Global Constraints

- Touch only one layer per commit (frontend / Rust / C++).
- Each step must independently compile, test, and revert.
- Do not merge frontend ④ with native ③ in one PR.
- Frontend writes stay GET until 4.1 finishes; only then 4.3 tightens methods.
- playerStore split keeps barrel export surface and single reactive state object.
- EchoContext F: zero FFI signature change.

---

## Track map

| Order | Track | PR | Scope |
|-------|-------|----|-------|
| 1 | FE | PR1 | ① Circuit breaker buckets (`backend.ts` only) |
| 2 | Native | PR2-1 | ③A Watchdog action entries (`HttpClient.cpp`) |
| 3 | Native | PR2-2 | ③B Extract `RequestWatchdog` to async |
| 4 | Native | PR2-3 | ③C Scheduler reuses watchdog |
| 5 | FE | PR4 | ④ Write routes POST + tighten 405 |
| 6 | Native | PR3-1 | ⑤a Deadline single-source (C++ + build.rs) |
| 7 | Native | PR3-2 | F EchoContext internal aggregation |
| 8 | FE | PR5 | ② playerStore split (last) |

---

### Task 1: Circuit breaker buckets (PR1)

**Files:**
- Modify: `ui/src/api/backend.ts`
- Modify: `ui/src/api/__tests__/backend.test.ts`
- Test: `ui/src/api/__tests__/circuitBreaker.test.ts` (unchanged unit; new bucket cases in backend.test)

**Interfaces:**
- Produces: `export type CircuitBucket = 'playback' | 'lyric' | 'search' | 'generic'`
- Produces: `export function pickBucket(path: string): CircuitBucket`
- Produces: `export function isCircuitOpen(bucket?: CircuitBucket): boolean` (default `'playback'`)

- [ ] **Step 1: Write failing bucket isolation tests in backend.test.ts**

```ts
it('search failures do not open playback bucket', async () => {
  mockInvoke.mockRejectedValue(new Error('fail'));
  for (let i = 0; i < 5; i++) {
    await expect(apiGet('/search')).rejects.toThrow();
  }
  mockInvoke.mockResolvedValueOnce(
    JSON.stringify({ status: 200, headers: {}, body: { ok: true } }),
  );
  await expect(apiGet('/song/url')).resolves.toEqual({ ok: true });
});
```

- [ ] **Step 2: Run vitest — expect FAIL (single global breaker)**

Run: `pnpm --dir ui exec vitest run src/api/__tests__/backend.test.ts`

- [ ] **Step 3: Implement buckets + pickBucket + wire apiGet/apiPost**

```ts
export type CircuitBucket = 'playback' | 'lyric' | 'search' | 'generic';

function makeBreaker() {
  return new CircuitBreaker({ failureThreshold: 5, openDurationMs: 30_000 });
}

const buckets: Record<CircuitBucket, CircuitBreaker> = {
  playback: makeBreaker(),
  lyric: makeBreaker(),
  search: makeBreaker(),
  generic: makeBreaker(),
};

export function pickBucket(path: string): CircuitBucket {
  if (path.startsWith('/song/url') || path.startsWith('/personal/fm')) return 'playback';
  if (path.startsWith('/search/lyric') || path.startsWith('/lyric')) return 'lyric';
  if (path.startsWith('/search')) return 'search';
  return 'generic';
}

export function isCircuitOpen(bucket: CircuitBucket = 'playback'): boolean {
  return !buckets[bucket].isClosed();
}
```

- [ ] **Step 4: Run vitest — expect PASS**

- [ ] **Step 5: Commit**

```bash
git add ui/src/api/backend.ts ui/src/api/__tests__/backend.test.ts
git commit -m "feat(ui): bucket circuit breakers by request category"
```

---

### Task 2: Watchdog action entries (PR2-1 / Native A)

**Files:**
- Modify: `native/core/HttpClient.cpp` (RequestWatchdog only)

**Interfaces:**
- Produces: `Arm(long timeoutMs, shared_ptr<atomic_bool> claimed, function<void()> action)`
- Produces: thin `Arm(HINTERNET, timeoutMs, claimed)` wrapping close handle

- [ ] **Step 1: Generalize WatchdogEntry to action; Loop CAS→action()**
- [ ] **Step 2: cmake build + EchoHttpClientResilienceTest**
- [ ] **Step 3: Commit** `refactor(native): generalize RequestWatchdog to action entries`

---

### Task 3: Extract RequestWatchdog (PR2-2 / Native B)

**Files:**
- Create: `native/include/echo/async/RequestWatchdog.h`
- Create: `native/async/RequestWatchdog.cpp`
- Modify: `native/CMakeLists.txt` (EchoAsync sources)
- Modify: `native/core/HttpClient.cpp` (include + Arm usage)

- [ ] **Step 1: Move class to async layer; HttpClient keeps winhttp close in lambda**
- [ ] **Step 2: CTest http_client_resilience**
- [ ] **Step 3: Commit** `refactor(native): move RequestWatchdog into EchoAsync`

---

### Task 4: Scheduler reuses watchdog (PR2-3 / Native C)

**Files:**
- Modify: `native/include/echo/async/RequestScheduler.h`

- [ ] **Step 1: Replace detached deadline thread with RequestWatchdog::Arm**
- [ ] **Step 2: Worker CAS-disarms claimed after completion**
- [ ] **Step 3: request_scheduler_resilience_test full green**
- [ ] **Step 4: Commit** `refactor(native): SubmitWithDeadline uses process watchdog`

---

### Task 5: Write routes POST + tighten 405 (PR4)

**Files:**
- Modify frontend call sites (4.1, one commit each)
- Modify: `native/core/CompatApi.cpp` AllowedMethods (4.3)
- Modify: `native/tests/route_contract_test.cpp`

**Active writes:** `/playhistory/upload`, `/register/dev`, `/settings/device`, `/playlist/tracks/add`, `/auth/logout`  
**Dead writes (no caller):** `/playlist/add`, `/playlist/del`, `/playlist/tracks/del`

- [ ] **4.1** Migrate callers to `apiPost` (low risk → high risk), backend still dual-allows GET|POST
- [ ] **4.2** `rg "apiGet\\(.*/(auth/logout|playlist/|playhistory|register/dev|settings/device)"` empty for writes
- [ ] **4.3** Remove `kMethodGet` from write whitelist; assert GET→405 POST→200
- [ ] Commit after each 4.1 route + one for 4.3

---

### Task 6: Deadline single-source (PR3-1 / ⑤a)

**Files:**
- `native/include/echo/core/RequestDeadlines.h` (truth)
- `ui/src-tauri/build.rs` extract → `OUT_DIR/deadlines_generated.rs`
- `ui/src-tauri/src/lib.rs` use generated constants

- [ ] Regex extract `kDeadline*Ms` from header; fail build if missing
- [ ] Assert Rust outer ≥ C++ inner
- [ ] Commit `feat(native,rust): generate deadline constants from RequestDeadlines.h`

---

### Task 7: EchoContext internal aggregation (PR3-2 / F)

**Files:**
- Modify: `native/core/C_API.cpp` only

- [ ] Pack `g_db/g_api/g_scheduler/g_api_rwlock/g_shutdown/g_stats` into `struct EchoContext`
- [ ] `static EchoContext& Ctx()`; no FFI signature change
- [ ] CTest basic_contract + play_stats
- [ ] Commit `refactor(native): aggregate C_API globals into EchoContext`

---

### Task 8: playerStore split (PR5 / ②)

**Files:** extract under `ui/src/api/` with barrel re-exports from `playerStore.ts`

- [ ] **3.0** Characterization tests only (FM, removeFromQueue, saveQueue debounce, next FM)
- [ ] **3.1** `playerPersistence.ts`
- [ ] **3.2** `useFmSession.ts`
- [ ] **3.3** `usePlaybackQueue.ts`
- [ ] **3.4** `songUrlResolver.ts` + `usePlayHistory.ts`
- [ ] Optional EQ extract last or skip
- [ ] Export surface unchanged; vitest + `pnpm --dir ui build`

---

## Self-review

1. **Spec coverage:** ①④② + ③ABC + ⑤a + F all have tasks.
2. **No placeholders:** task interfaces named; verification commands explicit.
3. **Ordering:** FE ① before ④ before ②; native A→B→C; E/F after C preferred.

## Execution

Inline in worktree with frequent commits. Push branch when first tasks land.
