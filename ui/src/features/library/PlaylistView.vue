<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch, computed } from 'vue';
import { describeBackendError } from '../../platform/tauri/nativeClient';
import { fetchPlaylistTracks, fetchAllPlaylistTracks } from './playlistGateway';
import { playAll, playerStore } from '../../playback/index';
import { Track as SongInfo, normalizeTrack } from '../../shared/music/track';
import { favoriteStore, type LikedOwnership } from './favoriteStore';
import SkinPageHeader from '../../shared/ui/SkinPageHeader.vue';


const props = defineProps<{
  playlistId: string;
  playlistName: string;
  playlistSource?: string;
}>();

const loading = ref(false);
const songs = ref<SongInfo[]>([]);
const totalCount = ref(0);
const page = ref(1);
const error = ref('');
/** Feedback for a partial / cancelled "play all" — never a silent truncation. */
const queueNotice = ref('');
const queueBusy = ref(false);

const PAGE_SIZE = 50;

/** Bumps on every load so slower older responses cannot overwrite newer playlist/page state. */
let playlistGeneration = 0;
/** Identity whose rows `songs` currently holds, as `<account>|<playlistId>`. */
const loadedIdentity = ref('');
/** Identity of the account-verified liked playlist for the playlist being shown. */
const likedOwnership = ref<LikedOwnership | null>(null);

function isUserCollectionId(id: string): boolean {
  return id.startsWith('collection_');
}

function currentIdentity(): string {
  const { epoch, uid } = favoriteStore.accountScope;
  return `${epoch}:${uid}|${props.playlistId}`;
}

/**
 * The loaded rows belong to what is on screen right now. Rows kept from another
 * account or another playlist are neither countable nor queueable here.
 */
const listIsCurrent = computed(
  () => loadedIdentity.value !== '' && loadedIdentity.value === currentIdentity(),
);

function isCurrent(gen: number): boolean {
  return gen === playlistGeneration;
}

/**
 * F01: liked markers come from the account's authoritative liked-playlist id,
 * never from the playlist name — a public playlist titled「我喜欢的音乐」used to
 * write this account's favorite state and light every heart.
 */
async function verifyLikedOwnership(gen: number): Promise<LikedOwnership | null> {
  const ownership = await favoriteStore.resolveLikedOwnership(props.playlistId);
  if (!isCurrent(gen)) return null;
  if (ownership.epoch !== favoriteStore.accountEpoch || ownership.uid !== favoriteStore.accountId) return null;
  likedOwnership.value = ownership;
  return ownership;
}

function syncLikedMarkersFromCurrentPage(ownership?: LikedOwnership | null): void {
  const verified = ownership ?? likedOwnership.value;
  if (!verified || !verified.owned) return;
  if (verified.playlistId !== props.playlistId) return;
  favoriteStore.hydrateLikedPage(songs.value, verified);
}

