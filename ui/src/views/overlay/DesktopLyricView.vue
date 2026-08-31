<script setup lang="ts">
/**
 * DesktopLyricView — 桌面歌词条（/overlay/lyric）
 *
 * 透明置顶横条：当前行大字（卡拉 OK 填充）+ 次行小字。
 * 歌词数据在浮层内按同步曲目自取（与主窗口同一条 fetchLyrics 路径），
 * 时间轴经 playerSync 同步。窗口固定在屏幕顶部，可横向拖放或选择锚点。
 */
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { closeCurrentWindow, onCurrentWindowResized, readCurrentWindowFrame } from '../../platform/tauri/windows';
import { PhGear, PhPause, PhPlay, PhSkipBack, PhSkipForward, PhX } from '@phosphor-icons/vue';
import { onPlayerState, sendPlayerCommand, applySyncedTheme, pinOverlayThemeDark, type PlayerSyncState } from '../../playback/index';
import { isTauriRuntime, moveCurrentOverlayTo, settleCurrentOverlay, loadLyricPrefs, saveLyricPrefs, saveLyricSize } from '../../platform/tauri/windows';
import type { LyricPrefs } from '../../platform/tauri/windows';
import { fetchLyrics, type LyricLine } from '../lyric/useLyricStage';

const state = ref<PlayerSyncState | null>(null);
const lines = ref<LyricLine[]>([]);
const lyricError = ref(false);

const hasTrack = computed(() => !!state.value?.hash);
const currentTime = computed(() => state.value?.currentTime ?? 0);

const activeIndex = computed(() => {
  const t = currentTime.value + 0.3; // slight lead so the line arrives before the voice
  const arr = lines.value;
  let idx = -1;
  for (let i = 0; i < arr.length; i++) {
    if (arr[i].time <= t) idx = i;
    else break;
  }
  return idx;
});

const activeLine = computed(() => (activeIndex.value >= 0 ? lines.value[activeIndex.value] : null));
const nextLine = computed(() =>
  activeIndex.value >= 0 && activeIndex.value + 1 < lines.value.length
    ? lines.value[activeIndex.value + 1]
    : null,
);

/** Karaoke sweep across the active line, by line duration. */
const fillPct = computed(() => {
  const line = activeLine.value;
  if (!line) return 0;
  const end = nextLine.value?.time ?? line.time + 4;
  const span = Math.max(0.4, end - line.time);
  return Math.max(0, Math.min(100, ((currentTime.value - line.time) / span) * 100));
});

const displayText = computed(() => {
  if (activeLine.value) return activeLine.value.text;
  if (!hasTrack.value) return '未播放';
  if (lyricError.value) return '歌词加载失败';
  if (lines.value.length === 0) return state.value?.name ?? '';
  return '…';
});

let unlisten: (() => void) | null = null;
let fetchToken = 0;

watch(
  () => state.value?.hash,
  async (hash) => {
    const token = ++fetchToken;
    lines.value = [];
    lyricError.value = false;
    if (!hash) return;
    try {
      const fetched = await fetchLyrics({ FileHash: hash } as Parameters<typeof fetchLyrics>[0]);
      if (token !== fetchToken) return;
      lines.value = fetched;
    } catch {
      if (token === fetchToken) lyricError.value = true;
    }
  },
);

const anchors = ['top-left', 'top-center', 'top-right'];
const showAnchors = ref(false);

/** User preferences: font size / density / background opacity (persisted). */
const prefs = ref<LyricPrefs>(loadLyricPrefs());
const showPrefs = ref(false);
const FONT_STEPS = [14, 16, 18, 20, 24] as const;

watch(prefs, (p) => saveLyricPrefs(p), { deep: true });

/** Width persistence: physical px → logical via scaleFactor, 300ms debounce. */
let resizeUnlisten: (() => void) | null = null;
let sizeTimer: number | undefined;

function toggleAnchors(event: MouseEvent): void {
  event.preventDefault();
  showAnchors.value = !showAnchors.value;
}

async function pickAnchor(anchor: string): Promise<void> {
  showAnchors.value = false;
  await moveCurrentOverlayTo(anchor, 'lyric');
}

function onDragRelease(): void {
  void settleCurrentOverlay('lyric');
}

async function closeBar(): Promise<void> {
  if (isTauriRuntime()) {
    await closeCurrentWindow();
  }
}

onMounted(async () => {
  // 桌面歌词条常驻深色：悬浮在任意壁纸上，主题白会读作眩目“白边”
  pinOverlayThemeDark();
  unlisten = await onPlayerState((s) => {
    applySyncedTheme(s);
    state.value = s;
  });
  document.addEventListener('mouseup', onDragRelease);

  if (isTauriRuntime()) {
    resizeUnlisten = await onCurrentWindowResized(({ payload }) => {
      window.clearTimeout(sizeTimer);
      sizeTimer = window.setTimeout(() => {
        void readCurrentWindowFrame().then(({ scaleFactor }) => {
          saveLyricSize(payload.width / (scaleFactor || 1));
        });
      }, 300);
    });
  }
});

