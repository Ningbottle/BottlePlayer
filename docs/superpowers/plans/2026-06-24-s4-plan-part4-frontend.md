# S4 Implementation Plan — Part 4: Frontend (Phase 4.1c + 4.2c)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire the frontend to choose between the existing HTML5 Audio backend and the new native (Tauri command) backend. Build the `EqualizerPanel.vue` UI and integrate it below `PlayerBar.vue`.

**Architecture:** `PlayerBackend` interface with two implementations: `Html5AudioBackend` (wraps the existing `<audio>` element) and `NativePlaybackBackend` (calls `invoke('playback_*')` and listens to `playback_event`). `playerStore` gains a `backend` field, a `initPlayerBackend()` startup function, and a `handlePlaybackEvent` function that keeps `currentTime`/`isPlaying` in sync.

**Tech Stack:** Vue 3, TypeScript, Pinia-style reactive, Tauri 2 invoke + listen API, Vitest + jsdom, localStorage.

## Global Constraints

(All S4 constraints from Parts 1-3 apply.)

Additional S4 frontend constraints:
- **Backend choice is at app startup, not user-togglable.** `initPlayerBackend()` runs once in `App.vue onMounted`.
- **Native is default, HTML5 is fallback.** Try `playback_initialize(MFS)` first; if it returns false, fall back to HTML5 silently.
- **Event subscription drives reactive state.** `playerStore.currentTime` is updated by the event handler, not polled.
- **EQ state must be pushed to backend on app start.** Read localStorage `eqBands/eqEnabled/activePreset` and call `invoke('playback_set_eq_bands', ...)` after backend init.
- **Tests use Vitest + jsdom at the highest seam.** Mock `invoke` and `listen` (do not test internals). Existing tests in `ui/src/api/__tests__/` and `ui/src/views/__tests__/` are the model.

## File Map

| File | Responsibility |
|---|---|
| `ui/src/api/playerBackend.ts` | New: PlayerBackend interface + PlaybackEvent type. |
| `ui/src/api/html5Backend.ts` | New: wraps existing HTMLAudioElement. |
| `ui/src/api/nativeBackend.ts` | New: Tauri invoke + listen wrapper. |
| `ui/src/api/playerStore.ts` | Modify: add backend field, initPlayerBackend, event handler, action delegation. |
| `ui/src/components/EqualizerPanel.vue` | New: collapsible EQ panel with 5 sliders + 4 presets. |
| `ui/src/components/PlayerBar.vue` | Modify: add `<EqualizerPanel v-model="eqExpanded" />`. |
| `ui/src/views/__tests__/playerBackend.test.ts` | New: mock-based PlayerBackend test. |
| `ui/src/components/__tests__/EqualizerPanel.test.ts` | New: component mount test. |

---

### Task 22: PlayerBackend interface

**Files:**
- Create: `ui/src/api/playerBackend.ts`

**Interfaces:**
- Consumes: nothing
- Produces: `PlayerBackend` interface + `PlaybackEvent` type

- [ ] **Step 1: Create the file**

```typescript
// ui/src/api/playerBackend.ts

export type PlaybackEventType = 'position' | 'state' | 'ended' | 'error';

export interface PlaybackEvent {
  type: PlaybackEventType;
  position?: number;
  duration?: number;
  state?: string;
  error?: string;
}

export type PlaybackState = {
  state: string;  // 'playing' | 'paused' | 'stopped' | 'uninitialized'
  position: number;
  duration: number;
};

export interface PlayerBackend {
  readonly kind: 'html5' | 'native';
  initialize(): Promise<boolean>;
  playUrl(url: string): Promise<boolean>;
  pause(): Promise<void>;
  resume(): Promise<void>;
  stop(): Promise<void>;
  seek(seconds: number): Promise<void>;
  setVolume(v: number): Promise<void>;
  setRate(r: number): Promise<void>;
  getState(): Promise<PlaybackState>;
  shutdown(): Promise<void>;
  onEvent(cb: (e: PlaybackEvent) => void): () => void;
}
```

