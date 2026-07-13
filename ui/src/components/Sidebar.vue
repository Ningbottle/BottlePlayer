<script setup lang="ts">
import { ref, watch, onMounted } from 'vue';
import { check } from '@tauri-apps/plugin-updater';
import { apiGet } from '../api/backend';
import { userStore } from '../api/userStore';
import { normalizePlaylists, UserPlaylist } from '../api/favorite';
import { useSkippedVersion, getSkippedVersion } from '../api/skippedVersion';
import { useThemeStore } from '../api/themeStore';

defineProps<{
  activeView: string;
}>();

const emit = defineEmits<{
  (e: 'navigate', view: string, params?: any): void;
}>();

const themeStore = useThemeStore();
const skinId = themeStore.skinId;

const sidebarNav = [
  { id: 'home', name: '首页', icon: 'M3 11l9-8 9 8v10a2 2 0 0 1-2 2h-4v-7h-6v7H5a2 2 0 0 1-2-2V11z' },
  { id: 'stats', name: '统计', icon: 'M3 3v18h18M7 14l4-4 4 4 5-5' },
  { id: 'history', name: '最近播放', icon: 'M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z' },
  { id: 'equalizer', name: '均衡器', icon: 'M4 21v-7M4 10V3m8 18v-9m0-4V3m8 18v-5m0-4V3M2 14h4m4-6h4m4 5h4' },
];

type SidebarPlaylist = Pick<UserPlaylist, 'id' | 'name'>;

const playlists = ref<SidebarPlaylist[]>([]);

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
    const update = await check();
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
  <aside
    class="sidebar"
    data-test="sidebar-chrome"
    :data-skin-chrome="skinId"
  >
    <!-- Logo Section -->
    <div class="masthead">
      <span v-if="skinId === 'newsprint'" class="logo"><i>The</i> Player</span>
      <span
        v-else
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

    <!-- Footer Stamp (newsprint editorial chrome only) -->
    <div
      v-if="skinId === 'newsprint'"
      class="sidebar-footer"
      data-test="newsprint-stamp"
    >
      <div class="stamp">印</div>
      <div>Printed daily<br/>since 2026</div>
    </div>
  </aside>
</template>
