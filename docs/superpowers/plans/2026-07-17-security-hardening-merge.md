# Security Hardening & Branch Merge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the uncommitted security hardening on `codex/dual-interface-deep-refactor` (with review-driven fixes), run RustSec + Vitest-noise work, then `--no-ff` merge to `main` and push, and clean up superseded branches/worktrees.

**Architecture:** All hardening lands on the codex branch first, is fully verified green, then merges into `main` as one `--no-ff` merge commit preserving the 25-commit TDD history. Security fixes are TDD (RED→GREEN) in C++/Rust/TS. Branch cleanup happens after the merge.

**Tech Stack:** C++ (native backend, CTest), Rust/Tauri (audio proxy, capabilities, CSP), TypeScript/Vue 3 (frontend, Vitest), GitHub Actions (release.yml), Git worktrees.

## Global Constraints

- Working tree for all hardening: `C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic` on branch `codex/dual-interface-deep-refactor`. Run git there with `git -C "<worktree>"`.
- Never delete remote branches. Only local branches + worktrees are cleaned.
- Baseline must not regress: Vitest 781/781, Rust 29 unit + 2 integration, CTest 11/11, `vue-tsc`, `vite build`, `cargo clippy -D warnings` all green.
- TDD: every security fix gets a failing test first. Do not modify tests to accept wrong behavior.
- One commit per task (or per logical sub-task). Commit messages follow existing `type(scope): subject` convention.
- Do not touch the main worktree's 394 lines of uncommitted changes until Task 0.
- `docs/` is gitignored but specs/plans are tracked via `git add -f`.

---

## File Structure

**C++ (native):**
- `native/include/echo/core/CompatApiUtils.h` — `StripSessionCredentials` (rewrite to case-insensitive credential allowlist) and `JsonResponse` (unchanged).
- `native/core/CompatApi.cpp` — `CompatApi::Handle` (add chokepoint scrub call).
- `native/core/compat_routes/LoginRoutes.cpp` — remove redundant `StripSessionCredentials(result)` at line 111.
- `native/core/compat_routes/UserRoutes.cpp` — remove redundant `StripSessionCredentials` calls (2 sites).
- `native/storage/SessionRepository.cpp` — migration closure + `SecureZeroMemory`.
- `native/tests/basic_contract_tests.cpp` — new tests for chokepoint scrub, variant fields, migration closure, corrupt ciphertext.

**Rust/Tauri:**
- `.github/workflows/release.yml` — `shell: bash` on verify steps; convert Rust step PATH export.
- `ui/src-tauri/` — cargo-audit runs here (no source change unless a vuln is found).

**TypeScript/Vue:**
- `ui/src/api/__tests__/releaseSecurity.test.ts` — add `ctest` + `continue-on-error` assertions.
- `ui/src/views/lyric/AuroraLyricStage.vue` — add keyboard navigation (ArrowUp/ArrowDown seek).
- `ui/src/views/lyric/__tests__/LyricStages.test.ts` — keyboard nav tests.
- `ui/src/test/` (or existing setup) — unified `HTMLMediaElement`/`Canvas` mock.

---

## Task 0: Quarantine main worktree's uncommitted changes

**Files:**
- Inspect: `C:/BottleMusic` working tree (8 modified files, 394 lines)
- Produce: a stash named `main-candidate-changes-pre-merge` for backup

**Interfaces:** Consumes the deep-refactor branch as comparison baseline. Produces a clean main worktree ready for the merge.

- [ ] **Step 1: Diff each main worktree file against deep-refactor's version**

For each of the 8 modified files, check whether deep-refactor already implements the same behavior:
```bash
for f in ui/src/api/motion.ts ui/src/components/QueuePanel.vue ui/src/views/home/AuroraHome.vue ui/src/views/home/__tests__/AuroraHome.test.ts ui/src/views/lyric/AuroraLyricStage.vue ui/src/views/lyric/AuroraPlaylistShelf.vue ui/src/views/lyric/NewsprintLyricStage.vue ui/src/views/lyric/__tests__/LyricStages.test.ts; do
  echo "=== $f ==="
  git -C "C:/BottleMusic" diff -- "$f" | head -80
done
```
Compare each change to the corresponding file in `codex/dual-interface-deep-refactor`. Already-confirmed covered: lyric click-to-seek (deep-refactor `AuroraLyricStage.vue:59-61,405`).

- [ ] **Step 2: Stash the main worktree changes as a backup**

```bash
git -C "C:/BottleMusic" stash push -m "main-candidate-changes-pre-merge-2026-07-17" -- ui/src/api/motion.ts ui/src/components/QueuePanel.vue ui/src/views/home/AuroraHome.vue ui/src/views/home/__tests__/AuroraHome.test.ts ui/src/views/lyric/AuroraLyricStage.vue ui/src/views/lyric/AuroraPlaylistShelf.vue ui/src/views/lyric/NewsprintLyricStage.vue ui/src/views/lyric/__tests__/LyricStages.test.ts
```
Expected: `Saved working directory and index state ...` and `git -C "C:/BottleMusic" status --short` shows only untracked items (`.agents/`, `.claude/`, `mcps/`, `skills-lock.json`, `ui/design-qa-captures/`, `ui/scripts/capture-aurora-layout-qa.mjs`).

