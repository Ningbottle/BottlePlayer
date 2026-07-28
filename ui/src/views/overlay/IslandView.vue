<script setup lang="ts">
/**
 * IslandView — 播放灵动岛（/overlay/island）
 *
 * 透明置顶胶囊：旋转封面盘（带环形进度）+ 曲目信息 + 三键传输。
 * 状态经 playerSync 与主窗口同步；拖拽自由摆放，松手磁吸边缘，
 * 右键九宫格快速锚点，位置记忆。
 */
import { computed, onBeforeUnmount, onMounted, ref, nextTick } from 'vue';
import { getCurrentWindow } from '@tauri-apps/api/window';
import { LogicalSize, PhysicalPosition } from '@tauri-apps/api/dpi';
import { PhPause, PhPlay, PhSkipBack, PhSkipForward, PhX } from '@phosphor-icons/vue';
import { onPlayerState, sendPlayerCommand, applySyncedTheme, type PlayerSyncState } from '../../api/playerSync';
import { isTauriRuntime, moveCurrentOverlayTo, settleCurrentOverlay } from '../../api/overlayWindows';
import { startVinylSpin } from '../../api/motion';
import type { VinylSpinHandle } from '../../api/motion';
import PlayerProgress from '../../components/player/PlayerProgress.vue';

const COLLAPSED_SIZE = { width: 300, height: 64 };
const EXPANDED_SIZE = { width: 480, height: 200 };
const AUTO_COLLAPSE_MS = 5_000;

const state = ref<PlayerSyncState | null>(null);
const hasTrack = computed(() => !!state.value?.hash);
const progress = computed(() => {
  const s = state.value;
  if (!s || s.duration <= 0) return 0;
  return Math.max(0, Math.min(1, s.currentTime / s.duration));
});

/** Progress ring geometry (SVG circle, r=20). */
const RING_R = 20;
const RING_LEN = 2 * Math.PI * RING_R;
const ringOffset = computed(() => RING_LEN * (1 - progress.value));

const discEl = ref<HTMLElement | null>(null);
let vinylSpin: VinylSpinHandle | null = null;
let unlisten: (() => void) | null = null;

/** Expanded wide-card state (not persisted — always opens collapsed). */
const expanded = ref(false);
const cardDiscEl = ref<HTMLElement | null>(null);
let cardSpin: VinylSpinHandle | null = null;
let desiredExpanded = false;
let windowExpanded = false;
let resizeTask: Promise<void> | null = null;
let autoCollapseTimer: ReturnType<typeof setTimeout> | null = null;

async function applyWindowSize(size: { width: number; height: number }): Promise<void> {
  if (!isTauriRuntime()) return;
  const win = getCurrentWindow();
  const [pos, old, scaleFactor] = await Promise.all([
    win.outerPosition(),
    win.outerSize(),
    win.scaleFactor(),
  ]);
  // Expand downward from the pill: top edge unchanged, card centered on the pill.
  // Position APIs use physical pixels while setSize uses logical pixels.
  const cx = pos.x + old.width / 2;
  const targetPhysicalWidth = size.width * scaleFactor;
  await win.setSize(new LogicalSize(size.width, size.height));
  await win.setPosition(new PhysicalPosition(
    Math.round(cx - targetPhysicalWidth / 2),
    pos.y,
  ));
}

function clearAutoCollapse(): void {
  if (autoCollapseTimer !== null) {
    clearTimeout(autoCollapseTimer);
    autoCollapseTimer = null;
  }
}

function stopCardSpin(): void {
  cardSpin?.kill();
  cardSpin = null;
}

function armAutoCollapse(): void {
  clearAutoCollapse();
  if (!desiredExpanded) return;
  autoCollapseTimer = setTimeout(() => {
    autoCollapseTimer = null;
    requestExpanded(false);
  }, AUTO_COLLAPSE_MS);
}

function runResizeQueue(): Promise<void> {
  if (resizeTask) return resizeTask;

  resizeTask = (async () => {
    try {
      while (windowExpanded !== desiredExpanded) {
        const target = desiredExpanded;
        if (target) {
          await applyWindowSize(EXPANDED_SIZE);
          windowExpanded = true;
          if (desiredExpanded) {
            expanded.value = true;
            await nextTick();
            if (cardDiscEl.value && !cardSpin) {
              cardSpin = startVinylSpin(cardDiscEl.value, () => !!state.value?.isPlaying);
            }
            armAutoCollapse();
          }
        } else {
          clearAutoCollapse();
          expanded.value = false;
          stopCardSpin();
          await nextTick();
          await applyWindowSize(COLLAPSED_SIZE);
          windowExpanded = false;
        }
      }
    } catch (error) {
      desiredExpanded = windowExpanded;
      expanded.value = windowExpanded;
      console.error('[island] resize transition failed:', error);
    } finally {
      resizeTask = null;
    }
  })();

  return resizeTask;
}

