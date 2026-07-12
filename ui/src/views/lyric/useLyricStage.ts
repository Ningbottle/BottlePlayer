import { ref, computed, watch, onMounted, onUnmounted, nextTick, type ComputedRef } from 'vue';
import { playerStore, seek as storeSeek } from '../../api/playerStore';
import { apiGet } from '../../api/backend';
import { useLyricFollow } from '../../api/useLyricFollow';
import { lyricFullscreen, setLyricFullscreen } from '../../api/lyricFullscreen';
import type { Track } from '../../api/normalizer';

export interface LyricLine {
  time: number;
  text: string;
}

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
}

export interface LyricStageCommands {
  onUserScroll: () => void;
  resumeFollow: () => void;
  enterFullscreen: () => void;
  exitFullscreen: () => void;
  /** Seek playback to a lyric line timestamp (seconds) and resume follow. */
  seekToLine: (timeSeconds: number) => void;
}

export interface UseLyricStageReturn {
  model: ComputedRef<LyricStageModel>;
  commands: LyricStageCommands;
}

const FALLBACK_BIG_COVER =
  'data:image/svg+xml;utf8,' + encodeURIComponent(
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200">` +
    `<rect width="200" height="200" fill="#2a2520"/>` +
    `<text x="100" y="115" text-anchor="middle" font-family="Noto Serif SC,serif" ` +
    `font-weight="700" font-size="36" fill="#f1ead8">听</text></svg>`
  );

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

export function useLyricStage(): UseLyricStageReturn {
  const loading = ref(false);
  const parsedLyrics = ref<LyricLine[]>([]);

  const currentTrack = computed(() => playerStore.currentTrack);
  const currentTime = computed(() => playerStore.currentTime);
  const coverUrl = computed(() => currentTrack.value?.Image || FALLBACK_BIG_COVER);

  const activeIndex = computed(() => {
    if (parsedLyrics.value.length === 0) return -1;
    const current = currentTime.value;
    const idx = parsedLyrics.value.findIndex(l => l.time > current);
    if (idx === -1) return parsedLyrics.value.length - 1;
    return Math.max(0, idx - 1);
  });

  let scrollToken = 0;
  function scrollToLine(idx: number, behavior: ScrollBehavior = 'smooth'): void {
    scrollToken++;
    const myToken = scrollToken;
    // nextTick: wait for lyric list paint (test-friendly; no rAF dependency)
    void nextTick(() => {
      const el = document.getElementById(`lyric-line-${idx}`);
      if (el && myToken === scrollToken) {
        el.scrollIntoView({ behavior, block: 'center' });
      }
    });
  }

  const { autoFollowing, onUserScroll, resumeFollow, snapToActive, resetForTrack } = useLyricFollow({
    activeIndex,
    scrollToLine,
  });

  /** After lyrics land, snap to playhead quickly (<1s total, including layout). */
  function scheduleEnterFollow(): void {
    // Instant first snap so user sees the current line immediately
    snapToActive('auto');
    // Second snap after enter animation settles (~0.5s)
    window.setTimeout(() => {
      if (autoFollowing.value) snapToActive('smooth');
    }, 480);
  }

  async function loadLyrics(): Promise<void> {
    if (!currentTrack.value) {
      parsedLyrics.value = [];
      return;
    }
    loading.value = true;
    parsedLyrics.value = [];
    try {
      const searchRes = await apiGet<{ status: number; candidates?: { id: string; accesskey: string }[] }>('/search/lyric', {
        hash: currentTrack.value.FileHash,
      });
      if ((searchRes.status === 1 || searchRes.status === 200) && searchRes.candidates && searchRes.candidates.length > 0) {
        const candidate = searchRes.candidates[0];
        const detailRes = await apiGet<{ status: number; lyric?: string }>('/lyric', {
          id: candidate.id,
          accesskey: candidate.accesskey,
        });
        if ((detailRes.status === 1 || detailRes.status === 200) && detailRes.lyric) {
          parsedLyrics.value = parseLrc(detailRes.lyric);
        } else {
          parsedLyrics.value = [{ time: 0, text: '无法加载歌词文本' }];
        }
      } else {
        parsedLyrics.value = [{ time: 0, text: '暂无歌词' }];
      }
    } catch (e) {
      console.error('Lyric fetch failed', e);
      parsedLyrics.value = [{ time: 0, text: '歌词加载出错' }];
    } finally {
      loading.value = false;
      // Snap to current line as soon as lines exist
      if (parsedLyrics.value.length > 0) {
        scheduleEnterFollow();
      }
    }
  }

  watch(currentTrack, (track) => {
    resetForTrack(track?.FileHash || '');
    loadLyrics();
  }, { deep: true });

  function onKeydown(e: KeyboardEvent): void {
    if (e.key === 'Escape' && lyricFullscreen.value) setLyricFullscreen(false);
  }

  onMounted(() => {
    loadLyrics();
    window.addEventListener('keydown', onKeydown);
  });

  onUnmounted(() => {
    window.removeEventListener('keydown', onKeydown);
    if (lyricFullscreen.value) setLyricFullscreen(false);
  });

  const model = computed<LyricStageModel>(() => ({
    loading: loading.value,
    parsedLyrics: parsedLyrics.value,
    activeIndex: activeIndex.value,
    currentTrack: currentTrack.value,
    coverUrl: coverUrl.value,
    autoFollowing: autoFollowing.value,
    fullscreen: lyricFullscreen.value,
    isPlaying: playerStore.isPlaying,
    currentTime: currentTime.value,
    duration: playerStore.duration,
  }));

  /**
   * Click-a-line: jump playhead to that line’s LRC time via the real store seek path,
   * then snap follow so the stage tracks the new line.
   */
  function seekToLine(timeSeconds: number): void {
    if (!Number.isFinite(timeSeconds) || timeSeconds < 0) return;
    void storeSeek(timeSeconds);
    snapToActive('smooth');
  }

  const commands: LyricStageCommands = {
    onUserScroll,
    resumeFollow,
    enterFullscreen: () => setLyricFullscreen(true),
    exitFullscreen: () => setLyricFullscreen(false),
    seekToLine,
  };

  return { model, commands };
}
