<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue';
import { playerStore, playTrack } from '../playerStore';
import { fetchCoverImage } from '../../playback/data/coverGateway';

defineProps<{
  show: boolean;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
}>();

/** Queue filter (frontend polish): name / artist substring, case-insensitive. */
const queueFilter = ref('');

const filteredQueue = computed(() => {
  const q = queueFilter.value.trim().toLowerCase();
  if (!q) return playerStore.queue;
  return playerStore.queue.filter(
    (item) =>
      (item.SongName || '').toLowerCase().includes(q)
      || (item.SingerName || '').toLowerCase().includes(q),
  );
});

/**
 * In-flight cover fetches: FileHash → generation that owns the pending slot.
 * Do not mutate Image to mark pending. Stale completions must only clear their
 * own generation so they never drop a newer request's pending mark.
 */
const pendingCoverFetches = new Map<string, number>();
/** Bumped when queue FileHash identity changes so stale responses no-op. */
let coverFetchGeneration = 0;
let lastQueueIdentity = '';

function queueCoverIdentity(): string {
  return playerStore.queue.map((t) => t.FileHash || '').join('\0');
}

function clearPendingIfOwner(hash: string, gen: number) {
  if (pendingCoverFetches.get(hash) === gen) {
    pendingCoverFetches.delete(hash);
  }
}

function fetchMissingCovers() {
  const identity = queueCoverIdentity();
  if (identity !== lastQueueIdentity) {
    lastQueueIdentity = identity;
    coverFetchGeneration += 1;
    // Drop pending slots; in-flight work is owned by prior gens and must not
    // block re-queue of the same FileHash under the new generation.
    pendingCoverFetches.clear();
  }
  const gen = coverFetchGeneration;

  for (const item of playerStore.queue) {
    const hash = item.FileHash;
    if (!hash || item.Image || pendingCoverFetches.has(hash)) continue;

    pendingCoverFetches.set(hash, gen);
    fetchCoverImage(hash)
      .then((img) => {
        clearPendingIfOwner(hash, gen);
        if (gen !== coverFetchGeneration) return;
        if (!img) return;
        const track = playerStore.queue.find((t) => t.FileHash === hash);
        if (!track || track.Image) return;
        track.Image = img;
        localStorage.setItem('player_queue', JSON.stringify(playerStore.queue));
      })
      .catch(() => {
        clearPendingIfOwner(hash, gen);
      });
  }
}

onMounted(() => {
  fetchMissingCovers();
});

watch(() => playerStore.queue, () => {
  fetchMissingCovers();
}, { deep: true });
</script>

<template>
  <transition name="slide-up">
    <aside class="queue-panel" v-if="show">
      <div class="panel-head">
        <div class="title">当前播放队列 <span class="en">QUEUE</span></div>
        <button class="panel-close" aria-label="close" @click="emit('close')">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
            <path d="M6 6l12 12M6 18L18 6"/>
          </svg>
        </button>
      </div>

      <div class="queue-filter-row">
        <input
          v-model="queueFilter"
          type="search"
          class="queue-filter"
          placeholder="筛选歌曲 / 歌手"
          aria-label="筛选播放队列"
        />
      </div>

      <div class="panel-scroll">
        <div class="recent">
          <div
            v-for="item in filteredQueue"
            :key="item.FileHash"
            class="item"
            :class="{ active: playerStore.currentTrack?.FileHash === item.FileHash }"
            @click="playTrack(item)"
          >
            <div class="mini">
              <img v-if="item.Image" :src="item.Image" alt="cover" style="width:100%;height:100%;object-fit:cover;" />
              <svg v-else viewBox="0 0 36 36">
                <rect width="36" height="36" fill="var(--ink-mute)"/>
                <text x="18" y="22" text-anchor="middle" font-family="var(--font-serif)" font-style="italic" font-size="12" fill="var(--paper)">
                  {{ item.SongName.slice(0, 2) }}
                </text>
              </svg>
            </div>
            <div class="info">
              <b>{{ item.SongName }}</b>
              <span>{{ item.SingerName }}</span>
            </div>
          </div>
          <div v-if="playerStore.queue.length === 0" class="empty-state">
            队列为空
          </div>
          <div v-else-if="filteredQueue.length === 0" class="empty-state">
            无匹配结果
          </div>
        </div>
      </div>
    </aside>
  </transition>