onBeforeUnmount(() => {
  unlisten?.();
  document.removeEventListener('mouseup', onDragRelease);
  resizeUnlisten?.();
  window.clearTimeout(sizeTimer);
});
</script>

<template>
  <div class="lyric-root" data-tauri-drag-region @contextmenu="toggleAnchors">
    <div
      class="lyric-bar"
      :class="[{ 'is-idle': !hasTrack }, `density-${prefs.density}`]"
      :style="{ '--lyric-font-size': prefs.fontSize + 'px', '--lyric-opacity': prefs.opacity / 100 }"
    >
      <div v-if="!showPrefs" class="lyric-lines">
        <span class="lyric-current" data-test="overlay-lyric-current">
          <span class="lyric-base">{{ displayText }}</span>
          <span
            v-if="activeLine"
            class="lyric-fill"
            :style="{ width: fillPct + '%' }"
            aria-hidden="true"
          >{{ displayText }}</span>
        </span>
        <span v-if="nextLine" class="lyric-next">{{ nextLine.text }}</span>
      </div>

      <div v-else class="lyric-prefs" data-test="lyric-prefs" @click.stop>
        <div class="lyric-prefs-row">
          <span>字号</span>
          <button
            v-for="s in FONT_STEPS"
            :key="s"
            type="button"
            :class="{ active: prefs.fontSize === s }"
            @click="prefs.fontSize = s"
          >{{ s }}</button>
        </div>
        <div class="lyric-prefs-row">
          <span>密度</span>
          <button type="button" :class="{ active: prefs.density === 'compact' }" @click="prefs.density = 'compact'">紧凑</button>
          <button type="button" :class="{ active: prefs.density === 'standard' }" @click="prefs.density = 'standard'">标准</button>
        </div>
        <div class="lyric-prefs-row">
          <span>不透明度</span>
          <input v-model.number="prefs.opacity" type="range" min="50" max="100" step="5" aria-label="不透明度" />
          <b>{{ prefs.opacity }}%</b>
        </div>
      </div>

      <div class="lyric-controls">
        <button type="button" aria-label="上一首" title="上一首" :disabled="!hasTrack" @click="sendPlayerCommand({ action: 'prev' })">
          <PhSkipBack :size="14" weight="fill" aria-hidden="true" />
        </button>
        <button
          type="button"
          class="lyric-play"
          :aria-label="state?.isPlaying ? '暂停' : '播放'"
          :title="state?.isPlaying ? '暂停' : '播放'"
          :disabled="!hasTrack"
          @click="sendPlayerCommand({ action: 'toggle' })"
        >
          <PhPause v-if="state?.isPlaying" :size="14" weight="fill" aria-hidden="true" />
          <PhPlay v-else :size="14" weight="fill" aria-hidden="true" />
        </button>
        <button type="button" aria-label="下一首" title="下一首" :disabled="!hasTrack" @click="sendPlayerCommand({ action: 'next' })">
          <PhSkipForward :size="14" weight="fill" aria-hidden="true" />
        </button>
      </div>

      <button type="button" class="lyric-close" aria-label="关闭桌面歌词" title="关闭" @click="closeBar">
        <PhX :size="12" weight="bold" aria-hidden="true" />
      </button>

      <button
        type="button"
        class="lyric-gear"
        aria-label="歌词设置"
        title="歌词设置"
        @click.stop="showPrefs = !showPrefs"
      >
        <PhGear :size="13" weight="bold" aria-hidden="true" />
      </button>
    </div>

    <div v-if="showAnchors" class="lyric-anchors" role="menu" aria-label="窗口位置">
      <button
        v-for="a in anchors"
        :key="a"
        type="button"
        class="lyric-anchor-dot"
        :title="a"
        @click="pickAnchor(a)"
      />
    </div>
  </div>
</template>

<style scoped>
:global(html),
:global(body),
:global(#app) {
  background: transparent !important;
  overflow: hidden;
}

.lyric-root {
  width: 100vw;
  height: 100vh;
  display: grid;
  place-items: center;
  position: relative;
  background: transparent;
}

