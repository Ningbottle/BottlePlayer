<script setup lang="ts">
import { ref, watch, onMounted } from 'vue';
import { apiGet, describeBackendError } from '../platform/tauri/nativeClient';
import { playAll, playerStore } from '../playback/playerStore';
import { Track as SongInfo, normalizeTrack } from '../api/normalizer';
import AddToPlaylistModal from '../components/AddToPlaylistModal.vue';
import SkinPageHeader from '../components/primitives/SkinPageHeader.vue';


const props = defineProps<{
  query: string;
}>();

const loading = ref(false);
const songs = ref<SongInfo[]>([]);
const totalCount = ref(0);
const page = ref(1);
const error = ref('');

/** Bumps on every load so slower older responses cannot overwrite newer query/page state. */
let searchGeneration = 0;

async function performSearch() {
  const gen = ++searchGeneration;
  if (!props.query) {
    songs.value = [];
    totalCount.value = 0;
    loading.value = false;
    return;
  }
  loading.value = true;
  error.value = '';
  try {
    const res = await apiGet<{ status: number; error?: string; data?: { lists: SongInfo[], total: number } }>('/search', {
      keywords: props.query,
      page: page.value,
      pagesize: 25
    });

    if (gen !== searchGeneration) return;

    if (res.status === 1 && res.data) {
      songs.value = (res.data.lists || []).map(normalizeTrack);
      totalCount.value = res.data.total || 0;
    } else {
      error.value = res.error || '检索失败，请稍后重试';
    }
  } catch (err: any) {
    if (gen !== searchGeneration) return;
    console.error('Search error', err);
    error.value = describeBackendError(err, '搜索失败，请稍后重试');
  } finally {
    if (gen === searchGeneration) {
      loading.value = false;
    }
  }
}

// Single load entry: when query changes on page>1, only reset page and let the
// page watcher fetch — avoid page=1 + performSearch() double request.
watch(() => props.query, () => {
  if (page.value !== 1) {
    page.value = 1;
  } else {
    performSearch();
  }
});

watch(page, () => {
  performSearch();
});

onMounted(() => {
  performSearch();
});

function handlePlay(song: SongInfo) {
  // 用整页搜索结果作为播放队列，从点击的这首开始。
  const idx = songs.value.findIndex(s => s.FileHash === song.FileHash);
  playAll(songs.value, idx >= 0 ? idx : 0);
}

