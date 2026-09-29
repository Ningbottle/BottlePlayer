<script setup lang="ts">
import { ref, computed, watch, onBeforeUnmount } from 'vue';
import { fetchUserHistory } from './historyGateway';
import { playAll, playerStore } from '../../playback/index';
import { normalizeTrack } from '../../shared/music/track';
import { userStore } from '../account';
import { recentPlayedStore, type RecentPlayedEntry } from '../../playback/index';
import SkinPageHeader from '../../shared/ui/SkinPageHeader.vue';
import SkinEmptyState from '../../shared/ui/SkinEmptyState.vue';

const loading = ref(false);
const remoteError = ref('');
const remoteEntries = ref<RecentPlayedEntry[]>([]);
let loadGeneration = 0;

// Local-first: mergeRemote with empty remote returns a sorted copy of local.
// When remote entries arrive, the computed recomputes the merged list. Local
// entries render immediately on mount — no network wait.
const displaySongs = computed(() => recentPlayedStore.mergeRemote(remoteEntries.value));

/** Map a KuGou /user/history item to a RecentPlayedEntry with a playedAt ts. */
function normalizeRemotePlayedAt(candidates: unknown[]): number {
  for (const candidate of candidates) {
    if (typeof candidate !== 'number' && typeof candidate !== 'string') continue;
    if (typeof candidate === 'string' && candidate.trim() === '') continue;
    const value = Number(candidate);
    if (!Number.isFinite(value) || value <= 0) continue;
    // Values below 1e11 are Unix seconds; values at or above it are epoch ms.
    // This keeps 1990s millisecond dates (below the old 1e12 cutoff) intact.
    return value < 100_000_000_000 ? value * 1000 : value;
  }
  return 0;
}

function remoteItemToEntry(item: any): RecentPlayedEntry | null {
  if (!item || typeof item !== 'object') return null;
  const song = item.info || item;
  const track = normalizeTrack({ ...song, SongName: song.SongName || song.name || song.songname || song.filename });
  if (typeof track.FileHash !== 'string' || !track.FileHash.trim()) return null;
  // Unknown dates sort after known listens; stable sort preserves remote order.
  // Never fabricate "now", which would hide/reorder actual local playback.
  const playedAt = normalizeRemotePlayedAt([
    item.time, item.addtime, item.play_time, item.playtime, item.timestamp,
    song.time, song.addtime, song.play_time, song.playtime, song.timestamp,
  ]);
  return {
    ...track,
    FileHash: track.FileHash.trim(),
    AlbumName: track.AlbumName || song.albuminfo?.name,
    Duration: Number.isFinite(track.Duration) && track.Duration > 0 ? track.Duration : 0,
    playedAt,
  };
}

async function loadRemoteHistory() {
  // Local-only when logged out — remote sync is a login-gated best-effort path.
  const generation = ++loadGeneration;
  if (!userStore.isLoggedIn || userStore.deviceReady === false) {
    loading.value = false;
    return;
  }
  loading.value = true;
  remoteError.value = '';
  try {
    const res = await fetchUserHistory(100);
    if (generation !== loadGeneration) return;
    if (res?.status === 1 && res?.data) {
      const list = [res.data.info, res.data.list, res.data.songs, res.data.data].find(Array.isArray) ?? [];
      remoteEntries.value = list
        .map(remoteItemToEntry)
        .filter((e: RecentPlayedEntry | null): e is RecentPlayedEntry => e !== null);
    } else {
      remoteError.value = res?.error || '远端同步失败';
    }
  } catch (err: any) {
    if (generation !== loadGeneration) return;
    console.error('Load remote history error', err);
    remoteError.value = '远端同步失败，已显示本地记录';
  } finally {
    if (generation === loadGeneration) loading.value = false;
  }
}

watch(
  () => [userStore.isLoggedIn, userStore.userId, userStore.deviceReady] as const,
  () => {
    remoteEntries.value = [];
    remoteError.value = '';
    void loadRemoteHistory();
  },
  { immediate: true, flush: 'sync' },
);
onBeforeUnmount(() => { loadGeneration++; });

function handlePlay(song: RecentPlayedEntry) {
  const tracks = displaySongs.value.map((e) => normalizeTrack(e));
  const idx = tracks.findIndex((t) => t.FileHash === song.FileHash);
  playAll(tracks, idx >= 0 ? idx : 0);
}

function formatDuration(sec: number) {
  if (!Number.isFinite(sec) || sec <= 0) return '—';
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
    <div v-else-if="remoteError" class="sync-hint" role="status" style="color: var(--accent);">
      {{ remoteError }}
      <button type="button" class="history-retry" @click="loadRemoteHistory">重试同步</button>
    </div>

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
        :key="song.FileHash"
        class="song-row"
        :class="{ active: isCurrentTrack(song) }"
        role="button"
        tabindex="0"
        :aria-label="`播放 ${song.SongName}`"
        @click="handlePlay(song)"
        @keydown.enter.prevent="handlePlay(song)"
        @keydown.space.prevent="handlePlay(song)"
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
.history-retry {
  margin-left: 10px;
  color: inherit;
  font: inherit;
  background: transparent;
  border: 1px solid currentColor;
  border-radius: 4px;
  cursor: pointer;
}
</style>
