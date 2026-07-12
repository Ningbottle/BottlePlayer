import { ref, watch, onScopeDispose, type Ref } from 'vue';

export interface UseLyricFollowOptions {
  activeIndex: Ref<number>;
  /** behavior: 'auto' for instant snap on enter; 'smooth' while following */
  scrollToLine: (idx: number, behavior?: ScrollBehavior) => void;
  now?: () => number;
}

export interface UseLyricFollowReturn {
  autoFollowing: Ref<boolean>;
  manualScrollUntil: Ref<number>;
  trackKey: Ref<string>;
  onUserScroll: () => void;
  resumeFollow: () => void;
  /** Force follow + scroll to active line (enter lyric page / lyrics loaded). */
  snapToActive: (behavior?: ScrollBehavior) => void;
  resetForTrack: (key: string) => void;
}

/** After user scrolls, resume follow in under 1s so the playhead is never “lost”. */
export const IDLE_RESUME_MS = 900;

export function useLyricFollow(opts: UseLyricFollowOptions): UseLyricFollowReturn {
  const autoFollowing = ref(true);
  const manualScrollUntil = ref(0);
  const trackKey = ref('');
  const now = opts.now ?? Date.now;
  let idleTimer: ReturnType<typeof setTimeout> | null = null;

  function clearIdleTimer(): void {
    if (idleTimer) {
      clearTimeout(idleTimer);
      idleTimer = null;
    }
  }

  function scrollActive(behavior: ScrollBehavior = 'smooth'): void {
    const idx = opts.activeIndex.value;
    if (idx >= 0) opts.scrollToLine(idx, behavior);
  }

  // immediate: snap when stage mounts with an already-valid active line
  watch(
    opts.activeIndex,
    (idx) => {
      if (autoFollowing.value && idx >= 0) {
        opts.scrollToLine(idx, 'smooth');
      }
    },
    { immediate: true },
  );

  onScopeDispose(() => {
    clearIdleTimer();
  });

  function onUserScroll() {
    autoFollowing.value = false;
    manualScrollUntil.value = now() + IDLE_RESUME_MS;
    clearIdleTimer();
    idleTimer = setTimeout(() => {
      autoFollowing.value = true;
      manualScrollUntil.value = 0;
      idleTimer = null;
      scrollActive('smooth');
    }, IDLE_RESUME_MS);
  }

  function resumeFollow() {
    clearIdleTimer();
    manualScrollUntil.value = 0;
    autoFollowing.value = true;
    scrollActive('smooth');
  }

  function snapToActive(behavior: ScrollBehavior = 'auto') {
    clearIdleTimer();
    manualScrollUntil.value = 0;
    autoFollowing.value = true;
    scrollActive(behavior);
  }

  return {
    autoFollowing,
    manualScrollUntil,
    trackKey,
    onUserScroll,
    resumeFollow,
    snapToActive,
    resetForTrack: (key: string) => {
      if (key === trackKey.value) return;
      trackKey.value = key;
      clearIdleTimer();
      manualScrollUntil.value = 0;
      autoFollowing.value = true;
    },
  };
}