.lyric-bar {
  position: relative;
  display: flex;
  align-items: center;
  gap: 10px;
  width: calc(100% - 12px);
  height: calc(100% - 12px);
  padding: 6px 12px;
  box-sizing: border-box;
  border-radius: 12px;
  border: 1px solid color-mix(in srgb, #fff 10%, transparent);
  background: transparent;
  box-shadow: 0 12px 32px rgba(0, 0, 0, 0.45);
}

/* Solid dark backing — opacity lives here so text stays fully opaque */
.lyric-bar::before {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: inherit;
  background: color-mix(in srgb, var(--surface-elevated, #1a2222) 96%, #000 4%);
  opacity: var(--lyric-opacity, 1);
  z-index: -1;
}

.lyric-bar.is-idle {
  opacity: 0.72;
}

.lyric-lines {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 2px;
  overflow: hidden;
}

.lyric-current {
  position: relative;
  display: block;
  font-size: var(--lyric-font-size, 20px);
  font-weight: 700;
  line-height: 1.25;
  white-space: nowrap;
  overflow: hidden;
}

.lyric-bar.density-compact .lyric-lines {
  gap: 0;
}

.lyric-bar.density-compact {
  padding-top: 4px;
  padding-bottom: 4px;
}

.lyric-gear {
  position: absolute;
  top: -4px;
  right: 16px;
  width: 18px;
  height: 18px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: var(--surface-2, #141a1b);
  color: var(--text-muted, #626d69);
  cursor: pointer;
  opacity: 0;
  transition: opacity 0.15s ease;
  z-index: 2;
}

.lyric-bar:hover .lyric-gear {
  opacity: 1;
}

.lyric-gear:hover {
  color: var(--text-primary, #f2f5f2);
}

.lyric-prefs {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 3px;
}

.lyric-prefs-row {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: var(--text-secondary, #929c98);
}

.lyric-prefs-row > span {
  width: 46px;
  flex: none;
}

.lyric-prefs-row button {
  padding: 1px 6px;
  border: 1px solid color-mix(in srgb, var(--text-primary, #f2f5f2) 16%, transparent);
  border-radius: 6px;
  background: transparent;
  color: var(--text-secondary, #929c98);
  font-size: 11px;
  cursor: pointer;
}

.lyric-prefs-row button.active {
  color: var(--accent, #62d6a2);
  border-color: color-mix(in srgb, var(--accent, #62d6a2) 55%, transparent);
}

.lyric-prefs-row input[type='range'] {
  flex: 1;
  accent-color: var(--accent, #62d6a2);
}

.lyric-prefs-row b {
  font-size: 11px;
  color: var(--text-secondary, #929c98);
  font-variant-numeric: tabular-nums;
  width: 34px;
  text-align: right;
}

.lyric-base {
  color: color-mix(in srgb, var(--text-primary, #f2f5f2) 38%, transparent);
}

.lyric-fill {
  position: absolute;
  left: 0;
  top: 0;
  color: var(--accent, #62d6a2);
  overflow: hidden;
  white-space: nowrap;
  transition: width 0.45s linear;
}

.lyric-next {
  font-size: 12px;
  color: var(--text-muted, #626d69);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.lyric-controls {
  display: flex;
  align-items: center;
  gap: 2px;
  flex: none;
  opacity: 0;
  transition: opacity 0.15s ease;
}

.lyric-bar:hover .lyric-controls {
  opacity: 1;
}

.lyric-controls button {
  width: 24px;
  height: 24px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: transparent;
  color: color-mix(in srgb, var(--text-primary, #f2f5f2) 75%, transparent);
  cursor: pointer;
}

.lyric-controls button:hover:not(:disabled) {
  color: var(--text-primary, #f2f5f2);
  background: color-mix(in srgb, var(--text-primary, #f2f5f2) 8%, transparent);
}

.lyric-controls button:disabled {
  opacity: 0.45;
  cursor: default;
}

.lyric-controls .lyric-play {
  background: var(--accent, #62d6a2);
  color: #0a1410;
}

.lyric-close {
  position: absolute;
  top: -4px;
  right: -4px;
  width: 18px;
  height: 18px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: var(--surface-2, #141a1b);
  color: var(--text-muted, #626d69);
  cursor: pointer;
  opacity: 0;
  transition: opacity 0.15s ease;
}

.lyric-bar:hover .lyric-close {
  opacity: 1;
}

.lyric-anchors {
  position: absolute;
  right: 10px;
  top: 6px;
  display: grid;
  grid-template-columns: repeat(3, 12px);
  gap: 5px;
  padding: 8px;
  border-radius: 10px;
  border: 1px solid color-mix(in srgb, #fff 10%, transparent);
  background: color-mix(in srgb, var(--surface-elevated, #1a2222) 92%, #000 8%);
  box-shadow: 0 8px 22px rgba(0, 0, 0, 0.4);
}

.lyric-anchor-dot {
  width: 12px;
  height: 12px;
  border: 1px solid color-mix(in srgb, var(--text-primary, #f2f5f2) 30%, transparent);
  border-radius: 3px;
  background: transparent;
  cursor: pointer;
  padding: 0;
}

.lyric-anchor-dot:hover {
  background: var(--accent, #62d6a2);
  border-color: var(--accent, #62d6a2);
}
</style>
