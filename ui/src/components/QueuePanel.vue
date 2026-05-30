<script setup lang="ts">
import { watch, onMounted } from 'vue';
import { playerStore, playTrack } from '../api/playerStore';
import { fetchCoverImage } from '../api/normalizer';

defineProps<{
  show: boolean;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
}>();

async function fetchMissingCovers() {
  for (const item of playerStore.queue) {
    if (!item.Image) {
      item.Image = ''; 
      fetchCoverImage(item.FileHash).then(img => {
        if (img) {
          item.Image = img;
          localStorage.setItem('player_queue', JSON.stringify(playerStore.queue));
        }
      });
    }
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
      
      <div class="panel-scroll">
        <div class="recent">
          <div 
            v-for="item in playerStore.queue" 
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
  transition: all 0.25s cubic-bezier(0.16, 1, 0.3, 1);
}
.slide-up-enter-from,
.slide-up-leave-to {
  opacity: 0;
  transform: translateY(20px) scale(0.98);
}

/* Dark mode overrides */
html.dark .queue-panel {
  box-shadow: 0 12px 32px -8px rgba(0,0,0,0.45),
              0 4px 12px -4px rgba(0,0,0,0.3);
}
html.dark .panel-scroll {
  scrollbar-color: rgba(255,255,255,0.15) transparent;
}
html.dark .recent .item:hover {
  background: rgba(255,255,255,0.06);
}
html.dark .recent .item.active {
  background: rgba(255,255,255,0.1);
}
</style>
