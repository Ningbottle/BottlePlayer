import { computed, watch, onMounted, onScopeDispose, nextTick, type ComputedRef } from 'vue';
import { playerStore, seek as storeSeek } from '../../playback/index';
import { apiGet } from '../../platform/tauri/nativeClient';
import { useLyricFollow } from '../../api/useLyricFollow';
import { lyricFullscreen, setLyricFullscreen } from '../../api/lyricFullscreen';
import type { Track } from '../../shared/music/track';
import { LyricsResource, type LyricLine } from '../../api/lyricsResource';

export type { LyricLine } from '../../api/lyricsResource';

export interface LyricStageModel {
  loading: boolean;
  parsedLyrics: LyricLine[];
  activeIndex: number;
  currentTrack: Track | null;
  coverUrl: string;
  autoFollowing: boolean;
  fullscreen: boolean;
  isPlaying: boolean;
  currentTime: number;
  duration: number;
  error: Error | null;
}

export interface LyricStageCommands {
  onUserScroll: () => void;
  resumeFollow: () => void;
  enterFullscreen: () => void;
  exitFullscreen: () => void;
  /** Seek playback to a lyric line timestamp (seconds) and resume follow. */
  seekToLine: (timeSeconds: number) => void;
  retryLyrics: () => Promise<void>;
}

export interface UseLyricStageReturn {
  model: ComputedRef<LyricStageModel>;
  commands: LyricStageCommands;
}

/**
 * Parse LRC (and light enhanced LRC). Exported for unit tests.
 * - Decodes base64 payloads when the body has no `[mm:ss]` tags
 * - Strips word-level tags like `<00:01.20>`
 * - Accepts `[mm:ss.xx]` / `[mm:ss:xx]` fractions
 */