- [ ] **Step 2: Build (TypeScript)**

Run: `pnpm exec vue-tsc --noEmit 2>&1 | Select-Object -Last 3`
Expected: 0 errors.

- [ ] **Step 3: Commit**

```bash
git add ui/src/api/playerBackend.ts
git commit -m "feat(s4): add PlayerBackend interface + PlaybackEvent type"
```

---

### Task 23: Html5AudioBackend

**Files:**
- Create: `ui/src/api/html5Backend.ts`

**Interfaces:**
- Consumes: `PlayerBackend` interface, `HTMLAudioElement`
- Produces: `Html5AudioBackend` class implementing `PlayerBackend`

- [ ] **Step 1: Create the file**

```typescript
// ui/src/api/html5Backend.ts
import type { PlayerBackend, PlaybackEvent, PlaybackState } from './playerBackend';

export class Html5AudioBackend implements PlayerBackend {
  readonly kind = 'html5' as const;

  constructor(private audio: HTMLAudioElement) {
    // Apply persisted volume/rate on creation
    this.audio.volume = parseFloat(localStorage.getItem('player_volume') || '0.7');
  }

  async initialize(): Promise<boolean> { return true; }

  async playUrl(url: string): Promise<boolean> {
    this.audio.src = url;
    try {
      await this.audio.play();
      return true;
    } catch (e) {
      console.warn('Html5AudioBackend play failed:', e);
      return false;
    }
  }

  async pause(): Promise<void> { this.audio.pause(); }
  async resume(): Promise<void> { await this.audio.play(); }
  async stop(): Promise<void> {
    this.audio.pause();
    this.audio.currentTime = 0;
  }

  async seek(seconds: number): Promise<void> {
    this.audio.currentTime = seconds;
  }

  async setVolume(v: number): Promise<void> {
    this.audio.volume = v;
    localStorage.setItem('player_volume', String(v));
  }

  async setRate(r: number): Promise<void> { this.audio.playbackRate = r; }

  async getState(): Promise<PlaybackState> {
    return {
      state: this.audio.paused ? 'paused' : 'playing',
      position: this.audio.currentTime,
      duration: this.audio.duration || 0,
    };
  }

  async shutdown(): Promise<void> { this.audio.pause(); }

  onEvent(cb: (e: PlaybackEvent) => void): () => void {
    const handlers: Record<string, () => void> = {
      timeupdate: () => cb({
        type: 'position',
        position: this.audio.currentTime,
        duration: this.audio.duration,
      }),
      play: () => cb({ type: 'state', state: 'playing' }),
      pause: () => cb({ type: 'state', state: 'paused' }),
      ended: () => cb({ type: 'ended' }),
      error: () => cb({ type: 'error', error: 'playback failed' }),
    };
    for (const [evt, h] of Object.entries(handlers)) {
      this.audio.addEventListener(evt, h);
    }
    return () => {
      for (const [evt, h] of Object.entries(handlers)) {
        this.audio.removeEventListener(evt, h);
      }
    };
  }
}
```

- [ ] **Step 2: Type-check**

Run: `pnpm exec vue-tsc --noEmit 2>&1 | Select-Object -Last 3`
Expected: 0 errors.

- [ ] **Step 3: Commit**

```bash
git add ui/src/api/html5Backend.ts
git commit -m "feat(s4): add Html5AudioBackend"
```

---

### Task 24: NativePlaybackBackend

**Files:**
- Create: `ui/src/api/nativeBackend.ts`

**Interfaces:**
- Consumes: `PlayerBackend` interface, Tauri `invoke` + `listen`
- Produces: `NativePlaybackBackend` class

- [ ] **Step 1: Create the file**