function requestExpanded(next: boolean): void {
  desiredExpanded = next;
  if (!next) clearAutoCollapse();
  void runResizeQueue();
}

function toggleExpanded(): void {
  requestExpanded(!desiredExpanded);
}

/** Click-to-toggle must not fire after a drag — require <5px pointer travel. */
let downX = 0;
let downY = 0;

function onAreaPointerDown(e: PointerEvent): void {
  if (desiredExpanded) armAutoCollapse();
  downX = e.clientX;
  downY = e.clientY;
}

function onAreaPointerUp(e: PointerEvent): void {
  if ((e.target as HTMLElement).closest('button, [role="slider"], input, select, textarea, a[href]')) return;
  if (Math.hypot(e.clientX - downX, e.clientY - downY) < 5) {
    void toggleExpanded();
  }
}

function onKeydown(e: KeyboardEvent): void {
  if (e.key === 'Escape' && desiredExpanded) {
    requestExpanded(false);
  }
}

function onWindowBlur(): void {
  if (desiredExpanded) requestExpanded(false);
}

const anchors = ['top-left', 'top-center', 'top-right', 'center-left', 'center', 'center-right', 'bottom-left', 'bottom-center', 'bottom-right'];
const showAnchors = ref(false);

function toggleAnchors(event: MouseEvent): void {
  event.preventDefault();
  showAnchors.value = !showAnchors.value;
}

async function pickAnchor(anchor: string): Promise<void> {
  showAnchors.value = false;
  await moveCurrentOverlayTo(anchor, 'island');
}

function onDragRelease(): void {
  void settleCurrentOverlay('island');
}

async function closeIsland(): Promise<void> {
  if (isTauriRuntime()) {
    await getCurrentWindow().close();
  }
}

onMounted(async () => {
  unlisten = await onPlayerState((s) => {
    applySyncedTheme(s);
    state.value = s;
    vinylSpin?.setPlaying();
    cardSpin?.setPlaying();
  });
  if (discEl.value) {
    vinylSpin = startVinylSpin(discEl.value, () => !!state.value?.isPlaying);
  }
  document.addEventListener('mouseup', onDragRelease);
  document.addEventListener('keydown', onKeydown);
  window.addEventListener('blur', onWindowBlur);
});

onBeforeUnmount(() => {
  clearAutoCollapse();
  unlisten?.();
  vinylSpin?.kill();
  vinylSpin = null;
  stopCardSpin();
  document.removeEventListener('mouseup', onDragRelease);
  document.removeEventListener('keydown', onKeydown);
  window.removeEventListener('blur', onWindowBlur);
});
</script>