async function loadPlaylistTracks() {
  const gen = ++playlistGeneration;
  const identity = currentIdentity();
  if (identity !== loadedIdentity.value) {
    // This load describes a different account or playlist than the one on
    // screen. Drop the previous rows immediately — while it is still loading
    // (or once it fails) they must not be counted or queued under a new title.
    songs.value = [];
    totalCount.value = 0;
    loadedIdentity.value = '';
  }
  likedOwnership.value = null;
  invalidateFullQueue();
  queueNotice.value = '';
  if (!props.playlistId) {
    loading.value = false;
    return;
  }
  if (props.playlistSource === 'user' && !isUserCollectionId(props.playlistId)) {
    loading.value = false;
    error.value = '歌单标识无效（缺少 global_collection_id）';
    songs.value = [];
    totalCount.value = 0;
    return;
  }
  loading.value = true;
  error.value = '';
  try {
    const [res, ownership] = await Promise.all([
      fetchPlaylistTracks<SongInfo>({
        id: props.playlistId,
        page: page.value,
        pagesize: PAGE_SIZE
      }),
      verifyLikedOwnership(gen),
    ]);

    if (gen !== playlistGeneration) return;

    if (res.status === 1 && res.data) {
      songs.value = (res.data.list || []).map(normalizeTrack);
      totalCount.value = res.data.total || songs.value.length;
      loadedIdentity.value = identity;
      // 「我喜欢的音乐」中的曲目应点亮底栏红心（不必再次点收藏）。通过共享
      // favoriteStore 投影，同时归档曲目供后续取消收藏使用 —— 但只有当权威身份
      // 确认这个 id 就是当前账号的喜欢歌单时才写入，身份未知时保持不亮。
      syncLikedMarkersFromCurrentPage(ownership);
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

// ── F04: whole-playlist queue ───────────────────────────────────────────────
// The visible page holds at most PAGE_SIZE tracks; "播放全部" and clicking a row
// must queue the *entire* playlist. The full list is fetched lazily, once per
// playlist, shared between concurrent callers, and dropped whenever the
// playlist, page, or account changes.
const fullQueue = ref<SongInfo[] | null>(null);
let fullQueuePromise: Promise<SongInfo[] | null> | null = null;
let fullQueuePlaylistId = '';
/** Separate from playlistGeneration so a user can cancel one queue walk only. */
let fullQueueGeneration = 0;

function invalidateFullQueue(): void {
  fullQueueGeneration += 1;
  fullQueue.value = null;
  fullQueuePromise = null;
  fullQueuePlaylistId = '';
  // Context changes and explicit cancellation must release the visible action
  // immediately. A stale walk's finally block is generation-guarded below.
  queueBusy.value = false;
}

async function ensureFullQueue(): Promise<SongInfo[] | null> {
  const playlistId = props.playlistId;
  if (!playlistId) return null;
  if (fullQueue.value && fullQueuePlaylistId === playlistId) return fullQueue.value;
  if (fullQueuePromise && fullQueuePlaylistId === playlistId) return fullQueuePromise;

  const gen = playlistGeneration;
  const queueGen = fullQueueGeneration;
  fullQueuePlaylistId = playlistId;
  const ownsWalk = () => gen === playlistGeneration
    && queueGen === fullQueueGeneration
    && playlistId === props.playlistId;
  const walk: Promise<SongInfo[] | null> = (async () => {
    const result = await fetchAllPlaylistTracks<SongInfo>({
      id: playlistId,
      pageSize: PAGE_SIZE,
      isCurrent: ownsWalk,
    });
    // Stale walk (playlist/page/account moved on): never publish or report it.
    if (gen !== playlistGeneration || queueGen !== fullQueueGeneration || playlistId !== props.playlistId) return null;
    if (result.cancelled) return null;
    const tracks = result.tracks.map(normalizeTrack);
    if (tracks.length === 0) {
      const detail = result.error ? `：${result.error}` : '';
      queueNotice.value = `完整歌单加载失败${detail}，将仅排入当前已加载的 ${songs.value.length} 首`;
      return null;
    }
    if (result.incomplete) {
      const detail = result.error ? `：${result.error}` : '：曲目列表不完整';
      queueNotice.value = `歌单共 ${result.total || '?'} 首${detail}，已排入 ${tracks.length} 首`;
    }
    // Partial data remains playable with a warning, but must not suppress a
    // later retry or silently lose that warning on the next play-all click.
    if (!result.incomplete) fullQueue.value = tracks;
    return tracks;
  })().finally(() => {
    if (fullQueuePromise === walk) fullQueuePromise = null;
  });
  fullQueuePromise = walk;
  return walk;
}

interface QueueStart {
  track: SongInfo;
  visibleIndex: number;
  page: number;
}

async function queueFrom(start?: QueueStart): Promise<void> {
  if (queueBusy.value) return;
  queueBusy.value = true;
  const gen = playlistGeneration;
  const queueGen = fullQueueGeneration;
  const playlistId = props.playlistId;
  queueNotice.value = '';
  let tracks: SongInfo[] | null = null;
  try {
    tracks = await ensureFullQueue();
  } finally {
    if (gen === playlistGeneration && queueGen === fullQueueGeneration) queueBusy.value = false;
  }
  if (gen !== playlistGeneration || queueGen !== fullQueueGeneration || playlistId !== props.playlistId) return;
  // Only fall back to the loaded page when the full walk produced nothing at
  // all — and the notice above then tells the user the queue is partial.
  let queue = tracks && tracks.length > 0 ? tracks : songs.value;
  if (queue.length === 0) return;
  syncLikedMarkersFromCurrentPage();
  let index = 0;
  if (start) {
    if (tracks && tracks.length > 0) {
      const absoluteIndex = (start.page - 1) * PAGE_SIZE + start.visibleIndex;
      // Pagination order is the only identity that distinguishes duplicate
      // hashes. Verify the slot before using it; a partial/reordered full walk
      // must not silently start some other occurrence.
      if (tracks[absoluteIndex]?.FileHash === start.track.FileHash) {
        index = absoluteIndex;
      } else {
        queue = songs.value;
        index = start.visibleIndex;
        queueNotice.value = `完整歌单未覆盖当前曲目，已仅排入当前页 ${queue.length} 首`;
      }
    } else {
      index = start.visibleIndex;
    }
  }
  playAll(queue, Math.max(0, Math.min(index, queue.length - 1)));
}

const canPlayAll = computed(() => songs.value.length > 0 && !queueBusy.value);

function handlePlay(song: SongInfo, visibleIndex: number) {
  // 用整张歌单作为播放队列，从点击的这首开始 —— 这样“下一首”才会沿着歌单走，
  // 而不是把单曲追加到一个无关的历史队列里。
  void queueFrom({ track: song, visibleIndex, page: page.value });
}

function handlePlayAll() {
  if (songs.value.length === 0) return;
  void queueFrom();
}

function cancelQueueLoading(): void {
  if (!queueBusy.value) return;
  invalidateFullQueue();
  queueNotice.value = '完整歌单加载已取消，未开始播放。';
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

// Logout / account switch: the store dropped the markers it held, and any
// ownership answer or in-flight full-queue walk we hold was decided for the
// previous account. Re-verify everything from scratch.
watch(() => favoriteStore.accountScope, () => {
  likedOwnership.value = null;
  invalidateFullQueue();
  queueNotice.value = '';
  if (props.playlistId) void loadPlaylistTracks();
}, { deep: true });

onBeforeUnmount(() => {
  // Retire this view's generation so an in-flight page walk or ownership
  // answer can no longer write state after unmount.
  playlistGeneration += 1;
  invalidateFullQueue();
});
</script>

<template>
  <div class="list-view">
    <SkinPageHeader
      :title="playlistName || '歌单'"
      kicker="PLAYLIST · 歌单"
    >
      <template #actions>
        <div class="playlist-header-actions">
          <span v-if="listIsCurrent" class="playlist-count" data-test="playlist-count">曲目数 <b>{{ totalCount }}</b> 首</span>
          <button
            v-if="listIsCurrent && songs.length > 0"
            class="play-cta"
            style="font-size:12px; padding: 6px 14px;"
            :disabled="!canPlayAll"
            data-test="playlist-play-all"
            @click="handlePlayAll"
          >
            <span class="pp" style="width:18px; height:18px;">
              <svg viewBox="0 0 24 24" fill="currentColor" width="8" height="8">
                <polygon points="6,4 20,12 6,20"/>
              </svg>
            </span>
            {{ queueBusy ? '排队中…' : '播放全部' }}
          </button>
          <button
            v-if="queueBusy"
            class="icon-btn"
            type="button"
            data-test="playlist-cancel-queue"
            @click="cancelQueueLoading"
          >
            取消加载
          </button>
          <span v-if="queueNotice" class="playlist-queue-notice" data-test="playlist-queue-notice">
            {{ queueNotice }}
          </span>
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
      <button type="button" class="list-retry" data-test="playlist-load-retry" @click="loadPlaylistTracks">重试</button>
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
        :key="`${(page - 1) * PAGE_SIZE + idx}:${song.FileHash}`"
        class="song-row"
        :class="{ active: isCurrentTrack(song) }"
        role="button"
        tabindex="0"
        :aria-label="`播放 ${song.SongName}`"
        @click="handlePlay(song, idx)"
        @keydown.enter.prevent="handlePlay(song, idx)"
        @keydown.space.prevent="handlePlay(song, idx)"
      >
        <span class="index">{{ (page - 1) * PAGE_SIZE + idx + 1 }}</span>
        <span class="title">{{ song.SongName }}</span>
        <span class="artist">{{ song.SingerName }}</span>
        <span class="album">{{ song.AlbumName || '—' }}</span>
        <span class="duration">{{ formatDuration(song.Duration) }}</span>
      </div>

      <!-- Pagination if needed -->
      <div v-if="totalCount > PAGE_SIZE" style="display:flex; justify-content:center; gap: 14px; margin-top: 24px; font-family:'EB Garamond',serif; font-style:italic;">
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
          :disabled="songs.length < PAGE_SIZE"
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
.playlist-queue-notice {
  font-size: 12px;
  font-family: var(--font-mono, ui-monospace, monospace);
  color: var(--vermillion, #b14e35);
  text-align: right;
}
.play-cta:disabled {
  opacity: 0.55;
  cursor: default;
}
</style>