```typescript
// ui/src/api/nativeBackend.ts
import { invoke } from '@tauri-apps/api/core';
import { listen, type UnlistenFn } from '@tauri-apps/api/event';
import type { PlayerBackend, PlaybackEvent, PlaybackState } from './playerBackend';

export class NativePlaybackBackend implements PlayerBackend {
  readonly kind = 'native' as const;
  private initialized = false;
  private backendUsed: 'mfs' | 'mfp' | null = null;

  async initialize(): Promise<boolean> {
    if (this.initialized) return true;
    // Try MFS first (IMFMediaSession + EQ)
    let ok = await invoke<boolean>('playback_initialize', { backend: 1 });
    if (ok) {
      this.backendUsed = 'mfs';
    } else {
      // Fallback: MFP (MFPlay, no EQ)
      ok = await invoke<boolean>('playback_initialize', { backend: 0 });
      if (ok) this.backendUsed = 'mfp';
    }
    if (!ok) return false;
    this.initialized = true;
    return true;
  }

  get activeBackendKind() { return this.backendUsed; }

  async playUrl(url: string): Promise<boolean> {
    return invoke<boolean>('playback_play_url', { url });
  }

  async pause(): Promise<void> { await invoke('playback_pause'); }
  async resume(): Promise<void> { await invoke('playback_resume'); }
  async stop(): Promise<void> { await invoke('playback_stop'); }
  async seek(seconds: number): Promise<void> {
    await invoke('playback_seek', { seconds });
  }
  async setVolume(v: number): Promise<void> {
    await invoke('playback_set_volume', { volume: v });
  }
  async setRate(r: number): Promise<void> {
    await invoke('playback_set_rate', { rate: r });
  }

  async getState(): Promise<PlaybackState> {
    const json = await invoke<string>('playback_get_state');
    return JSON.parse(json);
  }

  async shutdown(): Promise<void> { await invoke('playback_shutdown'); }

  onEvent(cb: (e: PlaybackEvent) => void): () => void {
    let unlisten: UnlistenFn | null = null;
    listen<string>('playback_event', (ev) => {
      try {
        const data = JSON.parse(ev.payload);
        cb(data);
      } catch (e) {
        console.warn('Failed to parse playback_event:', e);
      }
    }).then((u) => { unlisten = u; });
    return () => { unlisten?.(); };
  }
}
```

- [ ] **Step 2: Type-check**

Run: `pnpm exec vue-tsc --noEmit 2>&1 | Select-Object -Last 3`
Expected: 0 errors.

- [ ] **Step 3: Commit**

```bash
git add ui/src/api/nativeBackend.ts
git commit -m "feat(s4): add NativePlaybackBackend (Tauri invoke + listen)"
```

---

### Task 25: playerStore changes — backend field, initPlayerBackend, event handler

**Files:**
- Modify: `ui/src/api/playerStore.ts`

**Interfaces:**
- Consumes: `Html5AudioBackend`, `NativePlaybackBackend`
- Produces: `playerStore.backend` field, `initPlayerBackend()`, `handlePlaybackEvent`, EQ state

- [ ] **Step 1: Add imports**

```typescript
import { Html5AudioBackend } from './html5Backend';
import { NativePlaybackBackend } from './nativeBackend';
import type { PlayerBackend, PlaybackEvent } from './playerBackend';
import { invoke } from '@tauri-apps/api/core';
```

- [ ] **Step 2: Extend `PlayerState` interface**

```typescript
interface PlayerState {
  // ... existing fields ...
  backend: 'html5' | 'native' | null;
  eqEnabled: boolean;
  eqBands: number[];  // 5 values, dB
  activePreset: string;
}
```

- [ ] **Step 3: Add module-level backend variable**

```typescript
let activeBackend: PlayerBackend | null = null;
let eventUnsub: (() => void) | null = null;
```

- [ ] **Step 4: Initialize the new fields in `playerStore` declaration**

```typescript
export const playerStore = reactive<PlayerState>({
  // ... existing fields ...
  backend: null,
  eqEnabled: localStorage.getItem('player_eq_enabled') === 'true',
  eqBands: JSON.parse(localStorage.getItem('player_eq_bands') || '[0,0,0,0,0]'),
  activePreset: localStorage.getItem('player_eq_preset') || 'Flat',
});
```