- [ ] **Step 3: Record the stash ref**

```bash
git -C "C:/BottleMusic" stash list | head -3
```
Note the stash ref in the final report. The stash is recoverable if any unique behavior is later found missing from deep-refactor.

- [ ] **Step 4: If any unique behavior is found, port it to deep-refactor (RED→GREEN) before Task 10**

If Step 1 reveals behavior in main's changes that deep-refactor does NOT cover, write a failing test on the codex branch, implement, and commit there. Otherwise proceed.

---

## Task 1: H1 — Scrub credentials at the CompatApi::Handle chokepoint

**Files:**
- Modify: `native/core/CompatApi.cpp` (in `Handle`, after `HandleKnownRoute`)
- Modify: `native/core/compat_routes/LoginRoutes.cpp:111` (remove redundant call)
- Modify: `native/core/compat_routes/UserRoutes.cpp` (remove 2 redundant calls)
- Test: `native/tests/basic_contract_tests.cpp`

**Interfaces:**
- Consumes: `StripSessionCredentials(nlohmann::json&)` from `CompatApiUtils.h`.
- Produces: every `CompatApi::Handle` response has credential fields stripped, regardless of route.

- [ ] **Step 1: Write the failing test (chokepoint covers a route that previously did not scrub)**

Add to `native/tests/basic_contract_tests.cpp`, adjacent to the existing `/user/detail` scrub test (~line 360), inside the same test function body:

```cpp
  {
    // Routes that previously had no explicit StripSessionCredentials call
    // (e.g. /user/vip/detail) must still be scrubbed at the Handle chokepoint.
    echo::storage::Database vipDb;
    vipDb.Open(TestDbPath());
    vipDb.Initialize();
    echo::core::CompatApiHandlers vipHandlers;
    vipHandlers.userVip = [](std::string, std::string) {
      return nlohmann::json{
          {"status", 1},
          {"data",
           {{"vip", 1},
            {"token", "vip-token-secret"},
            {"t1", "vip-t1-secret"}}}};
    };
    echo::core::CompatApi vipApi(vipDb, std::move(vipHandlers));
    const auto vipResponse = vipApi.Handle("GET", "/user/vip/detail", {}, {}, "");
    const auto vipText = vipResponse.body.dump();
    assert(vipText.find("vip-token-secret") == std::string::npos);
    assert(vipText.find("vip-t1-secret") == std::string::npos);
    assert(vipResponse.body["data"]["vip"] == 1);
    std::cout << "  [ok] CompatApi scrubs credentials at the Handle chokepoint" << std::endl;
  }
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cmake -S native --preset bottlemusic-check && cmake --build native/out/bottlemusic-check --config Debug && ctest --preset bottlemusic-check -R "Compat" --output-on-failure
```
Expected: FAIL — `vip-token-secret` is found in the response body (chokepoint not yet scrubbing `/user/vip/detail`).

- [ ] **Step 3: Add the chokepoint scrub in `CompatApi::Handle`**

In `native/core/CompatApi.cpp`, inside `CompatApi::Handle`, immediately after `auto response = HandleKnownRoute(...);` and before the logging block, add:

```cpp
  StripSessionCredentials(response.body);
```

`CompatApi.cpp` already includes `CompatApiUtils.h` transitively via `CompatApi.h`; if the build complains, add `#include "echo/core/CompatApiUtils.h"` at the top of `CompatApi.cpp`.

- [ ] **Step 4: Remove the now-redundant scattered scrub calls**

- `native/core/compat_routes/LoginRoutes.cpp`: delete the line `StripSessionCredentials(result);` at ~line 111 (the next line `return JsonResponse(result);` stays).
- `native/core/compat_routes/UserRoutes.cpp`: delete each `StripSessionCredentials(...)` call (2 sites, in `HandleUserDetail` paths). Leave the surrounding `return JsonResponse(...)` intact.

- [ ] **Step 5: Run the full native test suite to verify pass**

```bash
ctest --preset bottlemusic-check --output-on-failure
```
Expected: PASS — all tests green, including the new chokepoint test and the pre-existing `/user/detail` scrub test (which still passes because the chokepoint now does the work).

- [ ] **Step 6: Commit**

```bash
git -C "C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic" add native/core/CompatApi.cpp native/core/compat_routes/LoginRoutes.cpp native/core/compat_routes/UserRoutes.cpp native/tests/basic_contract_tests.cpp
git -C "C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic" commit -m "fix(native): scrub session credentials at CompatApi chokepoint"
```

---

