<script setup lang="ts">
import { ref, watch, onMounted } from 'vue';
import { apiGet } from '../api/backend';
import { playTrack, playerStore } from '../api/playerStore';
import { Track as SongInfo, normalizeTrack } from '../api/normalizer';


const props = defineProps<{
  query: string;
}>();

const loading = ref(false);
const songs = ref<SongInfo[]>([]);
const totalCount = ref(0);
const page = ref(1);
const error = ref('');

async function performSearch() {
  if (!props.query) {
    songs.value = [];
    totalCount.value = 0;
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

    if (res.status === 1 && res.data) {
      songs.value = (res.data.lists || []).map(normalizeTrack);
      totalCount.value = res.data.total || 0;
    } else {
      error.value = res.error || '检索失败，请稍后重试';
    }
  } catch (err: any) {
    console.error('Search error', err);
    error.value = '连接 C++ 后端 Sidecar 出错';
  } finally {
    loading.value = false;
  }
}

watch(() => props.query, () => {
  page.value = 1;
  performSearch();
});

watch(page, () => {
  performSearch();
});

onMounted(() => {
  performSearch();
});

function handlePlay(song: SongInfo) {
  playTrack(song);
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
        <div class="kicker">SEARCH RESULTS · 通讯检索</div>
        <h1>关于“{{ query }}”</h1>
      </div>
      <div class="date">
        找到大约 <b>{{ totalCount }}</b> 条结果
      </div>
    </div>

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
        <span class="title">{{ song.SongName }}</span>
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
  </div>
</template>

<style scoped>
/* Scoped overrides */
</style>