<template>
  <div class="island-root" @contextmenu="toggleAnchors">
    <div
      v-if="!expanded"
      class="island-capsule"
      :class="{ 'is-idle': !hasTrack }"
      data-tauri-drag-region
      @pointerdown="onAreaPointerDown"
      @pointerup="onAreaPointerUp"
    >
      <!-- Cover disc with progress ring -->
      <div class="island-disc-wrap">
        <svg class="island-ring" viewBox="0 0 48 48" aria-hidden="true">
          <circle class="island-ring-track" cx="36" cy="36" :r="RING_R" />
          <circle
            class="island-ring-fill"
            cx="36" cy="36" :r="RING_R"
            :stroke-dasharray="RING_LEN"
            :stroke-dashoffset="ringOffset"
          />
        </svg>
        <div ref="discEl" class="island-disc">
          <img v-if="state?.cover" :src="state.cover" alt="封面" />
          <div v-else class="island-disc-empty" aria-hidden="true" />
          <div class="island-disc-grooves" aria-hidden="true" />
        </div>
        <div class="island-disc-spindle" aria-hidden="true" />
      </div>

      <!-- Meta -->
      <div class="island-meta">
        <span class="island-name">{{ hasTrack ? state?.name : '未播放' }}</span>
        <span class="island-artist">{{ hasTrack ? state?.artist : '—' }}</span>
      </div>

      <!-- Transport -->
      <div class="island-transport">
        <button type="button" aria-label="上一首" title="上一首" :disabled="!hasTrack" @click="sendPlayerCommand({ action: 'prev' })">
          <PhSkipBack :size="15" weight="fill" aria-hidden="true" />
        </button>
        <button
          type="button"
          class="island-play"
          :aria-label="state?.isPlaying ? '暂停' : '播放'"
          :title="state?.isPlaying ? '暂停' : '播放'"
          :disabled="!hasTrack"
          @click="sendPlayerCommand({ action: 'toggle' })"
        >
          <PhPause v-if="state?.isPlaying" :size="15" weight="fill" aria-hidden="true" />
          <PhPlay v-else :size="15" weight="fill" aria-hidden="true" />
        </button>
        <button type="button" aria-label="下一首" title="下一首" :disabled="!hasTrack" @click="sendPlayerCommand({ action: 'next' })">
          <PhSkipForward :size="15" weight="fill" aria-hidden="true" />
        </button>
      </div>

      <button type="button" class="island-close" aria-label="关闭灵动岛" title="关闭" @click="closeIsland">
        <PhX :size="12" weight="bold" aria-hidden="true" />
      </button>
    </div>

    <!-- Expanded wide card: vinyl cover + meta + seekable progress + transport -->
    <div
      v-else
      class="island-card"
      data-test="island-card"
      data-tauri-drag-region
      @pointerdown="onAreaPointerDown"
      @pointerup="onAreaPointerUp"
    >
      <div class="island-card-cover">
        <div ref="cardDiscEl" class="island-disc">
          <img v-if="state?.cover" :src="state.cover" alt="封面" />
          <div v-else class="island-disc-empty" aria-hidden="true" />
          <div class="island-disc-grooves" aria-hidden="true" />
        </div>
        <div class="island-disc-spindle" aria-hidden="true" />
      </div>
      <div class="island-card-main">
        <div class="island-card-meta">
          <span class="island-name">{{ hasTrack ? state?.name : '未播放' }}</span>
          <span class="island-artist">{{ hasTrack ? state?.artist : '—' }}</span>
        </div>
        <PlayerProgress
          :current-time="state?.currentTime ?? 0"
          :duration="state?.duration ?? 0"
          @seek="(s: number) => sendPlayerCommand({ action: 'seek', value: s })"
        />
        <div class="island-card-controls">
          <button type="button" aria-label="上一首" title="上一首" :disabled="!hasTrack" @click="sendPlayerCommand({ action: 'prev' })">
            <PhSkipBack :size="15" weight="fill" aria-hidden="true" />
          </button>
          <button
            type="button"
            class="island-play"
            :aria-label="state?.isPlaying ? '暂停' : '播放'"
            :title="state?.isPlaying ? '暂停' : '播放'"
            :disabled="!hasTrack"
            @click="sendPlayerCommand({ action: 'toggle' })"
          >
            <PhPause v-if="state?.isPlaying" :size="15" weight="fill" aria-hidden="true" />
            <PhPlay v-else :size="15" weight="fill" aria-hidden="true" />
          </button>
          <button type="button" aria-label="下一首" title="下一首" :disabled="!hasTrack" @click="sendPlayerCommand({ action: 'next' })">
            <PhSkipForward :size="15" weight="fill" aria-hidden="true" />
          </button>
        </div>
      </div>
    </div>

    <!-- Nine-grid anchor picker (right-click to open) -->
    <div v-if="showAnchors" class="island-anchors" role="menu" aria-label="窗口位置">
      <button
        v-for="a in anchors"
        :key="a"
        type="button"
        class="island-anchor-dot"
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

.island-root {
  width: 100vw;
  height: 100vh;
  display: grid;
  place-items: center;
  position: relative;
  background: transparent;
}