- [ ] **Step 5: Add `initPlayerBackend` function**

```typescript
export async function initPlayerBackend() {
  if (activeBackend) return;  // already initialized

  // Try native first
  const native = new NativePlaybackBackend();
  const ok = await native.initialize().catch((e) => {
    console.warn('Native playback init failed:', e);
    return false;
  });

  if (ok) {
    activeBackend = native;
    playerStore.backend = 'native';
  } else {
    // Fallback to HTML5
    if (!playerStore.audio) {
      console.error('No HTML5 audio element available for fallback');
      return;
    }
    activeBackend = new Html5AudioBackend(playerStore.audio);
    playerStore.backend = 'html5';
  }

  // Wire event subscription
  eventUnsub = activeBackend.onEvent(handlePlaybackEvent);

  // Push EQ state to native backend
  if (playerStore.backend === 'native' && playerStore.eqEnabled) {
    await invoke('playback_set_eq_enabled', { enabled: true }).catch(() => {});
    await invoke('playback_set_eq_bands', { gains: playerStore.eqBands }).catch(() => {});
  }
}

function handlePlaybackEvent(e: PlaybackEvent) {
  if (e.type === 'position') {
    if (typeof e.position === 'number') playerStore.currentTime = e.position;
    if (typeof e.duration === 'number') playerStore.duration = e.duration;
  } else if (e.type === 'state') {
    playerStore.isPlaying = e.state === 'playing';
    playerStore.errorMsg = '';
  } else if (e.type === 'ended') {
    next();
  } else if (e.type === 'error' && e.error) {
    playerStore.errorMsg = e.error;
  }
}
```

- [ ] **Step 6: Type-check**

Run: `pnpm exec vue-tsc --noEmit 2>&1 | Select-Object -Last 3`
Expected: 0 errors.

- [ ] **Step 7: Commit**

```bash
git add ui/src/api/playerStore.ts
git commit -m "feat(s4): add playerStore backend field + initPlayerBackend + event handler"
```

---

- [ ] **Step 7: Wire `initPlayerBackend()` into `App.vue onMounted`**

In `ui/src/App.vue`, add:

```typescript
import { onMounted } from 'vue';
import { initPlayerBackend } from './api/playerStore';

onMounted(async () => {
  await initPlayerBackend();
});
```