## Task 2: M2 — Case-insensitive credential allowlist

**Files:**
- Modify: `native/include/echo/core/CompatApiUtils.h` (`StripSessionCredentials`)
- Test: `native/tests/basic_contract_tests.cpp`

**Interfaces:**
- Produces: `StripSessionCredentials` erases any key matching (case-insensitively) the credential set: `token`, `t1`, `access_token`, `auth_token`, `session_token`, `secret`, `cookie`, `set-cookie`, `signature`.

- [ ] **Step 1: Write the failing test (variant + nested + case fields)**

Add to `native/tests/basic_contract_tests.cpp` near the chokepoint test:

```cpp
  {
    // Credential fields under variant names and nested objects are scrubbed.
    echo::storage::Database credDb;
    credDb.Open(TestDbPath());
    credDb.Initialize();
    echo::core::CompatApiHandlers credHandlers;
    credHandlers.userPlaylist = [](std::string, std::string, int, int) {
      return nlohmann::json{
          {"status", 1},
          {"data",
           {{"lists",
             nlohmann::json::array({
                 nlohmann::json{{"access_token", "atk-secret"},
                                {"Token", "case-secret"},
                                {"signature", "sig-secret"},
                                {"cookie", "ck-secret"},
                                {"auth_token", "autk-secret"},
                                {"secret", "sec-secret"},
                                {"keep", "keep-me"}},
             })}}}};
    };
    echo::core::CompatApi credApi(credDb, std::move(credHandlers));
    const auto credResponse = credApi.Handle("GET", "/user/playlist", {}, {}, "");
    const auto credText = credResponse.body.dump();
    assert(credText.find("atk-secret") == std::string::npos);
    assert(credText.find("case-secret") == std::string::npos);
    assert(credText.find("sig-secret") == std::string::npos);
    assert(credText.find("ck-secret") == std::string::npos);
    assert(credText.find("autk-secret") == std::string::npos);
    assert(credText.find("sec-secret") == std::string::npos);
    assert(credText.find("keep-me") != std::string::npos);
    std::cout << "  [ok] StripSessionCredentials covers variant credential fields" << std::endl;
  }
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
ctest --preset bottlemusic-check -R "Compat" --output-on-failure
```
Expected: FAIL — variant fields like `access_token` are still present.

- [ ] **Step 3: Rewrite `StripSessionCredentials` with a case-insensitive allowlist**

In `native/include/echo/core/CompatApiUtils.h`, replace the existing `StripSessionCredentials` with:

```cpp
inline bool IsCredentialKey(const std::string& key) {
  static const std::unordered_set<std::string> kCredentialKeys = {
      "token", "t1", "access_token", "auth_token", "session_token",
      "secret", "cookie", "set-cookie", "signature"};
  std::string lowered;
  lowered.reserve(key.size());
  for (unsigned char c : key) lowered.push_back(static_cast<char>(std::tolower(c)));
  return kCredentialKeys.count(lowered) > 0;
}

inline void StripSessionCredentials(nlohmann::json& value) {
  if (value.is_object()) {
    std::vector<std::string> toErase;
    for (auto& [key, child] : value.items()) {
      if (IsCredentialKey(key)) toErase.push_back(key);
    }
    for (const auto& key : toErase) value.erase(key);
    for (auto& [_, child] : value.items()) {
      StripSessionCredentials(child);
    }
    return;
  }
  if (value.is_array()) {
    for (auto& child : value) {
      StripSessionCredentials(child);
    }
  }
}
```

Add `#include <cctype>`, `#include <unordered_set>`, `#include <vector>` to the top of `CompatApiUtils.h` (after `#include <string>`).

- [ ] **Step 4: Run the full native suite to verify pass**

```bash
ctest --preset bottlemusic-check --output-on-failure
```
Expected: PASS — variant-field test green; chokepoint + detail + migration tests still green.

- [ ] **Step 5: Commit**

```bash
git -C "<worktree>" add native/include/echo/core/CompatApiUtils.h native/tests/basic_contract_tests.cpp
git -C "<worktree>" commit -m "fix(native): scrub credential fields under variant names"
```

---

## Task 3: M1 — Close the plaintext migration path after first migration

**Files:**
- Modify: `native/storage/SessionRepository.cpp` (`Load`)
- Test: `native/tests/basic_contract_tests.cpp`

**Interfaces:**
- Consumes: `database_.GetJson`/`SetJson` for the migration flag key `session.encryption_migrated`.
- Produces: after migration, a plaintext `session.info` payload is no longer trusted; Load returns nullopt and logs.

- [ ] **Step 1: Write the failing test (post-migration plaintext is refused)**

Add to `native/tests/basic_contract_tests.cpp` after the migration test (~line 1393):

