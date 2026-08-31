<script setup lang="ts">
import { ref, watch, onMounted, computed } from 'vue';
import { useRoute } from 'vue-router';
import { checkForUpdate } from '../../platform/tauri/updater';
import { fetchUserPlaylistsRaw } from '../../features/library/playlistGateway';
import { userStore } from '../../features/account';
import { normalizePlaylists, type UserPlaylist } from '../../api/favoriteStore';
import { useSkippedVersion, getSkippedVersion } from '../update/skippedVersion';
import { useThemeStore } from '../appearance/themeStore';

const emit = defineEmits<{
  (e: 'navigate', view: string, params?: any): void;
}>();

const themeStore = useThemeStore();
const skinId = themeStore.skinId;
const route = useRoute();
const activeView = computed(() => route.name);

const sidebarNav = [
  { id: 'home', name: '首页', icon: 'M3 11l9-8 9 8v10a2 2 0 0 1-2 2h-4v-7h-6v7H5a2 2 0 0 1-2-2V11z' },
  { id: 'stats', name: '统计', icon: 'M3 3v18h18M7 14l4-4 4 4 5-5' },
  { id: 'history', name: '最近播放', icon: 'M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z' },
  { id: 'equalizer', name: '均衡器', icon: 'M4 21v-7M4 10V3m8 18v-9m0-4V3m8 18v-5m0-4V3M2 14h4m4-6h4m4 5h4' },
];

type SidebarPlaylist = Pick<UserPlaylist, 'id' | 'name'>;

const playlists = ref<SidebarPlaylist[]>([]);
const playlistsLoading = ref(false);
const playlistError = ref('');
let playlistLoadGeneration = 0;

// 自动更新提示：启动时静默 check() 一次；发现新版本才在 logo 下冒标记，点击去设置页安装。
const updateAvailable = ref(false);
const updateVersion = ref('');
// Reactive watch on the shared "skipped version" store: if the user
// clicks "跳过此版本" in Settings, the badge clears immediately
// without needing to remount this component.
const skippedVersion = useSkippedVersion();
watch(skippedVersion, () => {
  if (skippedVersion.value && updateVersion.value &&
      skippedVersion.value === updateVersion.value) {
    updateAvailable.value = false;
  }
}, { immediate: true });
onMounted(async () => {
  try {
    const update = await checkForUpdate();
    if (update) {
      const skipped = getSkippedVersion();
      if (skipped !== update.version) {
        updateAvailable.value = true;
        updateVersion.value = update.version;
      }
    }
  } catch (e) {
    console.warn('Update check failed', e);
  }
});

async function loadUserPlaylists() {
  if (!userStore.isLoggedIn) {
    playlistLoadGeneration += 1;
    playlists.value = [];
    playlistsLoading.value = false;
    playlistError.value = '';
    return;
  }
  if (!userStore.deviceReady) {
    playlistLoadGeneration += 1;
    playlistsLoading.value = false;
    return;
  }
  const generation = ++playlistLoadGeneration;
  playlistsLoading.value = true;
  try {
    const res = await fetchUserPlaylistsRaw(1, 100);
    if (generation !== playlistLoadGeneration) return;
    if (res?.status !== 1) {
      if (String(res?.error_code) === 'native_user_playlist_id_contract_invalid') {
        throw new Error('歌单标识无效（缺少 global_collection_id）');
      }
      const code = res?.error_code;
      throw new Error(code ? `酷狗错误码 ${code}` : '歌单接口返回失败');
    }
    const normalized = normalizePlaylists(res);
    const skipped = Number(res?.data?.skipped_invalid_id_count || 0);
    if (normalized.length === 0 && skipped > 0) {
      throw new Error('歌单标识无效（缺少 global_collection_id）');
    }
    playlists.value = normalized;
    playlistError.value = '';

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
    if (generation !== playlistLoadGeneration) return;
    console.error('Failed to load user playlists', e);
    playlistError.value = e instanceof Error ? e.message : '歌单加载失败';
  } finally {
    if (generation === playlistLoadGeneration) {
      playlistsLoading.value = false;
    }
  }
}

