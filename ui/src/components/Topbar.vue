<script setup lang="ts">
import { ref, watch, computed } from 'vue';
import { userStore } from '../api/userStore';
import { useThemeStore } from '../api/themeStore';

const props = defineProps<{
  searchQuery: string;
}>();

const emit = defineEmits<{
  (e: 'update:searchQuery', val: string): void;
  (e: 'search', query: string): void;
  (e: 'toggle-tweaks'): void;
  (e: 'navigate', view: string): void;
  (e: 'back'): void;
  (e: 'forward'): void;
}>();

const themeStore = useThemeStore();
const skinId = themeStore.skinId;
const searchVariant = computed(() =>
  skinId.value === 'newsprint' ? 'legacy' : 'command',
);

const localQuery = ref(props.searchQuery);

watch(
  () => props.searchQuery,
  (value) => {
    localQuery.value = value;
  },
  { immediate: true }
);

function triggerSearch() {
  emit('update:searchQuery', localQuery.value);
  emit('search', localQuery.value);
}

function handleSearchInput(e: Event) {
  const val = (e.target as HTMLInputElement).value;
  localQuery.value = val;
  emit('update:searchQuery', val);
}

function goBack() {
  emit('back');
}

function goForward() {
  emit('forward');
}

function shareAlert() {
  window.alert('分享功能在桌面端已复制链接！');
}
</script>

<template>
  <header
    class="topbar"
    data-test="topbar-chrome"
    :data-skin-chrome="skinId"
  >
    <!-- Back/Forward controls -->
    <div class="nav-arrows">
      <button class="icon-btn" aria-label="后退" @click="goBack">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <polyline points="15 18 9 12 15 6"/>
        </svg>
      </button>
      <button class="icon-btn" aria-label="前进" @click="goForward">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <polyline points="9 18 15 12 9 6"/>
        </svg>
      </button>
    </div>

    <!-- Search Input -->
    <div
      class="search"
      data-test="topbar-search"
      :data-variant="searchVariant"
    >
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
        <circle cx="11" cy="11" r="7"/>
        <path d="m20 20-3.5-3.5"/>
      </svg>
      <input
        :value="searchQuery"
        @input="handleSearchInput"
        @keyup.enter="triggerSearch"
        :placeholder="skinId === 'newsprint'
          ? '搜索歌曲、艺人、专辑、歌单'
          : '搜索歌曲、艺人、专辑、歌单'"
      />
    </div>

    <!-- Free Badge -->
    <button class="free-badge" @click="emit('navigate', 'login')" style="cursor: pointer;">
      <span class="seal" :style="userStore.isVip ? { backgroundColor: 'var(--accent)', color: 'var(--paper)' } : {}">
        {{ userStore.isVip ? 'V' : (userStore.isLoggedIn ? '享' : '登') }}
      </span>
      {{ userStore.isVip ? 'VIP 畅听中' : (userStore.isLoggedIn ? '普通会员' : '扫码登录') }}
    </button>

    <!-- Right Action Buttons -->
    <div class="top-actions">
      <button class="icon-btn" aria-label="分享" @click="shareAlert">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
          <circle cx="6" cy="12" r="2.5"/>
          <circle cx="18" cy="6" r="2.5"/>
          <circle cx="18" cy="18" r="2.5"/>
          <path d="M8 11l8-4M8 13l8 4"/>
        </svg>
      </button>
      <button class="icon-btn" aria-label="设置" @click="emit('navigate', 'settings')">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
          <circle cx="12" cy="12" r="3"/>
          <path d="M19.4 15a1.7 1.7 0 0 0 .34 1.87l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.7 1.7 0 0 0-1.87-.34 1.7 1.7 0 0 0-1 1.55V21a2 2 0 1 1-4 0v-.08a1.7 1.7 0 0 0-1.11-1.55 1.7 1.7 0 0 0-1.87.34l-.06.06A2 2 0 1 1 4.18 16.9l.06-.06A1.7 1.7 0 0 0 4.58 15 1.7 1.7 0 0 0 3 14H3a2 2 0 1 1 0-4h.09a1.7 1.7 0 0 0 1.55-1.11 1.7 1.7 0 0 0-.34-1.87l-.06-.06A2 2 0 1 1 7.1 4.18l.06.06A1.7 1.7 0 0 0 9 4.58 1.7 1.7 0 0 0 10 3V3a2 2 0 1 1 4 0v.09a1.7 1.7 0 0 0 1 1.55 1.7 1.7 0 0 0 1.87-.34l.06-.06A2 2 0 1 1 19.82 7.1l-.06.06a1.7 1.7 0 0 0-.34 1.87V9c.27.65.84 1.13 1.55 1.11H21a2 2 0 1 1 0 4h-.09a1.7 1.7 0 0 0-1.55 1.11z"/>
        </svg>
      </button>
      <button class="icon-btn" aria-label="调整" @click="emit('toggle-tweaks')">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6">
          <path d="M4 6h16M4 12h10M4 18h16"/>
        </svg>
      </button>
    </div>
  </header>
</template>