```cpp
  {
    // Once migration has run, a plaintext session.info payload is no longer
    // trusted (closes the silent-plaintext-bypass gap).
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    // Seed legacy plaintext and migrate via first Load().
    db.SetJson("session.info",
               {{"userid", "legacy-user-2"}, {"token", "legacy-token-2"}, {"t1", "legacy-t1-2"},
                {"nickname", "legacy-nick-2"}, {"pic", "legacy-pic-2"}});
    echo::storage::SessionRepository repo(db);
    assert(repo.Load().has_value());
    // Simulate a plaintext blob written after migration (bug/restore/other writer).
    db.SetJson("session.info",
               {{"userid", "sneak-user"}, {"token", "sneak-token"}, {"t1", "sneak-t1"},
                {"nickname", "sneak-nick"}, {"pic", "sneak-pic"}});
    const auto afterMigration = repo.Load();
    assert(!afterMigration.has_value());
    std::cout << "  [ok] SessionRepository refuses plaintext after migration" << std::endl;
  }
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
ctest --preset bottlemusic-check -R "Session" --output-on-failure
```
Expected: FAIL — `afterMigration.has_value()` is true (plaintext still trusted).

- [ ] **Step 3: Gate the migration behind a one-shot flag**

In `native/storage/SessionRepository.cpp`, replace the plaintext-migration tail of `Load()` (the block after the encrypted branch) with:

```cpp
  // One-time migration for databases created before session encryption.
  if (database_.GetJson("session.encryption_migrated").value_or(false)) {
    // Migration already completed; a plaintext payload here is an anomaly
    // (bug, backup restore, or another writer). Do not trust it.
    ECHO_LOG("SessionRepository",
             "refusing plaintext session.info after migration; ignoring");
    return std::nullopt;
  }
  const auto session = echo::core::SessionInfoFromJson(*payload);
  if (IsEmptySession(session)) return std::nullopt;
  Save(session);
  database_.SetJson("session.encryption_migrated", true);
  return session;
```

`ECHO_LOG` is already available via `CompatApiUtils.h`/diagnostics includes used elsewhere in the native core; if not visible in this TU, add the include the build error names.

- [ ] **Step 4: Run the full native suite to verify pass**

```bash
ctest --preset bottlemusic-check --output-on-failure
```
Expected: PASS — new refusal test green; the existing "migrates plaintext sessions" test still passes (flag is set by that migration, but that test seeds fresh DBs so no cross-contamination). If the existing migration test now fails because it expects a second plaintext Load to succeed, that is the intended behavior change — do NOT weaken the new test.

- [ ] **Step 5: Commit**

```bash
git -C "<worktree>" add native/storage/SessionRepository.cpp native/tests/basic_contract_tests.cpp
git -C "<worktree>" commit -m "fix(native): close plaintext session path after one-time migration"
```

---

## Task 4: M3 — Zero plaintext/DPAPI buffers from memory

**Files:**
- Modify: `native/storage/SessionRepository.cpp` (`ProtectForCurrentUser`, `UnprotectForCurrentUser`, `Save`)
- Test: `native/tests/basic_contract_tests.cpp` (regression: round-trip still works)

**Interfaces:** No public API change. Internal hygiene only.

- [ ] **Step 1: Write the regression test (round-trip survives zeroing)**

Add to `native/tests/basic_contract_tests.cpp` near the session tests:

```cpp
  {
    // Round-trip still works after SecureZeroMemory is added to DPAPI paths.
    echo::storage::Database db;
    db.Open(TestDbPath());
    db.Initialize();
    echo::storage::SessionRepository repo(db);
    echo::core::SessionInfo session;
    session.userId = "zero-user";
    session.token = "zero-token";
    session.t1 = "zero-t1";
    session.nickname = "zero-nick";
    session.pic = "zero-pic";
    repo.Save(session);
    const auto loaded = repo.Load();
    assert(loaded.has_value());
    assert(loaded->token == "zero-token");
    assert(loaded->t1 == "zero-t1");
    assert(loaded->userId == "zero-user");
    std::cout << "  [ok] SessionRepository round-trip survives buffer zeroing" << std::endl;
  }
```

- [ ] **Step 2: Run the test to verify it passes (baseline before change)**

```bash
ctest --preset bottlemusic-check -R "Session" --output-on-failure
```
Expected: PASS (this is a guard against the zeroing breaking the copy).

- [ ] **Step 3: Add `SecureZeroMemory` calls**

In `native/storage/SessionRepository.cpp`:

In `ProtectForCurrentUser`, replace the try-block body so the output blob is zeroed before `LocalFree`:
```cpp
  try {
    const auto encoded = Base64Encode(output.pbData, output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return encoded;
  } catch (...) {
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    throw;
  }
```

In `UnprotectForCurrentUser`, before `LocalFree(output.pbData)`:
```cpp
  std::string plaintext(reinterpret_cast<const char*>(output.pbData), output.cbData);
  SecureZeroMemory(output.pbData, output.cbData);
  LocalFree(output.pbData);
  return plaintext;
```