</template>

<style scoped>
.queue-panel {
  position: absolute;
  bottom: 16px; /* Gap from the bottom PlayerBar */
  right: 16px; /* Gap from the right edge */
  width: 320px;
  height: 600px; /* Make it longer */
  max-height: 75vh;
  background: var(--paper-2);
  border: 1px solid var(--rule); /* Full border */
  border-radius: 12px; /* All corners rounded since it's floating */
  box-shadow: 0 12px 32px -8px rgba(40,28,12,0.25),
              0 4px 12px -4px rgba(40,28,12,0.15);
  display: flex;
  flex-direction: column;
  z-index: 100;
  overflow: hidden;
  pointer-events: auto; /* IMPORTANT: So click works on it, but wrapper doesn't block everything else if wrapper existed */
}

.panel-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 16px;
  border-bottom: 1px dashed var(--rule);
  background: var(--paper);
}

.panel-head .title {
  font-weight: 500;
  font-size: 13px;
  letter-spacing: 0.04em;
}

.panel-head .en {
  font-family: var(--font-serif);
  font-style: italic;
  color: var(--ink-mute);
  font-size: 12px;
  margin-left: 4px;
}

.queue-filter-row {
  padding: 8px 12px 4px;
  border-bottom: 1px dashed var(--rule);
  background: var(--paper);
}

.queue-filter {
  width: 100%;
  box-sizing: border-box;
  padding: 8px 10px;
  border: 1px solid var(--rule);
  border-radius: 8px;
  background: var(--paper-2);
  color: var(--ink);
  font-size: 12px;
}

.queue-filter:focus {
  outline: 2px solid color-mix(in srgb, var(--accent, #c9a227) 50%, transparent);
  outline-offset: 1px;
}

.panel-close {
  width: 24px;
  height: 24px;
  display: grid;
  place-items: center;
  border-radius: 4px;
  color: var(--ink-soft);
}
.panel-close:hover {
  background: var(--rule-soft);
  color: var(--ink);
}
.panel-close svg {
  width: 14px;
  height: 14px;
}

.panel-scroll {
  flex: 1;
  overflow-y: auto;
  padding: 8px 0;
  scrollbar-width: thin;
  scrollbar-color: rgba(34,27,18,0.2) transparent;
}

.recent .item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 16px;
  cursor: pointer;
  transition: background 0.15s;
}
.recent .item:hover {
  background: rgba(255,255,255,0.3);
}
.recent .item.active {
  background: rgba(255,255,255,0.5);
  border-left: 2px solid var(--accent);
  padding-left: 14px;
}
.recent .item.active b {
  color: var(--accent);
}

.recent .mini {
  width: 36px;
  height: 36px;
  flex-shrink: 0;
  border-radius: 4px;
  overflow: hidden;
  box-shadow: inset 0 0 0 1px var(--rule-soft);
  background: var(--paper-edge);
}

.recent .info {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.recent .info b {
  font-weight: 500;
  font-size: 13px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.recent .info span {
  font-size: 11px;
  color: var(--ink-mute);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.recent .dur {
  font-variant-numeric: tabular-nums;
  font-size: 12px;
  color: var(--ink-faint);
}

.empty-state {
  padding: 32px 16px;
  text-align: center;
  color: var(--ink-mute);
  font-style: italic;
}

.slide-up-enter-active,
.slide-up-leave-active {
  transition: all 0.25s var(--ease-spa);
}
.slide-up-enter-from,
.slide-up-leave-to {
  opacity: 0;
  transform: translateY(20px) scale(0.98);
}

/* Dark mode overrides */
:global(:root[data-mode="dark"]) .queue-panel {
  box-shadow: 0 12px 32px -8px rgba(0,0,0,0.45),
              0 4px 12px -4px rgba(0,0,0,0.3);
}
:global(:root[data-mode="dark"]) .panel-scroll {
  scrollbar-color: rgba(255,255,255,0.15) transparent;
}
:global(:root[data-mode="dark"]) .recent .item:hover {
  background: rgba(255,255,255,0.06);
}
:global(:root[data-mode="dark"]) .recent .item.active {
  background: rgba(255,255,255,0.1);
}
</style>
