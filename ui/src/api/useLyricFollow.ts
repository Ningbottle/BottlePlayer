import { ref, watch, type Ref } from 'vue';

export interface UseLyricFollowOptions {
  activeIndex: Ref<number>;
  scrollToLine: (idx: number) => void;
  now?: () => number;
}

export interface UseLyricFollowReturn {
  autoFollowing: Ref<boolean>;
  manualScrollUntil: Ref<number>;
  trackKey: Ref<string>;
  onUserScroll: () => void;
  resumeFollow: () => void;
  resetForTrack: (key: string) => void;
}

const IDLE_RESUME_MS = 3000;

export function useLyricFollow(opts: UseLyricFollowOptions): UseLyricFollowReturn {
  const autoFollowing = ref(true);
  const manualScrollUntil = ref(0);
  const trackKey = ref('');
  const now = opts.now ?? Date.now;
  let idleTimer: ReturnType<typeof setTimeout> | null = null;

  watch(opts.activeIndex, (idx) => {
    if (autoFollowing.value && idx >= 0) {
      opts.scrollToLine(idx);
    }
  });

  function onUserScroll() {
    autoFollowing.value = false;
    manualScrollUntil.value = now() + IDLE_RESUME_MS;
    if (idleTimer) clearTimeout(idleTimer);
    idleTimer = setTimeout(() => {
      autoFollowing.value = true;
      manualScrollUntil.value = 0;
      idleTimer = null;
    }, IDLE_RESUME_MS);
  }

  return {
    autoFollowing,
    manualScrollUntil,
    trackKey,
    onUserScroll,
    resumeFollow: () => {
      if (idleTimer) {
        clearTimeout(idleTimer);
        idleTimer = null;
      }
      manualScrollUntil.value = 0;
      autoFollowing.value = true;
      const idx = opts.activeIndex.value;
      if (idx >= 0) opts.scrollToLine(idx);
    },
    resetForTrack: (key: string) => {
      if (key === trackKey.value) return; // same track, no reset
      trackKey.value = key;
      if (idleTimer) {
        clearTimeout(idleTimer);
        idleTimer = null;
      }
      manualScrollUntil.value = 0;
      autoFollowing.value = true;
    },
  };
}