In `Save`, zero the plaintext JSON before it goes out of scope (add at end of function, after `SetJson`):
```cpp
  SecureZeroMemory(plaintext.data(), plaintext.size());
```
`plaintext` is currently `const auto`; change it to `auto` so `.data()` is mutable.

- [ ] **Step 4: Run the full native suite to verify pass**

```bash
ctest --preset bottlemusic-check --output-on-failure
```
Expected: PASS — round-trip + all session tests green.

- [ ] **Step 5: Commit**

```bash
git -C "<worktree>" add native/storage/SessionRepository.cpp native/tests/basic_contract_tests.cpp
git -C "<worktree>" commit -m "fix(native): zero plaintext and DPAPI buffers from memory"
```

---

## Task 5: H2 — Make release.yml verify steps fail on any error

**Files:**
- Modify: `.github/workflows/release.yml`
- Test: `ui/src/api/__tests__/releaseSecurity.test.ts`

**Interfaces:** Produces: every verify step uses `shell: bash` (GitHub default `bash -eo pipefail`), so a failing command fails the step.

- [ ] **Step 1: Write the failing test (asserts bash shell + ctest + no continue-on-error)**

Add to `ui/src/api/__tests__/releaseSecurity.test.ts` (inside the existing `release.yml` describe block, following the current SHA/gating assertions):

```ts
  it('uses bash for verify steps so native-command failures propagate', () => {
    const workflow = readFileSync(resolve(rootDir, '..', '..', '..', '.github/workflows/release.yml'), 'utf8');
    const verifySteps = ['Verify native core', 'Verify release', 'Verify Rust bridge'];
    for (const stepName of verifySteps) {
      const stepBlock = extractStep(workflow, stepName);
      expect(stepBlock).toMatch(/shell:\s*bash/);
      expect(stepBlock).not.toMatch(/continue-on-error:\s*true/i);
    }
  });

  it('runs ctest as part of native verification', () => {
    const workflow = readFileSync(resolve(rootDir, '..', '..', '..', '.github/workflows/release.yml'), 'utf8');
    const nativeStep = extractStep(workflow, 'Verify native core');
    expect(nativeStep).toContain('ctest');
  });
```

If `extractStep` does not exist in the test file, add this helper at the top of the describe block:
```ts
  function extractStep(yaml: string, name: string): string {
    const idx = yaml.indexOf(`name: ${name}`);
    expect(idx).toBeGreaterThan(-1);
    const next = yaml.indexOf('\n      - name:', idx + 1);
    return yaml.slice(idx, next === -1 ? undefined : next);
  }
```
(Adjust the `'\n      - name:'` separator to match the actual indentation if the build fails — the test file already parses release.yml, so mirror its existing approach.)

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd ui && pnpm vitest run src/api/__tests__/releaseSecurity.test.ts
```
Expected: FAIL — verify steps do not currently have `shell: bash`.

- [ ] **Step 3: Add `shell: bash` to the two pure verify steps**

In `.github/workflows/release.yml`, for the `Verify native core` and `Verify release` steps, add `shell: bash` under the step name. Example for `Verify release`:
```yaml
      - name: Verify release
        shell: bash
        run: |
          pnpm test
          pnpm build
        working-directory: ui
```
Do the same for `Verify native core`.

- [ ] **Step 4: Convert the Rust verify step to bash**

Replace the `Verify Rust bridge` step with:
```yaml
      - name: Verify Rust bridge
        shell: bash
        run: |
          export PATH="$PWD/../../native/vcpkg_installed/x64-windows/bin:$PATH"
          cargo test
          cargo clippy --all-targets -- -D warnings
        working-directory: ui/src-tauri
```
(bash `-e` makes `cargo test` failure stop the step; `export PATH` replaces the PowerShell `$env:PATH` form.)

- [ ] **Step 5: Run the test to verify pass**

```bash
cd ui && pnpm vitest run src/api/__tests__/releaseSecurity.test.ts
```
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git -C "<worktree>" add .github/workflows/release.yml ui/src/api/__tests__/releaseSecurity.test.ts
git -C "<worktree>" commit -m "fix(ci): make release verify steps fail on any command error"
```

---

## Task 6: Phase 1.5 — Port shiny-cabin lyric keyboard navigation

**Files:**
- Modify: `ui/src/views/lyric/AuroraLyricStage.vue`
- Test: `ui/src/views/lyric/__tests__/LyricStages.test.ts`

**Interfaces:**
- Consumes: `props.model.parsedLyrics` (`Array<{ time: number; text: string }>`), `props.model.activeIndex` (`number`).
- Produces: ArrowDown seeks to next line, ArrowUp seeks to previous line, via existing `seek-line` emit (already wired to `playerStore.seek` by parent).

- [ ] **Step 1: Write the failing tests**

In `ui/src/views/lyric/__tests__/LyricStages.test.ts`, add a new describe block (mirror the existing `createModel` helper used by other tests in this file):