watch(
  () => [userStore.isLoggedIn, userStore.userId, userStore.deviceReady] as const,
  ([isLoggedIn, , deviceReady]) => {
  if (isLoggedIn && deviceReady) {
    loadUserPlaylists();
  } else if (!isLoggedIn) {
    playlistLoadGeneration += 1;
    playlists.value = [];
    playlistsLoading.value = false;
    playlistError.value = '';
  }
}, { immediate: true });

function handleNav(viewId: string) {
  emit('navigate', viewId);
}

function handlePlaylist(playlist: { id: string; name: string }) {
  emit('navigate', 'playlist', { id: playlist.id, name: playlist.name, source: 'user' });
}
</script>

<template>
  <aside
    class="sidebar"
    data-test="sidebar-chrome"
    :data-skin-chrome="skinId"
  >
    <!-- Logo Section -->
    <div class="masthead">
      <div class="sidebar-wordmark">
        <span class="sidebar-brand" data-test="sidebar-brand">BottleMusic</span>
        <span class="sidebar-skin-label" data-test="sidebar-skin-label">
          {{ skinId === 'aurora' ? '极光 Aurora' : '报刊 Newsprint' }}
        </span>
      </div>
      <span
        v-if="skinId === 'aurora'"
        class="aurora-nav-label"
        data-test="aurora-nav-label"
      >导航</span>
    </div>

    <!-- 检查更新（常驻入口；启动静默 check() 检测到新版本时自动高亮，点击去设置页检查/安装） -->
    <a
      class="update-entry"
      @click="handleNav('settings')"
      :title="updateAvailable ? `发现新版本 v${updateVersion}，点击前往安装` : '点击前往设置检查更新'"
      :style="updateAvailable
        ? 'display:flex; align-items:center; gap:6px; margin:0 0 10px; padding:4px 8px; font-size:11px; color:var(--paper); background:var(--accent); border-radius:4px; cursor:pointer; width:fit-content;'
        : 'display:flex; align-items:center; gap:6px; margin:0 0 10px; padding:4px 8px; font-size:11px; color:var(--ink-soft); border:1px solid var(--rule); border-radius:4px; cursor:pointer; width:fit-content;'"
    >
      <span :style="updateAvailable
        ? 'width:6px; height:6px; border-radius:50%; background:var(--paper);'
        : 'width:6px; height:6px; border-radius:50%; background:var(--ink-mute);'"></span>
      {{ updateAvailable ? `有新版本 v${updateVersion}` : '检查更新' }}
    </a>

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
        data-test="sidebar-nav-item"
        :class="{ active: activeView === item.id }"
        @click="handleNav(item.id)"
      >
        <span
          v-if="skinId === 'aurora' && activeView === item.id"
          class="nav-active-pill"
          data-test="sidebar-nav-active-pill"
          aria-hidden="true"
        />
        <svg class="ico" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5">
          <path :d="item.icon" />
        </svg>
        {{ item.name }}
      </a>
    </nav>

    <!-- Playlists section -->
    <div class="section-label">我的歌单</div>
    <nav class="playlists">
      <template v-if="userStore.isLoggedIn">
        <template v-if="playlists.length > 0">
          <a
            v-for="pl in playlists"
            :key="pl.id"
            data-test="sidebar-user-playlist"
            @click="handlePlaylist(pl)"
          >
            <span class="dot"></span>
            {{ pl.name }}
          </a>
        </template>
        <div
          v-if="playlistsLoading && playlists.length === 0"
          class="playlist-placeholder"
        >
          正在读取歌单…
        </div>
        <button
          v-else-if="playlistError"
          type="button"
          class="playlist-placeholder playlist-retry"
          data-test="playlist-retry"
          :title="playlistError"
          @click="loadUserPlaylists"
        >
          歌单加载失败 · 重试
        </button>
        <div v-else-if="playlists.length === 0" class="playlist-placeholder">暂无歌单</div>
      </template>
      <div v-else class="playlist-placeholder" @click="handleNav('login')" style="cursor: pointer;">
        扫码登录后查看歌单
      </div>
    </nav>

    <!-- Footer Stamp (newsprint editorial chrome only) -->
    <div
      v-if="skinId === 'newsprint'"
      class="sidebar-footer"
      data-test="newsprint-stamp"
    >
      <div class="stamp">印</div>
      <div>每日刊印<br/>始于 2026</div>
    </div>
  </aside>
</template>
