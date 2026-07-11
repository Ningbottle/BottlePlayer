import { ref, computed, watch, onMounted, onUnmounted, type ComputedRef } from 'vue';
import { playerStore } from '../../api/playerStore';
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
}

export interface LyricStageCommands {
  onUserScroll: () => void;
  resumeFollow: () => void;
  enterFullscreen: () => void;
  exitFullscreen: () => void;
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

function parseLrc(lrcText: string): LyricLine[] {
  const lines = lrcText.split('\n');
  const result: LyricLine[] = [];
  for (const line of lines) {
    const timeMatches = [...line.matchAll(/\[(\d+):(\d+)(?:\.(\d+))?\]/g)];
    if (timeMatches.length > 0) {
      const text = line.replace(/\[\d+:\d+(?:\.\d+)?\]/g, '').trim();
      if (text) {
        for (const match of timeMatches) {
          const min = parseInt(match[1], 10);
          const sec = parseInt(match[2], 10);
          const msStr = match[3] || '0';
          const ms = parseFloat('0.' + msStr);
          const time = min * 60 + sec + ms;
          result.push({ time, text });
        }
      }
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
  function scrollToLine(idx: number): void {
    scrollToken++;
    const myToken = scrollToken;
    const el = document.getElementById(`lyric-line-${idx}`);
    if (el && myToken === scrollToken) {
      el.scrollIntoView({ behavior: 'smooth', block: 'center' });
    }
  }

  const { autoFollowing, onUserScroll, resumeFollow, resetForTrack } = useLyricFollow({
    activeIndex,
    scrollToLine,
  });

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
  }));

  const commands: LyricStageCommands = {
    onUserScroll,
    resumeFollow,
    enterFullscreen: () => setLyricFullscreen(true),
    exitFullscreen: () => setLyricFullscreen(false),
  };

  return { model, commands };
}
