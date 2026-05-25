<script setup lang="ts">
import { ref, watch } from 'vue';
import { apiGet } from '../api/backend';
import { userStore } from '../api/userStore';

defineProps<{
  activeView: string;
}>();

const emit = defineEmits<{
  (e: 'navigate', view: string, params?: any): void;
}>();

const sidebarNav = [
  { id: 'home', name: '首页', icon: 'M3 11l9-8 9 8v10a2 2 0 0 1-2 2h-4v-7h-6v7H5a2 2 0 0 1-2-2V11z' },
];

interface SidebarPlaylist {
  id: string;
  name: string;
}

type RawRecord = Record<string, unknown>;

const playlists = ref<SidebarPlaylist[]>([]);
const playlistArrayKeys = [
  'list',
  'lists',
  'info',
  'special_list',
  'specialList',
  'cloud_list',
  'cloudList',
  'playlist',
  'playlists',
  'data',
];
const playlistIdKeys = [
  'global_collection_id',
  'global_collectionid',
  'listid',
  'list_id',
  'specialid',
  'special_id',
  'id',
  'list_create_listid',
  'collection_id',
  'gid',
];
const playlistNameKeys = [
  'name',
  'listname',
  'list_name',
  'specialname',
  'special_name',
  'title',
  'filename',
];

function asRecord(value: unknown): RawRecord | null {
  return value && typeof value === 'object' && !Array.isArray(value) ? value as RawRecord : null;
}

function readField(source: RawRecord, keys: string[]) {
  for (const key of keys) {
    const value = source[key];
    if (typeof value === 'string' && value.trim()) return value.trim();
    if (typeof value === 'number' && Number.isFinite(value)) return String(value);
    if (typeof value === 'bigint') return value.toString();
  }
  return '';
}

function looksLikePlaylist(value: unknown) {
  const record = asRecord(value);
  if (!record) return false;
  return !!readField(record, playlistIdKeys) || !!readField(record, playlistNameKeys);
}

function collectPlaylistCandidates(value: unknown, output: RawRecord[], depth = 0) {
  if (depth > 5 || value == null) return;

  if (Array.isArray(value)) {
    const records = value.map(asRecord).filter((item): item is RawRecord => !!item);
    if (records.some(looksLikePlaylist)) {
      output.push(...records.filter(looksLikePlaylist));
      return;
    }
    records.forEach((item) => collectPlaylistCandidates(item, output, depth + 1));
    return;
  }

  const record = asRecord(value);
  if (!record) return;

  if (looksLikePlaylist(record)) {
    output.push(record);
  }

  for (const key of playlistArrayKeys) {
    if (key in record) {
      collectPlaylistCandidates(record[key], output, depth + 1);
    }
  }
}

function normalizePlaylists(payload: unknown): SidebarPlaylist[] {
  const candidates: RawRecord[] = [];
  collectPlaylistCandidates(payload, candidates);

  const seen = new Set<string>();
  return candidates
    .map((item) => ({
      id: readField(item, playlistIdKeys),
      name: readField(item, playlistNameKeys) || '无标题歌单',
    }))
    .filter((item) => {
      if (!item.id || seen.has(item.id)) return false;
      seen.add(item.id);
      return true;
    });
}

async function loadUserPlaylists() {
  if (!userStore.isLoggedIn) {
    playlists.value = [];
    return;
  }
  try {
    const res = await apiGet<any>('/user/playlist', { page: 1, pagesize: 100 });
    playlists.value = normalizePlaylists(res);

    // The user_playlist response carries the real nickname + avatar URL on
    // every entry (list_create_username + create_user_pic). Backfill them
    // into userStore so the sidebar/profile stop showing the fallback "听歌用户".
    const first = res?.data?.info?.[0];
    if (first && typeof first === 'object') {
      const nick = (first as any).list_create_username;
      const pic = (first as any).create_user_pic;
      if (typeof nick === 'string' && nick.trim() &&
          (userStore.username === '听歌用户' || userStore.username === '未登录')) {
        userStore.username = nick.trim();
      }
      if (typeof pic === 'string' && pic.trim() && !userStore.avatar) {
        userStore.avatar = pic.trim();
      }
    }
  } catch (e) {
    console.error('Failed to load user playlists', e);
    playlists.value = [];
  }
}

watch(() => [userStore.isLoggedIn, userStore.userId] as const, ([isLoggedIn]) => {
  if (isLoggedIn) {
    loadUserPlaylists();
  } else {
    playlists.value = [];
  }
}, { immediate: true });

function handleNav(viewId: string) {
  emit('navigate', viewId);
}

function handlePlaylist(playlist: { id: string; name: string }) {
  // Navigate to playlist view
  emit('navigate', 'playlist', { id: playlist.id, name: playlist.name });
}
</script>

<template>
  <aside class="sidebar">
    <!-- Logo Section -->
    <div class="masthead">
      <span class="logo"><i>The</i> Player</span>
    </div>

    <!-- User Section -->
    <div class="user" @click="handleNav('login')" style="cursor: pointer;">
      <div class="avatar" :class="{'has-img': !!userStore.avatar}" :style="userStore.avatar ? { backgroundImage: `url(${userStore.avatar})`, backgroundSize: 'cover' } : {}">
        <span v-if="!userStore.avatar && userStore.isLoggedIn" style="font-size:10px; font-weight:700; color:var(--paper); display:flex; align-items:center; justify-content:center; height:100%;">{{ userStore.username.slice(0,1) }}</span>
      </div>
      <div>
        <div class="name" style="display:flex; align-items:center; gap:6px;">
          {{ userStore.username }}
          <span v-if="userStore.isVip" style="font-size: 8px; background: var(--accent); color: var(--paper); padding: 0px 4px; border-radius: 4px; font-weight:700; line-height: 1.2;">VIP</span>
        </div>
        <div class="meta">{{ userStore.isLoggedIn ? `Lv.${userStore.vipLevel} · 账户中心` : '点此扫码登录' }}</div>
      </div>
    </div>

    <!-- Navigation Menu -->
    <nav class="nav">
      <a 
        v-for="item in sidebarNav" 
        :key="item.id"
        :class="{ active: activeView === item.id }"
        @click="handleNav(item.id)"
      >
        <svg class="ico" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
          <path :d="item.icon" />
        </svg>
        {{ item.name }}
      </a>
    </nav>

    <!-- Playlists section -->
    <div class="section-label">我的歌单 · Playlists</div>
    <nav class="playlists">
      <template v-if="userStore.isLoggedIn">
        <template v-if="playlists.length > 0">
          <a 
            v-for="pl in playlists" 
            :key="pl.id"
            @click="handlePlaylist(pl)"
          >
            <span class="dot"></span>
            {{ pl.name }}
          </a>
        </template>
        <div v-else class="playlist-placeholder">暂无歌单</div>
      </template>
      <div v-else class="playlist-placeholder" @click="handleNav('login')" style="cursor: pointer;">
        扫码登录后查看歌单
      </div>
    </nav>

    <!-- Footer Stamp -->
    <div class="sidebar-footer">
      <div class="stamp">印</div>
      <div>Printed daily<br/>since 2026</div>
    </div>
  </aside>
</template>

<style scoped>
/* Scoped overrides if any, but mostly relies on style.css */
</style>