.island-capsule {
  position: relative;
  display: flex;
  align-items: center;
  gap: 10px;
  width: 288px;
  height: 52px;
  padding: 6px 10px;
  box-sizing: border-box;
  border-radius: 999px;
  border: 1px solid color-mix(in srgb, #fff 10%, transparent);
  background: color-mix(in srgb, var(--surface-elevated, #1a2222) 88%, #000 12%);
  box-shadow: 0 12px 32px rgba(0, 0, 0, 0.45);
}

.island-capsule.is-idle {
  opacity: 0.72;
}

.island-disc-wrap {
  position: relative;
  width: 40px;
  height: 40px;
  flex: none;
}

.island-ring {
  position: absolute;
  inset: 0;
  transform: rotate(-90deg);
}

.island-ring-track {
  fill: none;
  stroke: color-mix(in srgb, var(--text-primary, #f2f5f2) 14%, transparent);
  stroke-width: 2.5;
}

.island-ring-fill {
  fill: none;
  stroke: var(--accent, #62d6a2);
  stroke-width: 2.5;
  stroke-linecap: round;
  transition: stroke-dashoffset 0.4s linear;
}

.island-disc {
  position: absolute;
  inset: 3px;
  border-radius: 50%;
  overflow: hidden;
  background: #0a0a09;
  will-change: transform;
}

.island-disc img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  border-radius: 50%;
}

.island-disc-empty {
  width: 100%;
  height: 100%;
}

.island-disc-grooves {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  background: repeating-radial-gradient(circle at 50% 50%,
    rgba(255, 255, 255, 0.06) 0 1px,
    transparent 1px 3px);
  pointer-events: none;
}

.island-disc-spindle {
  position: absolute;
  left: 50%;
  top: 50%;
  width: 12px;
  height: 12px;
  transform: translate(-50%, -50%);
  border-radius: 50%;
  background: radial-gradient(circle at 50% 50%,
    #0a0a09 0 20%,
    color-mix(in srgb, var(--accent, #62d6a2) 82%, #000 18%) 21% 100%);
  pointer-events: none;
  z-index: 1;
}

.island-meta {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 1px;
}

.island-name {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-primary, #f2f5f2);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.island-artist {
  font-size: 11px;
  color: var(--text-muted, #626d69);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.island-transport {
  display: flex;
  align-items: center;
  gap: 2px;
  flex: none;
}

.island-transport button {
  width: 26px;
  height: 26px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: transparent;
  color: color-mix(in srgb, var(--text-primary, #f2f5f2) 75%, transparent);
  cursor: pointer;
  transition: color 0.15s ease, background 0.15s ease;
}

.island-transport button:hover:not(:disabled) {
  color: var(--text-primary, #f2f5f2);
  background: color-mix(in srgb, var(--text-primary, #f2f5f2) 8%, transparent);
}

.island-transport button:disabled {
  opacity: 0.45;
  cursor: default;
}

.island-transport .island-play {
  width: 30px;
  height: 30px;
  background: var(--accent, #62d6a2);
  color: #0a1410;
}

.island-transport .island-play:hover:not(:disabled) {
  background: var(--accent, #62d6a2);
  color: #0a1410;
  filter: brightness(1.06);
}

.island-close {
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

.island-capsule:hover .island-close {
  opacity: 1;
}

.island-close:hover {
  color: var(--text-primary, #f2f5f2);
}

.island-anchors {
  position: absolute;
  right: 10px;
  bottom: calc(100% - 6px);
  display: grid;
  grid-template-columns: repeat(3, 12px);
  gap: 5px;
  padding: 8px;
  border-radius: 10px;
  border: 1px solid color-mix(in srgb, #fff 10%, transparent);
  background: color-mix(in srgb, var(--surface-elevated, #1a2222) 92%, #000 8%);
  box-shadow: 0 8px 22px rgba(0, 0, 0, 0.4);
}

.island-anchor-dot {
  width: 12px;
  height: 12px;
  border: 1px solid color-mix(in srgb, var(--text-primary, #f2f5f2) 30%, transparent);
  border-radius: 3px;
  background: transparent;
  cursor: pointer;
  padding: 0;
}

.island-anchor-dot:hover {
  background: var(--accent, #62d6a2);
  border-color: var(--accent, #62d6a2);
}

/* ── Expanded wide card ── */
.island-card {
  display: flex;
  align-items: center;
  gap: 14px;
  width: 468px;
  height: 188px;
  padding: 14px;
  box-sizing: border-box;
  border-radius: 18px;
  border: 1px solid color-mix(in srgb, #fff 10%, transparent);
  background: color-mix(in srgb, var(--surface-elevated, #1a2222) 88%, #000 12%);
  box-shadow: 0 12px 32px rgba(0, 0, 0, 0.45);
  animation: island-card-in 0.35s cubic-bezier(0.16, 1, 0.3, 1);
}

@keyframes island-card-in {
  from { opacity: 0; transform: scale(0.96); }
  to { opacity: 1; transform: scale(1); }
}

@media (prefers-reduced-motion: reduce) {
  .island-card { animation: none; }
}

.island-card-cover {
  position: relative;
  width: 140px;
  aspect-ratio: 1;
  flex: none;
}

.island-card-cover .island-disc {
  inset: 0;
}

.island-card-cover .island-disc-spindle {
  width: 36px;
  height: 36px;
}

.island-card-main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 10px;
}

.island-card-meta {
  display: flex;
  flex-direction: column;
  gap: 1px;
  min-width: 0;
}

.island-card-controls {
  display: flex;
  align-items: center;
  gap: 6px;
}

.island-card-controls button {
  width: 32px;
  height: 32px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: transparent;
  color: color-mix(in srgb, var(--text-primary, #f2f5f2) 75%, transparent);
  cursor: pointer;
  transition: color 0.15s ease, background 0.15s ease;
}

.island-card-controls button:hover:not(:disabled) {
  color: var(--text-primary, #f2f5f2);
  background: color-mix(in srgb, var(--text-primary, #f2f5f2) 8%, transparent);
}

.island-card-controls button:disabled {
  opacity: 0.45;
  cursor: default;
}

.island-card-controls .island-play {
  width: 36px;
  height: 36px;
  background: var(--accent, #62d6a2);
  color: #0a1410;
}

.island-card-controls .island-play:hover:not(:disabled) {
  background: var(--accent, #62d6a2);
  color: #0a1410;
  filter: brightness(1.06);
}
</style>
