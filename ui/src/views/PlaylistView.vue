<script setup lang="ts">
import { ref, onMounted, watch } from 'vue';
import { apiGet, describeBackendError } from '../api/backend';
import { playAll, playerStore } from '../api/playerStore';
import { Track as SongInfo, normalizeTrack } from '../api/normalizer';
import { favoriteStore, isLikedPlaylistName } from '../api/favoriteStore';
import SkinPageHeader from '../components/primitives/SkinPageHeader.vue';


const props = defineProps<{
  playlistId: string;
  playlistName: string;
}>();

const loading = ref(false);
const songs = ref<SongInfo[]>([]);
const totalCount = ref(0);
const page = ref(1);
const error = ref('');

/** Bumps on every load so slower older responses cannot overwrite newer playlist/page state. */
let playlistGeneration = 0;

async function loadPlaylistTracks() {
  const gen = ++playlistGeneration;
  if (!props.playlistId) {
    loading.value = false;
    return;
  }
  loading.value = true;
  error.value = '';
  try {
    const res = await apiGet<{ status: number; error?: string; data?: { list: SongInfo[], total: number } }>('/playlist/track/all', {
      id: props.playlistId,
      page: page.value,
      pagesize: 50
    });

    if (gen !== playlistGeneration) return;

    if (res.status === 1 && res.data) {
      songs.value = (res.data.list || []).map(normalizeTrack);
      totalCount.value = res.data.total || songs.value.length;
      // 「我喜欢的音乐」中的曲目应点亮底栏红心（不必再次点收藏）。通过共享
      // favoriteStore 投影，同时归档曲目供后续取消收藏使用。
      if (isLikedPlaylistName(props.playlistName)) {
        favoriteStore.hydrateLikedPage(songs.value);
      }
    } else {
      error.value = res.error || '无法获取歌单曲目';
    }
  } catch (err: any) {
    if (gen !== playlistGeneration) return;
    console.error('Playlist load error', err);
    error.value = describeBackendError(err, '歌单加载失败，请稍后重试');
  } finally {
    if (gen === playlistGeneration) {
      loading.value = false;
    }
  }
}

// Single load entry: when playlistId changes on page>1, only reset page and let
// the page watcher fetch — avoid page=1 + loadPlaylistTracks() double request.
watch(() => props.playlistId, () => {
  if (page.value !== 1) {
    page.value = 1;
  } else {
    loadPlaylistTracks();
  }
});

watch(page, () => {
  loadPlaylistTracks();
});

onMounted(() => {
  loadPlaylistTracks();
});

function syncLikedMarkersFromCurrentPage(): void {
  if (!isLikedPlaylistName(props.playlistName)) return;
  favoriteStore.hydrateLikedPage(songs.value);
}

function handlePlay(song: SongInfo) {
  // 用整张歌单作为播放队列，从点击的这首开始 —— 这样“下一首”才会沿着歌单走，
  // 而不是把单曲追加到一个无关的历史队列里。
  syncLikedMarkersFromCurrentPage();
  const idx = songs.value.findIndex(s => s.FileHash === song.FileHash);
  playAll(songs.value, idx >= 0 ? idx : 0);
}

function handlePlayAll() {
  if (songs.value.length === 0) return;
  syncLikedMarkersFromCurrentPage();
  playAll(songs.value, 0);
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
</script>

<template>
  <div class="list-view">
    <SkinPageHeader
      :title="playlistName || '歌单'"
      kicker="PLAYLIST · 歌单"
    >
      <template #actions>
        <div class="playlist-header-actions">
          <span class="playlist-count">曲目数 <b>{{ totalCount }}</b> 首</span>
          <button
            v-if="songs.length > 0"
            class="play-cta"
            style="font-size:12px; padding: 6px 14px;"
            @click="handlePlayAll"
          >
            <span class="pp" style="width:18px; height:18px;">
              <svg viewBox="0 0 24 24" fill="currentColor" width="8" height="8">
                <polygon points="6,4 20,12 6,20"/>
              </svg>
            </span>
            播放全部
          </button>
        </div>
      </template>
    </SkinPageHeader>

    <!-- Spinner -->
    <div v-if="loading" class="spinner">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
        <circle cx="12" cy="12" r="10" stroke="rgba(34,27,18,0.1)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
      </svg>
      采编中…
    </div>

    <!-- Error message -->
    <div v-else-if="error" class="spinner" style="color: var(--accent);">
      {{ error }}
    </div>

    <!-- Empty playlist -->
    <div v-else-if="songs.length === 0" class="spinner">
      该歌单暂无曲目记录。
    </div>

    <!-- Songs table list -->
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
        <span class="index">{{ (page - 1) * 50 + idx + 1 }}</span>
        <span class="title">{{ song.SongName }}</span>
        <span class="artist">{{ song.SingerName }}</span>
        <span class="album">{{ song.AlbumName || '—' }}</span>
        <span class="duration">{{ formatDuration(song.Duration) }}</span>
      </div>

      <!-- Pagination if needed -->
      <div v-if="totalCount > 50" style="display:flex; justify-content:center; gap: 14px; margin-top: 24px; font-family:'EB Garamond',serif; font-style:italic;">
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
          :disabled="songs.length < 50" 
          style="width:auto; padding: 4px 14px; border-radius:14px;"
          @click="page++"
        >
          Next →
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.playlist-header-actions {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 10px;
}
.playlist-count {
  font-size: 13px;
  color: var(--text-muted, var(--ink-mute, #8a7e6a));
  white-space: nowrap;
}
.playlist-count b {
  color: var(--text-primary, var(--ink, #221b12));
  font-weight: 600;
}
</style>
