<script setup lang="ts">
import { ref, watch, onBeforeUnmount, nextTick, useId } from 'vue';
import { gsap } from 'gsap';
import { addTrackToPlaylist, type UserPlaylist } from './favorite';
import { getUserPlaylistsOrThrow, favoriteStore } from './favoriteStore';
import { Track } from '../../shared/music/track';
import { userStore } from '../account';
import { transitionEnter, transitionLeave } from '../../app/navigation/pageTransitions';

const props = defineProps<{
  show: boolean;
  track: Track | null;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
  (e: 'success', playlistName: string): void;
  (e: 'error', message: string): void;
}>();

const playlists = ref<UserPlaylist[]>([]);
const loading = ref(false);
const adding = ref<string | null>(null);
const dialogRef = ref<HTMLElement | null>(null);
const dialogTitleId = `add-playlist-dialog-title-${useId()}`;
let returnFocusTarget: HTMLElement | null = null;

/**
 * Every async operation in this modal is bound to the account session that
 * started it, and to a monotonically increasing "epoch" bumped whenever the
 * list load restarts (modal reopened, logged out, account switched). A result
 * that comes back after either moved is dropped: it must never render, clear a
 * busy flag, or mutate the shared favorite store.
 */
let loadEpoch = 0;
let addEpoch = 0;
let disposed = false;

function restoreModalFocus(): void {
  const target = returnFocusTarget;
  returnFocusTarget = null;
  if (target?.isConnected) target.focus();
}

watch(
  () => props.show,
  async (visible) => {
    if (!visible) {
      restoreModalFocus();
      return;
    }
    if (!returnFocusTarget && document.activeElement instanceof HTMLElement) {
      returnFocusTarget = document.activeElement;
    }
    await nextTick();
    if (!props.show || disposed) return;
    const initialTarget = dialogRef.value?.querySelector<HTMLElement>('[data-modal-initial-focus]');
    (initialTarget ?? dialogRef.value)?.focus();
  },
  { flush: 'post' },
);

// The parent can close/reopen or replace the track without calling handleClose.
// Invalidate synchronously before a pending promise can close another modal.
watch(
  [() => props.show, () => props.track, () => props.track?.FileHash,
    () => userStore.isLoggedIn, () => userStore.userId, () => favoriteStore.accountScope.epoch],
  () => { addEpoch += 1; adding.value = null; },
  { flush: 'sync' },
);

watch(
  [() => props.show, () => userStore.isLoggedIn, () => userStore.userId,
    () => favoriteStore.accountScope.epoch],
  async ([visible]) => {
    const epoch = ++loadEpoch;
    playlists.value = []; // never show a previous account's choices during a reload
    if (!visible || !userStore.isLoggedIn) {
      loading.value = false;
      return;
    }
    const uid = userStore.userId;
    const accountEpoch = favoriteStore.accountEpoch;
    loading.value = true;
    try {
      const list = await getUserPlaylistsOrThrow();
      if (disposed || epoch !== loadEpoch || !props.show || !userStore.isLoggedIn) return;
      if (userStore.userId !== uid || favoriteStore.accountEpoch !== accountEpoch) return;
      playlists.value = list;
    } catch {
      if (disposed || epoch !== loadEpoch) return;
      emit('error', '歌单列表加载失败，请稍后重试');
    } finally {
      if (!disposed && epoch === loadEpoch) loading.value = false;
    }
  },
  { flush: 'sync' },
);

onBeforeUnmount(() => {
  disposed = true;
  loadEpoch += 1;
  addEpoch += 1;
  restoreModalFocus();
});

async function handleSelect(playlist: UserPlaylist) {
  const track = props.track;
  if (!props.show || !track || adding.value || disposed) return;
  if (!userStore.isLoggedIn) {
    emit('error', '请先登录后收藏歌曲');
    return;
  }

  // Pin the session and the track *before* awaiting: the account may switch,
  // the modal may close, and the caller may point the modal at another track
  // while the add request is in flight.
  const uid = userStore.userId;
  const epoch = ++addEpoch;
  const scopeAtStart = favoriteStore.accountEpoch;
  const likedAtStart = favoriteStore.accountId;

  adding.value = playlist.id;
  let result: { success: boolean; error?: string };
  try {
    result = await addTrackToPlaylist(playlist, track);
  } catch (e: any) {
    // addTrackToPlaylist throws on transport errors (offline / circuit open);
    // surface them as a regular error toast.
    result = { success: false, error: e?.message || '收藏失败' };
  }

  if (disposed || epoch !== addEpoch || !props.show || props.track !== track) return;
  adding.value = null;

  // The request itself already reached the backend for the session that made
  // it, but nothing after that may assume we are still the same account.
  if (!userStore.isLoggedIn || userStore.userId !== uid) return;
  if (favoriteStore.accountEpoch !== scopeAtStart || favoriteStore.accountId !== likedAtStart) return;

  if (result.success) {
    // Mirror into the shared favorite store so the player-bar heart lights up
    // immediately (the adapter already performed the API add, so no duplicate
    // request) — but only when the account's authoritative liked-playlist
    // identity matches this entry. A public playlist merely named
    //「我喜欢的音乐」must not light our heart, and an unverified identity
    // (offline) leaves the heart as-is rather than guessing.
    const ownership = await favoriteStore.resolveLikedOwnership(playlist.id);
    if (disposed || epoch !== addEpoch || !props.show || props.track !== track) return;
    if (!userStore.isLoggedIn || userStore.userId !== uid) return;
    if (ownership.owned && ownership.uid === uid && ownership.epoch === favoriteStore.accountEpoch) {
      // The track that was added — not "whatever the modal points at now".
      favoriteStore.markFavoriteTrack(track);
    }
    emit('success', playlist.name);
    emit('close');
  } else {
    emit('error', result.error || '收藏失败');
  }
}

