<script setup lang="ts">
import { ref, watch } from 'vue';
import { gsap } from 'gsap';
import { addTrackToPlaylist, type UserPlaylist } from '../api/favorite';
import { getUserPlaylists } from '../api/favoriteStore';
import { Track } from '../api/normalizer';
import { userStore } from '../api/userStore';
import { transitionEnter, transitionLeave } from '../api/motion';

const props = defineProps<{
  show: boolean;
  track: Track | null;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'success', playlistName: string): void;
  (e: 'error', message: string): void;
}>();

const playlists = ref<UserPlaylist[]>([]);
const loading = ref(false);
const adding = ref<string | null>(null);

watch(() => props.show, async (newVal) => {
  if (newVal && userStore.isLoggedIn) {
    loading.value = true;
    playlists.value = await getUserPlaylists();
    loading.value = false;
  }
});

async function handleSelect(playlist: UserPlaylist) {
  if (!props.track || adding.value) return;

  adding.value = playlist.id;
  let result: { success: boolean; error?: string };
  try {
    result = await addTrackToPlaylist(playlist, props.track);
  } catch (e: any) {
    // addTrackToPlaylist throws on transport errors (offline / circuit open);
    // surface them as a regular error toast.
    result = { success: false, error: e?.message || '收藏失败' };
  }
  adding.value = null;

  if (result.success) {
    emit('success', playlist.name);
    emit('close');
  } else {
    emit('error', result.error || '收藏失败');
  }
}

function handleClose() {
  emit('close');
}

function onEnter(el: Element, done: () => void) {
  transitionEnter(el, done);
  const modal = (el as HTMLElement).querySelector('.playlist-modal');
  if (modal) {
    gsap.fromTo(modal, { scale: 0.96, y: 8 }, { scale: 1, y: 0, duration: 0.25, ease: 'power2.out', onComplete: done });
  }
}

function onLeave(el: Element, done: () => void) {
  transitionLeave(el, done);
}
</script>

<template>
  <Teleport to="body">
    <Transition :css="false" appear @enter="onEnter" @leave="onLeave">
      <div v-if="show" class="modal-overlay" @click.self="handleClose">
        <div class="playlist-modal">
          <div class="modal-header">
            <h3>收藏到歌单</h3>
            <button class="close-btn" @click="handleClose">×</button>
          </div>

          <div class="modal-body">
            <div v-if="!userStore.isLoggedIn" class="empty-hint">
              请先登录后收藏歌曲
            </div>
            <div v-else-if="loading" class="empty-hint">
              加载歌单中…
            </div>
            <div v-else-if="playlists.length === 0" class="empty-hint">
              暂无歌单，请先创建歌单
            </div>
            <div v-else class="playlist-list">
              <div
                v-for="pl in playlists"
                :key="pl.id"
                class="playlist-item"
                :class="{ disabled: adding !== null }"
                @click="handleSelect(pl)"
              >
                <div class="pl-icon">♫</div>
                <div class="pl-info">
                  <div class="pl-name">{{ pl.name }}</div>
                  <div class="pl-count">{{ pl.songcount || 0 }} 首</div>
                </div>
                <div v-if="adding === pl.id" class="pl-adding">添加中…</div>
              </div>
            </div>
          </div>
      </div>
    </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.playlist-modal {
  background: var(--paper, #f1ead8);
  border-radius: 12px;
  width: 360px;
  max-height: 480px;
  display: flex;
  flex-direction: column;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.2);
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  border-bottom: 1px solid var(--ink-light, #c4b99a);
}

.modal-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
}

.close-btn {
  background: none;
  border: none;
  font-size: 24px;
  cursor: pointer;
  color: var(--ink-mute, #8a7e6a);
  padding: 0;
  line-height: 1;
}

.close-btn:hover {
  color: var(--ink, #221b12);
}

.modal-body {
  overflow-y: auto;
  padding: 12px 0;
}

.empty-hint {
  padding: 32px 20px;
  text-align: center;
  color: var(--ink-mute, #8a7e6a);
  font-style: italic;
}

.playlist-list {
  display: flex;
  flex-direction: column;
}

.playlist-item {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 20px;
  cursor: pointer;
  transition: background 0.15s;
}

.playlist-item:hover {
  background: rgba(0, 0, 0, 0.05);
}

.playlist-item.disabled {
  opacity: 0.6;
  pointer-events: none;
}

.pl-icon {
  width: 40px;
  height: 40px;
  background: var(--accent, #a8311b);
  color: var(--paper, #f1ead8);
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
}

.pl-info {
  flex: 1;
  min-width: 0;
}

.pl-name {
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.pl-count {
  font-size: 12px;
  color: var(--ink-mute, #8a7e6a);
  margin-top: 2px;
}

.pl-adding {
  font-size: 12px;
  color: var(--accent, #a8311b);
  font-style: italic;
}
</style>