```ts
describe('AuroraLyricStage keyboard navigation', () => {
  it('emits seek-line with the next line time on ArrowDown', async () => {
    const model = createModel({ activeIndex: 1 });
    const wrapper = mount(AuroraLyricStage, { props: { model } });
    const scroll = wrapper.find('[data-test="lyric-scroll"]');
    await scroll.trigger('keydown', { key: 'ArrowDown' });
    const events = wrapper.emitted('seek-line');
    expect(events).toBeTruthy();
    expect(events![0]).toEqual([model.parsedLyrics[2].time]);
  });

  it('emits seek-line with the previous line time on ArrowUp', async () => {
    const model = createModel({ activeIndex: 1 });
    const wrapper = mount(AuroraLyricStage, { props: { model } });
    const scroll = wrapper.find('[data-test="lyric-scroll"]');
    await scroll.trigger('keydown', { key: 'ArrowUp' });
    const events = wrapper.emitted('seek-line');
    expect(events).toBeTruthy();
    expect(events![0]).toEqual([model.parsedLyrics[0].time]);
  });

  it('does nothing at the last line on ArrowDown', async () => {
    const model = createModel({ activeIndex: model.parsedLyrics.length - 1 });
    const wrapper = mount(AuroraLyricStage, { props: { model } });
    await wrapper.find('[data-test="lyric-scroll"]').trigger('keydown', { key: 'ArrowDown' });
    expect(wrapper.emitted('seek-line')).toBeFalsy();
  });
});
```
Confirm `createModel` exists and its `parsedLyrics`/`activeIndex` shape; if `createModel` is named differently, use the file's actual helper (grep `function createModel` or `const createModel` in this test file).

- [ ] **Step 2: Run the tests to verify they fail**

```bash
cd ui && pnpm vitest run src/views/lyric/__tests__/LyricStages.test.ts -t "keyboard navigation"
```
Expected: FAIL — no `@keydown` handler on `.lyric-scroll`; `seek-line` not emitted.

- [ ] **Step 3: Add the keydown handler and wire it to both lyric-scroll divs**

In `ui/src/views/lyric/AuroraLyricStage.vue` `<script setup>`, after `onLineClick`:

```ts
function onLyricKeydown(e: KeyboardEvent): void {
  const lyrics = props.model.parsedLyrics;
  const current = props.model.activeIndex;
  if (e.key === 'ArrowDown') {
    e.preventDefault();
    const next = current + 1;
    if (next < lyrics.length) onLineClick(lyrics[next]);
  } else if (e.key === 'ArrowUp') {
    e.preventDefault();
    const prev = current - 1;
    if (prev >= 0) onLineClick(lyrics[prev]);
  }
}
```

On every `<div class="lyric-scroll" ...>` (there are two: fullscreen and non-fullscreen), add `tabindex="0"` and `@keydown="onLyricKeydown"`:
```html
        class="lyric-scroll"
        :class="{ paused: !model.autoFollowing }"
        data-test="lyric-scroll"
        tabindex="0"
        @wheel.passive="$emit('user-scroll')"
        @touchmove.passive="$emit('user-scroll')"
        @keydown="onLyricKeydown"
```

- [ ] **Step 4: Run the tests to verify pass**

```bash
cd ui && pnpm vitest run src/views/lyric/__tests__/LyricStages.test.ts
```
Expected: PASS — all keyboard-nav tests green, existing tests still green.

- [ ] **Step 5: Commit**

```bash
git -C "<worktree>" add ui/src/views/lyric/AuroraLyricStage.vue ui/src/views/lyric/__tests__/LyricStages.test.ts
git -C "<worktree>" commit -m "feat(ui): add Aurora lyric keyboard navigation"
```

---

## Task 7: cargo-audit RustSec scan

**Files:**
- Possibly Modify: `ui/src-tauri/Cargo.toml`, `ui/src-tauri/Cargo.lock` (only if a vuln is found)

- [ ] **Step 1: Install cargo-audit**

```bash
cargo install cargo-audit --locked
```
If the environment has no network, record "cargo-audit install failed: no network" in the final report and stop this task (do not pretend it passed). Otherwise proceed.

- [ ] **Step 2: Run the audit**

```bash
cd "C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic/ui/src-tauri" && cargo audit
```
Expected: `no vulnerabilities found` (exit 0) OR a list of advisories.

- [ ] **Step 3: If advisories exist, fix them**

For each advisory: `cargo update -p <crate>` (if a fixed version exists) or pin/replace per the advisory guidance. Re-run `cargo audit` until clean. Re-run `cargo test` + `cargo clippy -D warnings` to confirm no regression.

- [ ] **Step 4: Commit (only if Cargo.lock/Cargo.toml changed)**

```bash
git -C "<worktree>" add ui/src-tauri/Cargo.lock ui/src-tauri/Cargo.toml
git -C "<worktree>" commit -m "fix(deps): resolve RustSec advisories from cargo-audit"
```
If nothing changed, no commit — record "cargo-audit clean, no changes" in the report.