function formatDuration(sec: number) {
  if (!sec) return '00:00';
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

const isCurrentTrack = (song: SongInfo) => {
  return playerStore.currentTrack?.FileHash === song.FileHash;
};

// 收藏功能
const showAddModal = ref(false);
const trackToAdd = ref<SongInfo | null>(null);
const favoriteMsg = ref('');

function handleFavorite(e: MouseEvent, song: SongInfo) {
  e.stopPropagation(); // 阻止冒泡到行点击播放
  trackToAdd.value = song;
  showAddModal.value = true;
}

let favToastTimer: ReturnType<typeof setTimeout> | null = null;

function handleFavoriteSuccess(playlistName: string) {
  favoriteMsg.value = `已收藏到「${playlistName}」`;
  if (favToastTimer) clearTimeout(favToastTimer);
  favToastTimer = setTimeout(() => { favoriteMsg.value = ''; }, 2000);
}

function handleFavoriteError(msg: string) {
  favoriteMsg.value = msg;
  if (favToastTimer) clearTimeout(favToastTimer);
  favToastTimer = setTimeout(() => { favoriteMsg.value = ''; }, 2000);
}
</script>

<template>
  <div class="list-view">
    <SkinPageHeader
      title="搜索"
      kicker="SEARCH · 检索"
      :subtitle="query || '输入关键词'"
    >
      <template #actions>
        <span class="search-count">找到大约 <b>{{ totalCount }}</b> 条结果</span>
      </template>
    </SkinPageHeader>

    <!-- Spinner -->
    <div v-if="loading" class="spinner">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
        <circle cx="12" cy="12" r="10" stroke="rgba(34,27,18,0.1)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
      </svg>
      排版校对中…
    </div>

    <!-- Error message -->
    <div v-else-if="error" class="spinner" style="color: var(--accent);">
      {{ error }}
    </div>

    <!-- Empty results -->
    <div v-else-if="songs.length === 0" class="spinner">
      未找到相关记录。请尝试其他关键词。
    </div>

    <!-- Song Table List -->
    <div v-else>
      <div class="song-row" style="font-weight: 600; border-bottom: 2px solid var(--ink); cursor: default; background: transparent;">
        <span class="index">#</span>
        <span class="title">歌名</span>
        <span class="artist">歌手</span>
        <span class="album">专辑</span>
        <span class="duration">时长</span>
      </div>

      <div 
        v-for="(song, idx) in songs" 
        :key="song.FileHash"
        class="song-row"
        :class="{ active: isCurrentTrack(song) }"
        @click="handlePlay(song)"
      >
        <span class="index">{{ (page - 1) * 25 + idx + 1 }}</span>
        <span class="title">
          {{ song.SongName }}
          <button 
            class="fav-btn" 
            title="收藏"
            @click="handleFavorite($event, song)"
          >
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
              <path d="M12 2l2.39 6.96H22l-6 4.62L18.18 21 12 16.77 5.82 21 8 13.58 2 8.96h7.61z"/>
            </svg>
          </button>
        </span>
        <span class="artist">{{ song.SingerName }}</span>
        <span class="album">{{ song.AlbumName || '—' }}</span>
        <span class="duration">{{ formatDuration(song.Duration) }}</span>
      </div>

      <!-- Pagination -->
      <div style="display:flex; justify-content:center; gap: 14px; margin-top: 24px; font-family:'EB Garamond',serif; font-style:italic;">
        <button 
          class="icon-btn" 
          :disabled="page === 1" 
          style="width:auto; padding: 4px 14px; border-radius:14px;"
          @click="page--"
        >
          ← Previous
        </button>
        <span style="line-height:30px; font-size:16px;">Page {{ page }}</span>
        <button 
          class="icon-btn" 
          :disabled="songs.length < 25" 
          style="width:auto; padding: 4px 14px; border-radius:14px;"
          @click="page++"
        >
          Next →
        </button>
      </div>
    </div>

    <!-- 收藏成功提示 -->
    <div v-if="favoriteMsg" class="toast-msg">
      {{ favoriteMsg }}
    </div>

    <!-- 收藏到歌单弹窗 -->
    <AddToPlaylistModal
      :show="showAddModal"
      :track="trackToAdd"
      @close="showAddModal = false"
      @success="handleFavoriteSuccess"
      @error="handleFavoriteError"
    />
  </div>
</template>

<style scoped>
.search-count {
  font-size: 13px;
  color: var(--text-muted, var(--ink-mute, #8a7e6a));
  white-space: nowrap;
}
.search-count b {
  color: var(--text-primary, var(--ink, #221b12));
  font-weight: 600;
}

.fav-btn {
  background: none;
  border: none;
  padding: 2px 4px;
  margin-left: 8px;
  cursor: pointer;
  opacity: 0;
  transition: opacity 0.2s, color 0.2s;
  color: var(--ink-mute, #8a7e6a);
  vertical-align: middle;
}

.fav-btn svg {
  width: 14px;
  height: 14px;
}

.song-row:hover .fav-btn {
  opacity: 1;
}

.fav-btn:hover {
  color: var(--accent, #a8311b);
}

.toast-msg {
  position: fixed;
  bottom: 80px;
  left: 50%;
  transform: translateX(-50%);
  background: var(--ink, #221b12);
  color: var(--paper, #f1ead8);
  padding: 10px 24px;
  border-radius: 8px;
  font-size: 14px;
  z-index: 1000;
  animation: fadeInUp 0.3s ease;
}

@keyframes fadeInUp {
  from {
    opacity: 0;
    transform: translateX(-50%) translateY(10px);
  }
  to {
    opacity: 1;
    transform: translateX(-50%) translateY(0);
  }
}
</style>