(Apply this change here, not in a separate task — it's part of the playerStore changes.)

- [ ] **Step 8: Commit**

```bash
git add ui/src/api/playerStore.ts ui/src/App.vue
git commit -m "feat(s4): add playerStore backend field + initPlayerBackend + event handler"
```

---

### Task 26: playerStore action delegation (playTrack/togglePlay/seek/etc → activeBackend)

**Files:**
- Modify: `ui/src/api/playerStore.ts`

**Interfaces:**
- Consumes: existing `playTrack`, `togglePlay`, `seek`, `setVolume`, `setQuality` functions
- Produces: All actions delegate to `activeBackend` instead of touching `playerStore.audio` directly

- [ ] **Step 1: Update `playTrack` to use active backend**

Find `playTrack` in `playerStore.ts` and replace the audio-element manipulation with a backend call:

```typescript
export async function playTrack(track: Track) {
  if (!activeBackend) {
    console.warn('Backend not initialized; falling through to legacy logic');
    // ... keep existing legacy code as fallback ...
    return;
  }
  const url = track.PlayURL || track.url;
  if (!url) return;
  const ok = await activeBackend.playUrl(url);
  if (ok) {
    playerStore.currentTrack = track;
    // ... existing queue/quality logic ...
  } else {
    playerStore.errorMsg = '播放失败';
  }
}
```

(Keep the existing queue management logic; the change is only the audio engine call.)

- [ ] **Step 2: Update `togglePlay`**

```typescript
export async function togglePlay() {
  if (!activeBackend) return;
  if (playerStore.isPlaying) {
    await activeBackend.pause();
  } else {
    await activeBackend.resume();
  }
}
```

- [ ] **Step 3: Update `seek`**

```typescript
export async function seek(seconds: number) {
  if (!activeBackend) return;
  await activeBackend.seek(seconds);
  playerStore.currentTime = seconds;
}
```

- [ ] **Step 4: Update `setVolume`**

```typescript
export async function setVolume(v: number) {
  playerStore.volume = v;
  localStorage.setItem('player_volume', String(v));
  if (activeBackend) await activeBackend.setVolume(v);
}
```

- [ ] **Step 5: Type-check**

Run: `pnpm exec vue-tsc --noEmit 2>&1 | Select-Object -Last 3`
Expected: 0 errors.

- [ ] **Step 6: Commit**

```bash
git add ui/src/api/playerStore.ts
git commit -m "refactor(s4): playerStore actions delegate to activeBackend"
```

---

### Task 27: EqualizerPanel.vue skeleton

**Files:**
- Create: `ui/src/components/EqualizerPanel.vue`

**Interfaces:**
- Consumes: `playerStore`, Tauri `invoke`
- Produces: Collapsible panel with 5 sliders + preset dropdown

- [ ] **Step 1: Create the component**

```vue
<!-- ui/src/components/EqualizerPanel.vue -->
<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { playerStore } from '../api/playerStore';

const props = defineProps<{ modelValue: boolean }>();
const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void }>();

const expanded = computed({
  get: () => props.modelValue,
  set: (v: boolean) => emit('update:modelValue', v),
});

const PRESETS: Record<string, number[]> = {
  'Flat':       [0, 0, 0, 0, 0],
  'Bass Boost': [6, 4, 0, 0, 0],
  'Vocal':      [0, 2, 4, 2, 0],
  'Rock':       [4, 2, -2, 2, 4],
};

const BAND_FREQS = ['60Hz', '230Hz', '910Hz', '3.6kHz', '14kHz'];

const bandGains = ref<number[]>([...playerStore.eqBands]);
watch(() => playerStore.eqBands, (v) => { bandGains.value = [...v]; });

const disabled = computed(() => playerStore.backend !== 'native');

async function onSliderInput() {
  playerStore.eqBands = [...bandGains.value];
  localStorage.setItem('player_eq_bands', JSON.stringify(bandGains.value));
  if (playerStore.backend === 'native') {
    try {
      await invoke('playback_set_eq_bands', { gains: bandGains.value });
    } catch (e) { console.warn('set_eq_bands failed', e); }
  }
}

async function toggleEnabled() {
  playerStore.eqEnabled = !playerStore.eqEnabled;
  localStorage.setItem('player_eq_enabled', String(playerStore.eqEnabled));
  if (playerStore.backend === 'native') {
    try {
      await invoke('playback_set_eq_enabled', { enabled: playerStore.eqEnabled });
    } catch (e) { console.warn('set_eq_enabled failed', e); }
  }
}

function applyPreset(name: string) {
  if (PRESETS[name]) {
    bandGains.value = [...PRESETS[name]];
    playerStore.activePreset = name;
    localStorage.setItem('player_eq_preset', name);
    onSliderInput();
  }
}

function formatGain(g: number) {
  return (g > 0 ? '+' : '') + g.toFixed(1) + 'dB';
}
</script>

<template>
  <div class="eq-panel" :class="{ expanded, disabled }">
    <button class="eq-toggle" @click="expanded = !expanded">
      <span class="eq-status">EQ {{ playerStore.eqEnabled ? 'ON' : 'OFF' }}</span>
      <span class="chevron">{{ expanded ? '▾' : '▸' }}</span>
    </button>
    <div v-if="expanded" class="eq-controls">
      <div class="eq-row">
        <label class="eq-enable">
          <input
            type="checkbox"
            :checked="playerStore.eqEnabled"
            :disabled="disabled"
            @change="toggleEnabled"
          />
          Enable Equalizer
        </label>
        <select
          class="eq-preset"
          :value="playerStore.activePreset"
          :disabled="disabled || !playerStore.eqEnabled"
          @change="applyPreset(($event.target as HTMLSelectElement).value)"
        >
          <option v-for="(_, name) in PRESETS" :key="name" :value="name">{{ name }}</option>
        </select>
      </div>
      <div class="eq-bands">
        <div v-for="(gain, i) in bandGains" :key="i" class="eq-band">
          <input
            type="range"
            min="-12"
            max="12"
            step="0.5"
            v-model.number="bandGains[i]"
            :disabled="disabled || !playerStore.eqEnabled"
            orient="vertical"
            class="band-slider"
            @input="onSliderInput"
          />
          <span class="band-freq">{{ BAND_FREQS[i] }}</span>
          <span class="band-gain">{{ formatGain(gain) }}</span>
        </div>
      </div>
      <p v-if="disabled" class="eq-hint">
        Native backend not available — EQ disabled. Using HTML5 fallback.
      </p>
    </div>
  </div>
</template>

<style scoped>
.eq-panel { /* styles here */ }
</style>
```

- [ ] **Step 2: Type-check**

Run: `pnpm exec vue-tsc --noEmit 2>&1 | Select-Object -Last 3`
Expected: 0 errors.

- [ ] **Step 3: Commit**

```bash
git add ui/src/components/EqualizerPanel.vue
git commit -m "feat(s4): add EqualizerPanel.vue (5 sliders + 4 presets + localStorage)"
```

---

### Task 28: PlayerBar.vue EQ panel integration

**Files:**
- Modify: `ui/src/components/PlayerBar.vue`

**Interfaces:**
- Consumes: `EqualizerPanel` (Task 27)
- Produces: Panel rendered below the player bar

- [ ] **Step 1: Add import + state**

```typescript
import EqualizerPanel from './EqualizerPanel.vue';
const eqExpanded = ref(false);
```

- [ ] **Step 2: Render the panel below the existing player bar markup**

Find the closing `</div>` of the main player bar container (after the playback controls). Add:

```vue
<EqualizerPanel v-model="eqExpanded" />
```

- [ ] **Step 3: Type-check**

Run: `pnpm exec vue-tsc --noEmit 2>&1 | Select-Object -Last 3`
Expected: 0 errors.

- [ ] **Step 4: Commit**

```bash
git add ui/src/components/PlayerBar.vue
git commit -m "feat(s4): integrate EqualizerPanel below PlayerBar"
```

---

### Task 29: Frontend tests — PlayerBackend mock + EqualizerPanel

**Files:**
- Create: `ui/src/api/__tests__/playerBackend.test.ts`
- Create: `ui/src/components/__tests__/EqualizerPanel.test.ts`

**Interfaces:**
- Consumes: Vitest, mock Tauri `invoke` + `listen`
- Produces: Tests that prove backend selection and EQ event flow

- [ ] **Step 1: Create `playerBackend.test.ts`**

```typescript
// ui/src/api/__tests__/playerBackend.test.ts
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { NativePlaybackBackend } from '../nativeBackend';
import { Html5AudioBackend } from '../html5Backend';

// Mock Tauri invoke
vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn(),
}));

vi.mock('@tauri-apps/api/event', () => ({
  listen: vi.fn().mockResolvedValue(() => {}),
}));

import { invoke } from '@tauri-apps/api/core';

describe('NativePlaybackBackend', () => {
  beforeEach(() => { vi.clearAllMocks(); });

  it('initializes with MFS first, falls back to MFP if MFS fails', async () => {
    (invoke as any)
      .mockResolvedValueOnce(false)  // MFS fails
      .mockResolvedValueOnce(true);  // MFP succeeds

    const backend = new NativePlaybackBackend();
    const ok = await backend.initialize();

    expect(ok).toBe(true);
    expect(backend.activeBackendKind).toBe('mfp');
    expect(invoke).toHaveBeenCalledWith('playback_initialize', { backend: 1 });
    expect(invoke).toHaveBeenCalledWith('playback_initialize', { backend: 0 });
  });

  it('initializes with MFS only when MFS succeeds', async () => {
    (invoke as any).mockResolvedValueOnce(true);
    const backend = new NativePlaybackBackend();
    const ok = await backend.initialize();
    expect(ok).toBe(true);
    expect(backend.activeBackendKind).toBe('mfs');
    expect(invoke).toHaveBeenCalledTimes(1);
  });

  it('returns false when both backends fail', async () => {
    (invoke as any).mockResolvedValue(false);
    const backend = new NativePlaybackBackend();
    const ok = await backend.initialize();
    expect(ok).toBe(false);
  });

  it('playUrl forwards to invoke with correct args', async () => {
    (invoke as any).mockResolvedValue(true);
    const backend = new NativePlaybackBackend();
    await backend.initialize();
    await backend.playUrl('https://example.com/song.mp3');
    expect(invoke).toHaveBeenCalledWith('playback_play_url', { url: 'https://example.com/song.mp3' });
  });
});

describe('Html5AudioBackend', () => {
  it('playUrl triggers audio.play()', async () => {
    const audio = document.createElement('audio') as HTMLAudioElement;
    audio.play = vi.fn().mockResolvedValue(undefined);
    const backend = new Html5AudioBackend(audio);
    const ok = await backend.playUrl('https://example.com/song.mp3');
    expect(ok).toBe(true);
    expect(audio.play).toHaveBeenCalled();
    expect(audio.src).toBe('https://example.com/song.mp3');
  });
});
```

- [ ] **Step 2: Create `EqualizerPanel.test.ts`**

```typescript
// ui/src/components/__tests__/EqualizerPanel.test.ts
import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount } from '@vue/test-utils';
import { nextTick } from 'vue';
import EqualizerPanel from '../EqualizerPanel.vue';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(undefined),
}));

import { invoke } from '@tauri-apps/api/core';
import { playerStore } from '../../api/playerStore';

describe('EqualizerPanel', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    playerStore.backend = 'native';
    playerStore.eqEnabled = true;
    playerStore.eqBands = [0, 0, 0, 0, 0];
    playerStore.activePreset = 'Flat';
  });

  it('renders 5 band sliders when expanded', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const sliders = wrapper.findAll('input[type="range"]');
    expect(sliders.length).toBe(5);
  });

  it('slider change calls invoke playback_set_eq_bands', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const slider = wrapper.find('input[type="range"]');
    await slider.setValue('6');
    await nextTick();
    expect(invoke).toHaveBeenCalledWith('playback_set_eq_bands', {
      gains: expect.arrayContaining([6, 0, 0, 0, 0]),
    });
  });

  it('applies preset when dropdown changes', async () => {
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    const select = wrapper.find('select');
    await select.setValue('Bass Boost');
    await nextTick();
    expect(playerStore.eqBands).toEqual([6, 4, 0, 0, 0]);
    expect(invoke).toHaveBeenCalledWith('playback_set_eq_bands', {
      gains: [6, 4, 0, 0, 0],
    });
  });

  it('shows hint when backend is html5', async () => {
    playerStore.backend = 'html5';
    const wrapper = mount(EqualizerPanel, { props: { modelValue: true } });
    expect(wrapper.text()).toContain('Native backend not available');
  });
});
```

- [ ] **Step 3: Run all tests**

Run: `pnpm test -- --run 2>&1 | Select-Object -Last 10`
Expected: all tests pass, including new ones.

- [ ] **Step 4: Commit**

```bash
git add ui/src/api/__tests__/playerBackend.test.ts ui/src/components/__tests__/EqualizerPanel.test.ts
git commit -m "test(s4): add PlayerBackend mock + EqualizerPanel component tests"
```

---

**End of Part 4 — Phase 4.1c + 4.2c complete. Continue with Part 5 (Polish).**