export function parseLrc(raw: string): LyricLine[] {
  let lrcText = (raw || '').replace(/^\uFEFF/, '').replace(/\r\n/g, '\n').replace(/\r/g, '\n');

  // Some providers return base64 LRC without brackets until decoded.
  if (lrcText && !/\[\d+:\d+/.test(lrcText)) {
    const compact = lrcText.replace(/\s+/g, '');
    if (/^[A-Za-z0-9+/]+=*$/.test(compact) && compact.length > 32) {
      try {
        const decoded = typeof atob === 'function'
          ? atob(compact)
          : Buffer.from(compact, 'base64').toString('utf8');
        if (/\[\d+:\d+/.test(decoded)) lrcText = decoded;
      } catch {
        /* keep original */
      }
    }
  }

  const lines = lrcText.split('\n');
  const result: LyricLine[] = [];
  // [mm:ss.xx] or [mm:ss:xx] or [mm:ss]
  const tagRe = /\[(\d{1,3}):(\d{1,2})(?:[.:](\d{1,3}))?\]/g;

  for (const line of lines) {
    const timeMatches = [...line.matchAll(tagRe)];
    if (timeMatches.length === 0) continue;

    let text = line
      .replace(tagRe, '')
      // Enhanced LRC word timing: <mm:ss.xx>
      .replace(/<\d{1,3}:\d{1,2}(?:[.:]\d{1,3})?>/g, '')
      .replace(/\{[^}]*\}/g, '')
      .trim();
    // Drop pure-metadata residual like empty or "///"
    if (!text || /^[/\-–—·.\s]+$/.test(text)) continue;

    for (const match of timeMatches) {
      const min = parseInt(match[1], 10);
      const sec = parseInt(match[2], 10);
      const fracRaw = match[3] || '0';
      // Interpret fraction as milliseconds padded to 3 digits (LRC convention).
      const frac = parseInt(fracRaw.padEnd(3, '0').slice(0, 3), 10) / 1000;
      const time = min * 60 + sec + frac;
      if (!Number.isFinite(time)) continue;
      result.push({ time, text });
    }
  }

  return result.sort((a, b) => a.time - b.time);
}

export async function fetchLyrics(track: Track): Promise<LyricLine[]> {
  const searchRes = await apiGet<{ status: number; candidates?: { id: string; accesskey: string }[] }>('/search/lyric', {
    hash: track.FileHash,
  });
  if (searchRes.status !== 1 && searchRes.status !== 200) {
    throw new Error('Unable to search lyrics');
  }

  const candidate = searchRes.candidates?.[0];
  if (!candidate) return [];

  const detailRes = await apiGet<{ status: number; lyric?: string }>('/lyric', {
    id: candidate.id,
    accesskey: candidate.accesskey,
  });
  if (detailRes.status !== 1 && detailRes.status !== 200) {
    throw new Error('Unable to load lyrics');
  }

  return detailRes.lyric ? parseLrc(detailRes.lyric) : [];
}

export function useLyricStage(): UseLyricStageReturn {
  const lyricsResource = new LyricsResource(fetchLyrics);
  const parsedLyrics = computed(() => lyricsResource.state.lines);

  const currentTrack = computed(() => playerStore.currentTrack);
  const currentTime = computed(() => playerStore.currentTime);
  const coverUrl = computed(() => currentTrack.value?.Image || '');

  const activeIndex = computed(() => {
    if (parsedLyrics.value.length === 0) return -1;
    const current = currentTime.value;
    const idx = parsedLyrics.value.findIndex(l => l.time > current);
    if (idx === -1) return parsedLyrics.value.length - 1;
    return Math.max(0, idx - 1);
  });

  let mounted = false;
  let disposed = false;
  let scrollToken = 0;
  let followGeneration = 0;
  let enterFollowTimer: number | null = null;

  function clearEnterFollowTimer(): void {
    if (enterFollowTimer !== null) {
      globalThis.clearTimeout(enterFollowTimer);
      enterFollowTimer = null;
    }
  }

  function scrollToLine(idx: number, behavior: ScrollBehavior = 'smooth'): void {
    scrollToken++;
    const myToken = scrollToken;
    const ownerGeneration = followGeneration;
    // nextTick: wait for lyric list paint (test-friendly; no rAF dependency)
    void nextTick(() => {
      if (!mounted || ownerGeneration !== followGeneration || myToken !== scrollToken) return;
      const el = document.getElementById(`lyric-line-${idx}`);
      if (!el) return;

      // Prefer scrolling only the lyric list — scrollIntoView(block:center) on the
      // last lines can drag the whole page up and expose layered shell chrome.
      const container =
        typeof (el as HTMLElement).closest === 'function'
          ? ((el as HTMLElement).closest('.lyric-scroll') as HTMLElement | null)
          : null;
      if (
        container
        && typeof container.getBoundingClientRect === 'function'
        && typeof container.scrollTo === 'function'
      ) {
        const elRect = el.getBoundingClientRect();
        const cRect = container.getBoundingClientRect();
        const elMid =
          container.scrollTop + (elRect.top - cRect.top) + elRect.height / 2;
        const target = elMid - cRect.height / 2;
        const maxScroll = Math.max(0, container.scrollHeight - container.clientHeight);
        const top = Math.max(0, Math.min(maxScroll, target));
        container.scrollTo({ top, behavior });
        return;
      }

      // Fallback for tests / odd DOM: nearest avoids outer page crawl.
      if (typeof (el as HTMLElement).scrollIntoView === 'function') {
        (el as HTMLElement).scrollIntoView({ behavior, block: 'nearest' });
      }
    });
  }

  const { autoFollowing, onUserScroll, resumeFollow, snapToActive, resetForTrack } = useLyricFollow({
    activeIndex,
    scrollToLine,
  });

  /** After current lyrics land, snap to playhead quickly (<1s total, including layout). */
  function scheduleEnterFollow(generation: number): void {
    if (!mounted || generation !== followGeneration) return;
    clearEnterFollowTimer();
    // Instant first snap so user sees the current line immediately
    snapToActive('auto');
    // Second snap after enter animation settles (~0.5s)
    enterFollowTimer = window.setTimeout(() => {
      enterFollowTimer = null;
      if (mounted && generation === followGeneration && autoFollowing.value) {
        snapToActive('smooth');
      }
    }, 480);
  }

  watch(
    () => currentTrack.value?.FileHash ?? null,
    () => {
      const followSession = ++followGeneration;
      clearEnterFollowTimer();
      resetForTrack(currentTrack.value?.FileHash ?? '');
      void lyricsResource.load(currentTrack.value).then(() => {
        if (
          !mounted ||
          followSession !== followGeneration ||
          lyricsResource.state.loading ||
          lyricsResource.state.error ||
          lyricsResource.state.lines.length === 0
        ) return;
        scheduleEnterFollow(followSession);
      });
    },
    { immediate: true },
  );

  function onKeydown(e: KeyboardEvent): void {
    if (e.key === 'Escape' && lyricFullscreen.value) setLyricFullscreen(false);
  }

  onMounted(() => {
    if (disposed) return;
    mounted = true;
    window.addEventListener('keydown', onKeydown);
  });

  function disposeStage(): void {
    if (disposed) return;
    disposed = true;
    mounted = false;
    followGeneration++;
    scrollToken++;
    clearEnterFollowTimer();
    lyricsResource.dispose();
    window.removeEventListener('keydown', onKeydown);
    if (lyricFullscreen.value) setLyricFullscreen(false);
  }

  onScopeDispose(disposeStage);

  const model = computed<LyricStageModel>(() => ({
    loading: lyricsResource.state.loading,
    parsedLyrics: parsedLyrics.value,
    activeIndex: activeIndex.value,
    currentTrack: currentTrack.value,
    coverUrl: coverUrl.value,
    autoFollowing: autoFollowing.value,
    fullscreen: lyricFullscreen.value,
    isPlaying: playerStore.isPlaying,
    currentTime: currentTime.value,
    duration: playerStore.duration,
    error: lyricsResource.state.error,
  }));

  /**
   * Click-a-line: jump playhead to that line’s LRC time via the real store seek path,
   * then snap follow so the stage tracks the new line.
   */
  function seekToLine(timeSeconds: number): void {
    if (!Number.isFinite(timeSeconds) || timeSeconds < 0) return;
    followGeneration++;
    clearEnterFollowTimer();
    void storeSeek(timeSeconds);
    snapToActive('smooth');
  }

  async function retryLyrics(): Promise<void> {
    const followSession = ++followGeneration;
    clearEnterFollowTimer();
    await lyricsResource.retry();
    if (
      !mounted ||
      followSession !== followGeneration ||
      lyricsResource.state.loading ||
      lyricsResource.state.error ||
      lyricsResource.state.lines.length === 0
    ) return;
    scheduleEnterFollow(followSession);
  }

  const commands: LyricStageCommands = {
    onUserScroll: () => {
      followGeneration++;
      clearEnterFollowTimer();
      onUserScroll();
    },
    resumeFollow: () => {
      followGeneration++;
      clearEnterFollowTimer();
      resumeFollow();
    },
    enterFullscreen: () => setLyricFullscreen(true),
    exitFullscreen: () => setLyricFullscreen(false),
    seekToLine,
    retryLyrics,
  };

  return { model, commands };
}