function handleClose() {
  addEpoch += 1; // retire any in-flight add before its result can land
  emit('close');
}

function onDialogKeydown(event: KeyboardEvent): void {
  if (event.key === 'Escape') {
    event.preventDefault();
    event.stopPropagation();
    handleClose();
    return;
  }
  if (event.key !== 'Tab') return;

  const dialog = dialogRef.value;
  if (!dialog) return;
  const focusable = Array.from(dialog.querySelectorAll<HTMLElement>(
    'button:not(:disabled), [href], input:not(:disabled), select:not(:disabled), textarea:not(:disabled), [tabindex]:not([tabindex="-1"])',
  ));
  if (focusable.length === 0) {
    event.preventDefault();
    dialog.focus();
    return;
  }

  const first = focusable[0];
  const last = focusable[focusable.length - 1];
  const active = document.activeElement;
  if (event.shiftKey && (active === first || !dialog.contains(active))) {
    event.preventDefault();
    last.focus();
  } else if (!event.shiftKey && (active === last || !dialog.contains(active))) {
    event.preventDefault();
    first.focus();
  }
}

function onEnter(el: Element, done: () => void) {
  transitionEnter(el, done);
  const modal = (el as HTMLElement).querySelector('.playlist-modal');
  if (modal) {
    gsap.fromTo(modal, { scale: 0.96, y: 8 }, { scale: 1, y: 0, duration: 0.25, ease: 'power2.out', onComplete: done });
  }
}

function onLeave(el: Element, done: () => void) {
  transitionLeave(el, done);
}
</script>

<template>
  <Teleport to="body">
    <Transition :css="false" appear @enter="onEnter" @leave="onLeave">
      <div v-if="show" class="modal-overlay" @click.self="handleClose">
        <div
          ref="dialogRef"
          class="playlist-modal"
          role="dialog"
          aria-modal="true"
          :aria-labelledby="dialogTitleId"
          tabindex="-1"
          @keydown="onDialogKeydown"
        >
          <div class="modal-header">
            <h3 :id="dialogTitleId">收藏到歌单</h3>
            <button type="button" class="close-btn" aria-label="关闭收藏弹窗" data-modal-initial-focus @click="handleClose">×</button>
          </div>

          <div class="modal-body">
            <div v-if="!userStore.isLoggedIn" class="empty-hint">
              请先登录后收藏歌曲
            </div>
            <div v-else-if="loading" class="empty-hint">
              加载歌单中…
            </div>
            <div v-else-if="playlists.length === 0" class="empty-hint">
              暂无歌单，请先创建歌单
            </div>
            <div v-else class="playlist-list">
              <button
                v-for="pl in playlists"
                :key="pl.id"
                type="button"
                class="playlist-item"
                :class="{ disabled: adding !== null }"
                :disabled="adding !== null"
                :aria-busy="adding === pl.id ? 'true' : undefined"
                :aria-label="`${pl.name}，${pl.songcount || 0} 首`"
                @click="handleSelect(pl)"
              >
                <span class="pl-icon" aria-hidden="true">♫</span>
                <span class="pl-info">
                  <span class="pl-name">{{ pl.name }}</span>
                  <span class="pl-count">{{ pl.songcount || 0 }} 首</span>
                </span>
                <span v-if="adding === pl.id" class="pl-adding">添加中…</span>
              </button>
            </div>
          </div>
      </div>
    </div>
    </Transition>
  </Teleport>
</template>

<style scoped>
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.playlist-modal {
  background: var(--paper, #f1ead8);
  border-radius: 12px;
  width: 360px;
  max-height: 480px;
  display: flex;
  flex-direction: column;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.2);
}

.modal-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 20px;
  border-bottom: 1px solid var(--ink-light, #c4b99a);
}

.modal-header h3 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
}

.close-btn {
  background: none;
  border: none;
  font-size: 24px;
  cursor: pointer;
  color: var(--ink-mute, #8a7e6a);
  padding: 0;
  line-height: 1;
}

.close-btn:hover {
  color: var(--ink, #221b12);
}

.modal-body {
  overflow-y: auto;
  padding: 12px 0;
}

.empty-hint {
  padding: 32px 20px;
  text-align: center;
  color: var(--ink-mute, #8a7e6a);
  font-style: italic;
}

.playlist-list {
  display: flex;
  flex-direction: column;
}

.playlist-item {
  appearance: none;
  display: flex;
  align-items: center;
  gap: 12px;
  width: 100%;
  padding: 12px 20px;
  border: 0;
  background: transparent;
  color: inherit;
  font: inherit;
  text-align: left;
  cursor: pointer;
  transition: background 0.15s;
}
.playlist-item:focus-visible {
  outline: 2px solid var(--accent, #a8311b);
  outline-offset: -2px;
}

.playlist-item:hover {
  background: rgba(0, 0, 0, 0.05);
}

.playlist-item.disabled {
  opacity: 0.6;
  cursor: wait;
}

.pl-icon {
  width: 40px;
  height: 40px;
  background: var(--accent, #a8311b);
  color: var(--paper, #f1ead8);
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
}

.pl-info {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}

.pl-name {
  display: block;
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.pl-count {
  font-size: 12px;
  color: var(--ink-mute, #8a7e6a);
  margin-top: 2px;
}

.pl-adding {
  font-size: 12px;
  color: var(--accent, #a8311b);
  font-style: italic;
}
</style>