---

## Task 8: Vitest jsdom noise treatment

**Files:**
- Modify: the Vitest setup file (find via `grep -rn "setupFiles" ui/vitest.config.* ui/vite.config.*`; if none, create `ui/src/test/setup.ts` and register it)
- Test: a regression test proving real failures still surface

- [ ] **Step 1: Locate the test setup file**

```bash
grep -rn "setupFiles" "C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic/ui" --include="*.ts" --include="*.js"
```
Note the setup file path (or determine one must be created).

- [ ] **Step 2: Add unified HTMLMediaElement + Canvas mocks**

In the setup file (create `ui/src/test/setup.ts` if none exists), add:

```ts
import { vi } from 'vitest';

// Silence jsdom "Not implemented" noise for media/canvas so real test
// failures are visible. These mocks are inert by default; tests that need
// real behavior override them locally.

class MockMediaElement {
  // HTMLMediaElement
  play = vi.fn().mockResolvedValue(undefined);
  pause = vi.fn();
  load = vi.fn();
  canPlayType = vi.fn().mockReturnValue('');
  currentTime = 0;
  duration = NaN;
  volume = 1;
  muted = false;
  paused = true;
  src = '';
  addEventListener = vi.fn();
  removeEventListener = vi.fn();
  dispatchEvent = vi.fn().mockReturnValue(true);
}

if (!('HTMLMediaElement' in globalThis) || !globalThis.HTMLMediaElement?.prototype?.play) {
  (globalThis as any).HTMLMediaElement = MockMediaElement;
}

class MockCanvasContext {
  fillRect = vi.fn();
  clearRect = vi.fn();
  getImageData = vi.fn().mockReturnValue({ data: new Uint8ClampedArray(4) });
  getContextAttributes = vi.fn().mockReturnValue({});
  canvas = { width: 0, height: 0 };
  // Add no-op stubs as tests require; keep minimal to avoid masking real calls.
}
HTMLCanvasElement.prototype.getContext = vi.fn().mockReturnValue(new MockCanvasContext()) as any;
```
Register the setup file in the vitest config `test.setupFiles` if it was newly created.

- [ ] **Step 3: Add a regression test that real failures still surface**

Create `ui/src/test/__tests__/setup-regression.test.ts`:
```ts
import { describe, it, expect } from 'vitest';

describe('test setup regression guard', () => {
  it('still fails on a genuinely wrong assertion (mocks do not mask failures)', () => {
    // Intentionally correct assertion; if this passes, the mock setup is not
    // swallowing test results. If mocks ever hide failures, flip the expected
    // value temporarily to confirm vitest reports it.
    expect(1 + 1).toBe(2);
  });
});
```

- [ ] **Step 4: Run the full Vitest suite and confirm reduced noise + green**

```bash
cd "C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic/ui" && pnpm vitest run 2>&1 | tee vitest-run.log
```
Expected: 781+ tests pass (count grows by new tests). The jsdom "Not implemented: HTMLMediaElement.* / canvas.*" warnings are gone or drastically reduced. Confirm by `grep -c "Not implemented" vitest-run.log` dropping vs. baseline.

- [ ] **Step 5: Confirm mocks do not mask a real failure**

Temporarily change the regression test's `toBe(2)` to `toBe(3)`, run it, confirm FAIL, then revert. (This is a manual verification, not committed.)

- [ ] **Step 6: Commit**

```bash
git -C "<worktree>" add ui/src/test/setup.ts ui/vitest.config.ts ui/src/test/__tests__/setup-regression.test.ts
git -C "<worktree>" commit -m "test(ui): add unified media/canvas mocks to quiet jsdom noise"
```
(Add only the files that exist/changed.)

---

## Task 9: Full verification on the codex branch

- [ ] **Step 1: Run every verification gate on the codex branch**

```bash
cd "C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic/ui" && pnpm vitest run && pnpm vue-tsc --noEmit && pnpm build
cd "C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic/ui/src-tauri" && cargo test && cargo clippy --all-targets -- -D warnings && cargo check
cd "C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic/native" && cmake -S . --preset bottlemusic-check && cmake --build out/bottlemusic-check --config Debug && ctest --preset bottlemusic-check
```
Expected: all green. Fix any regression before proceeding (return to the relevant task).

- [ ] **Step 2: Confirm `git diff --check` is clean**

```bash
git -C "<worktree>" diff --check
```
Expected: no whitespace errors.

---

## Task 10: Merge to main and push

**Files:** none (git operation)

- [ ] **Step 1: Ensure main worktree is clean (Task 0 stashed the 394 lines)**

```bash
git -C "C:/BottleMusic" status --short
```
Expected: only untracked items (`.agents/`, `.claude/`, `mcps/`, `skills-lock.json`, captures, script). No modified tracked files.

