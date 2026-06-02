<script setup lang="ts">
import { ref, watch } from 'vue';
import { getUserPlaylists, addTrackToPlaylist, UserPlaylist } from '../api/favorite';
import { Track } from '../api/normalizer';
import { userStore } from '../api/userStore';

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
  const result = await addTrackToPlaylist(playlist, props.track);
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
</script>

<template>
  <Teleport to="body">
    <div v-if="show" class="modal-overlay" @click.self="handleClose">
      <div class="modal-content">
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

.modal-content {
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
