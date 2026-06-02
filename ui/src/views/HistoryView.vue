<script setup lang="ts">
import { ref, onMounted } from 'vue';
import { apiGet } from '../api/backend';
import { playAll, playerStore } from '../api/playerStore';
import { Track as SongInfo, normalizeTrack } from '../api/normalizer';
import { userStore } from '../api/userStore';

const loading = ref(false);
const songs = ref<SongInfo[]>([]);
const error = ref('');

async function loadHistory() {
  if (!userStore.isLoggedIn) {
    error.value = '请先登录后查看播放历史';
    return;
  }
  
  loading.value = true;
  error.value = '';
  try {
    const res = await apiGet<any>('/user/history', { pagesize: 100 });
    if (res?.status === 1 && res?.data) {
      const list = res.data.info || res.data.list || res.data.songs || res.data.data || [];
      songs.value = list.map((item: any) => {
        // 播放历史嵌套结构：item.info 包含歌曲详情
        const song = item.info || item;
        const normalized = {
          ...song,
          FileHash: song.hash || song.FileHash || '',
          SongName: song.name || song.songname || song.SongName || '未知歌曲',
          SingerName: song.singername || song.SingerName || song.author_name || '未知歌手',
          AlbumName: song.album_name || song.albumname || song.AlbumName || song.albuminfo?.name || '',
          AlbumID: String(song.album_id || song.albumid || song.AlbumID || ''),
          AlbumAudioID: String(song.mixsongid || song.album_audio_id || item.mxid || ''),
          Duration: song.timelen ? Math.round(song.timelen / 1000) : (song.duration || 0),
          Image: song.cover || song.trans_param?.union_cover || song.albuminfo?.sizable_cover || undefined,
        };
        return normalizeTrack(normalized);
      });
    } else {
      error.value = res?.error || '获取播放历史失败';
    }
  } catch (err: any) {
    console.error('Load history error', err);
    error.value = '连接后端出错';
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  loadHistory();
});

function handlePlay(song: SongInfo) {
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
</script>

<template>
  <div class="list-view">
    <div class="page-head">
      <div>
        <div class="kicker">RECENTLY PLAYED · 最近播放</div>
        <h1>播放历史</h1>
      </div>
      <div class="date">
        共 <b>{{ songs.length }}</b> 首
      </div>
    </div>

    <!-- Not logged in -->
    <div v-if="!userStore.isLoggedIn" class="spinner" style="flex-direction:column; gap:12px;">
      <div>登录后查看播放历史</div>
    </div>

    <!-- Spinner -->
    <div v-else-if="loading" class="spinner">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
        <circle cx="12" cy="12" r="10" stroke="rgba(34,27,18,0.1)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
      </svg>
      加载播放历史中…
    </div>

    <!-- Error message -->
    <div v-else-if="error" class="spinner" style="color: var(--accent);">
      {{ error }}
    </div>

    <!-- Empty results -->
    <div v-else-if="songs.length === 0" class="spinner">
      暂无播放记录
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
        :key="song.FileHash + '-' + idx"
        class="song-row"
        :class="{ active: isCurrentTrack(song) }"
        @click="handlePlay(song)"
      >
        <span class="index">{{ idx + 1 }}</span>
        <span class="title">{{ song.SongName }}</span>
        <span class="artist">{{ song.SingerName }}</span>
        <span class="album">{{ song.AlbumName || '—' }}</span>
        <span class="duration">{{ formatDuration(song.Duration) }}</span>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* Scoped overrides */
</style>
