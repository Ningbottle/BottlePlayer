<script setup lang="ts">
import { ref, computed, onMounted } from 'vue';
import { apiGet } from '../platform/tauri/nativeClient';
import { playAll, playerStore } from '../playback/playerStore';
import { normalizeTrack } from '../shared/music/track';
import { userStore } from '../api/userStore';
import { recentPlayedStore, type RecentPlayedEntry } from '../playback/data/recentPlayedStore';
import SkinPageHeader from '../shared/ui/SkinPageHeader.vue';
import SkinEmptyState from '../shared/ui/SkinEmptyState.vue';

const loading = ref(false);
const remoteError = ref('');
const remoteEntries = ref<RecentPlayedEntry[]>([]);

// Local-first: mergeRemote with empty remote returns a sorted copy of local.
// When remote entries arrive, the computed recomputes the merged list. Local
// entries render immediately on mount — no network wait.
const displaySongs = computed(() => recentPlayedStore.mergeRemote(remoteEntries.value));

/** Map a KuGou /user/history item to a RecentPlayedEntry with a playedAt ts. */
function remoteItemToEntry(item: any, idx: number): RecentPlayedEntry | null {
  const song = item.info || item;
  const FileHash = song.hash || song.FileHash || '';
  if (!FileHash) return null;
  const rawTime = item.time ?? item.addtime ?? item.play_time ?? item.playtime;
  let playedAt: number;
  if (typeof rawTime === 'number' && rawTime > 0) {
    // KuGou uses unix seconds; convert to ms. If already ms, keep as-is.
    playedAt = rawTime > 1e12 ? rawTime : rawTime * 1000;
  } else {
    // No timestamp — preserve remote order via descending synthetic.
    playedAt = Date.now() - idx * 1000;
  }
  return {
    FileHash,
    SongName: song.name || song.songname || song.SongName || '未知歌曲',
    SingerName: song.singername || song.SingerName || song.author_name || '未知歌手',
    AlbumName: song.album_name || song.albumname || song.AlbumName || song.albuminfo?.name || undefined,
    AlbumID: String(song.album_id || song.albumid || song.AlbumID || ''),
    Image: song.cover || song.trans_param?.union_cover || song.albuminfo?.sizable_cover || undefined,
    Duration: song.timelen ? Math.round(song.timelen / 1000) : (song.duration || 0),
    playedAt,
  };
}

async function loadRemoteHistory() {
  // Local-only when logged out — remote sync is a login-gated best-effort path.
  if (!userStore.isLoggedIn) return;
  loading.value = true;
  remoteError.value = '';
  try {
    const res = await apiGet<any>('/user/history', { pagesize: 100 });
    if (res?.status === 1 && res?.data) {
      const list = res.data.info || res.data.list || res.data.songs || res.data.data || [];
      remoteEntries.value = list
        .map((item: any, idx: number) => remoteItemToEntry(item, idx))
        .filter((e: RecentPlayedEntry | null): e is RecentPlayedEntry => e !== null);
    } else {
      remoteError.value = res?.error || '远端同步失败';
    }
  } catch (err: any) {
    console.error('Load remote history error', err);
    remoteError.value = '远端同步失败，已显示本地记录';
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  loadRemoteHistory();
});

function handlePlay(song: RecentPlayedEntry) {
  const tracks = displaySongs.value.map((e) => normalizeTrack(e));
  const idx = tracks.findIndex((t) => t.FileHash === song.FileHash);
  playAll(tracks, idx >= 0 ? idx : 0);
}

function formatDuration(sec: number) {
  if (!sec) return '00:00';
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
}

const isCurrentTrack = (song: RecentPlayedEntry) => {
  return playerStore.currentTrack?.FileHash === song.FileHash;
};
</script>

<template>
  <div class="list-view">
    <SkinPageHeader title="播放历史" kicker="RECENTLY PLAYED · 最近播放">
      <template #actions>
        <span class="history-count">共 <b>{{ displaySongs.length }}</b> 首</span>
      </template>
    </SkinPageHeader>

    <!-- Non-blocking sync status (local entries remain visible below) -->
    <div v-if="!userStore.isLoggedIn" class="sync-hint">登录后可同步远端历史 · 当前显示本地记录</div>
    <div v-else-if="loading" class="sync-hint">同步远端历史中…</div>
    <div v-else-if="remoteError" class="sync-hint" style="color: var(--accent);">{{ remoteError }}</div>

    <!-- Empty: both local AND remote empty -->
    <SkinEmptyState v-if="displaySongs.length === 0" message="暂无播放记录" />

    <!-- Song Table List (local-first, rendered immediately) -->
    <div v-else>
      <div class="song-row" style="font-weight: 600; border-bottom: 2px solid var(--ink); cursor: default; background: transparent;">
        <span class="index">#</span>
        <span class="title">歌名</span>
        <span class="artist">歌手</span>
        <span class="album">专辑</span>
        <span class="duration">时长</span>
      </div>

      <div
        v-for="(song, idx) in displaySongs"
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
.list-view {
  box-sizing: border-box;
  width: 100%;
  max-width: 100%;
  min-width: 0;
  padding-bottom: 24px;
  overflow-x: hidden;
}

.sync-hint {
  font-family: var(--font-serif);
  font-style: italic;
  font-size: 12px;
  color: var(--ink-mute);
  padding: 8px 0 14px;
}
.history-count {
  font-size: 13px;
  color: var(--text-muted);
}
.history-count b {
  font-weight: 600;
  color: var(--text-secondary);
}
</style>