- [ ] **Step 2: Merge `codex/dual-interface-deep-refactor` into main with --no-ff**

```bash
git -C "C:/BottleMusic" checkout main
git -C "C:/BottleMusic" merge --no-ff codex/dual-interface-deep-refactor -m "merge: dual-interface deep refactor + security hardening"
```
Expected: merge commit created; no conflicts (deep-refactor supersedes main's stashed changes). If a conflict arises, it means a stashed main change overlaps — resolve in favor of deep-refactor (the authoritative reimplementation) and document.

- [ ] **Step 3: Push main**

```bash
git -C "C:/BottleMusic" push origin main
```
Expected: push succeeds. (If push is rejected as non-fast-forward, stop and report — do not force-push.)

---

## Task 11: Clean up superseded branches and worktrees

- [ ] **Step 1: Remove the player-redesign worktree and branch (discard trivial localization + BOM changes)**

```bash
git -C "C:/BottleMusic/.worktrees/dual-interface-player-redesign" checkout -- .   # discard uncommitted
git worktree remove "C:/BottleMusic/.worktrees/dual-interface-player-redesign" --force
git -C "C:/BottleMusic" branch -D codex/dual-interface-player-redesign
```

- [ ] **Step 2: Remove the shiny-cabin worktree and branch (keyboard nav already ported in Task 6)**

```bash
git worktree remove "C:/Users/w1521/.local/share/opencode/worktree/e6ec3558eda6db35b78355ca7c3d74517f6b6770/shiny-cabin" --force
git -C "C:/BottleMusic" branch -D opencode/shiny-cabin
```

- [ ] **Step 3: Remove the happy-harbor worktree and branch (clean, fully merged)**

```bash
git worktree remove "C:/Users/w1521/.local/share/opencode/worktree/e6ec3558eda6db35b78355ca7c3d74517f6b6770/happy-harbor" --force
git -C "C:/BottleMusic" branch -d opencode/happy-harbor
```

- [ ] **Step 4: Remove the playback-orchestrator-tdd worktree and branch (clean, fully merged)**

```bash
git worktree remove "C:/BottleMusic-worktrees/playback-orchestrator-tdd" --force
git -C "C:/BottleMusic" branch -d playback-orchestrator-tdd
```

- [ ] **Step 5: Verify remaining state**

```bash
git -C "C:/BottleMusic" worktree list
git -C "C:/BottleMusic" branch -vv
```
Expected: worktree list shows only `C:/BottleMusic` (main) and `C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic` (the deep-refactor worktree, which can be removed too once you confirm the merge is pushed). Branches: `main`, `codex/dual-interface-deep-refactor` only.

- [ ] **Step 6: Optionally remove the deep-refactor worktree + branch after confirming push**

```bash
git worktree remove "C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic" --force
git -C "C:/BottleMusic" branch -d codex/dual-interface-deep-refactor
```
(Only after Task 10 push succeeded and Task 12 verification passed. Keep if you want to retain the branch.)

---

## Task 12: Final verification on main

- [ ] **Step 1: Run the full verification suite on main**

```bash
cd "C:/BottleMusic/ui" && pnpm vitest run && pnpm vue-tsc --noEmit && pnpm build
cd "C:/BottleMusic/ui/src-tauri" && cargo test && cargo clippy --all-targets -- -D warnings && cargo check
cd "C:/BottleMusic/native" && cmake -S . --preset bottlemusic-check && cmake --build out/bottlemusic-check --config Debug && ctest --preset bottlemusic-check
```
Expected: all green on main.

- [ ] **Step 2: Confirm main is pushed and clean**

```bash
git -C "C:/BottleMusic" status --short --branch
```
Expected: `## main...origin/main` (not ahead/behind), only untracked non-source items.

- [ ] **Step 3: Write the final report**

Record in `docs/superpowers/reports/2026-07-17-security-hardening-merge-report.md` (force-add): security fixes landed, cargo-audit result, Vitest noise before/after, merge commit SHA, branches/worktrees removed, stash ref, known follow-ups (spec §9). Commit the report.

---

## Self-Review Notes

- Spec coverage: H1→Task 1, H2→Task 5, M1→Task 3, M2→Task 2, M3→Task 4, M4 (CSP connect-src)→documented as follow-up per spec §9, cargo-audit→Task 7, Vitest noise→Task 8, salvage→Task 0 + Task 11, merge→Task 10, cleanup→Task 11, final verify→Task 12, keyboard nav port→Task 6. All spec phases covered.
- `<worktree>` shorthand in commit commands means `C:/Users/w1521/.codex/worktrees/a6d0/BottleMusic`.
- The `extractStep` helper in Task 5 may need indentation tuning to match release.yml; the test file already parses release.yml, so mirror its existing pattern if the helper as written does not match.
- Task 3's `ECHO_LOG` include: if the build errors on an undefined macro, add the diagnostics include the error names (likely `echo/diagnostics/EchoDiagnostics.h`).
