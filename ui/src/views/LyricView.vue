<script setup lang="ts">
import { ref, computed, watch, onMounted } from 'vue';
import { playerStore } from '../api/playerStore';
import { apiGet } from '../api/backend';

interface LyricLine {
  time: number;
  text: string;
}

const loading = ref(false);
const rawLyricText = ref('');
const parsedLyrics = ref<LyricLine[]>([]);
const currentTrack = computed(() => playerStore.currentTrack);
const currentTime = computed(() => playerStore.currentTime);

// Stable cover URL with inline SVG fallback (same trick as PlayerBar) so
// switching songs doesn't flicker to a blank/SVG fallback while the cover
// URL loads from KuGou's CDN.
const FALLBACK_BIG_COVER =
  'data:image/svg+xml;utf8,' + encodeURIComponent(
    `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 200 200">` +
    `<rect width="200" height="200" fill="#2a2520"/>` +
    `<text x="100" y="115" text-anchor="middle" font-family="Noto Serif SC,serif" ` +
    `font-weight="700" font-size="36" fill="#f1ead8">听</text></svg>`
  );
const coverUrl = computed(() => currentTrack.value?.Image || FALLBACK_BIG_COVER);

async function loadLyrics() {
  if (!currentTrack.value) {
    parsedLyrics.value = [];
    return;
  }
  loading.value = true;
  parsedLyrics.value = [];
  rawLyricText.value = '';
  
  try {
    // Step 1: Search lyric candidates
    const searchRes = await apiGet<{ status: number; candidates?: { id: string; accesskey: string }[] }>('/search/lyric', {
      hash: currentTrack.value.FileHash
    });

    if ((searchRes.status === 1 || searchRes.status === 200) && searchRes.candidates && searchRes.candidates.length > 0) {
      const candidate = searchRes.candidates[0];
      
      // Step 2: Download lyric detail
      const detailRes = await apiGet<{ status: number; lyric?: string }>('/lyric', {
        id: candidate.id,
        accesskey: candidate.accesskey
      });

      if ((detailRes.status === 1 || detailRes.status === 200) && detailRes.lyric) {
        rawLyricText.value = detailRes.lyric;
        parsedLyrics.value = parseLrc(detailRes.lyric);
      } else {
        rawLyricText.value = '[00:00.00] 无法加载歌词文本';
      }
    } else {
      rawLyricText.value = '[00:00.00] 暂无歌词';
      parsedLyrics.value = [{ time: 0, text: '暂无歌词' }];
    }
  } catch (e) {
    console.error('Lyric fetch failed', e);
    rawLyricText.value = '[00:00.00] 歌词加载出错';
    parsedLyrics.value = [{ time: 0, text: '歌词加载出错' }];
  } finally {
    loading.value = false;
  }
}

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

const activeIndex = computed(() => {
  if (parsedLyrics.value.length === 0) return -1;
  const current = currentTime.value;
  const idx = parsedLyrics.value.findIndex(l => l.time > current);
  if (idx === -1) return parsedLyrics.value.length - 1;
  return Math.max(0, idx - 1);
});

// Autoscroll watcher
watch(activeIndex, (newIdx) => {
  if (newIdx === -1) return;
  const el = document.getElementById(`lyric-line-${newIdx}`);
  if (el) {
    el.scrollIntoView({ behavior: 'smooth', block: 'center' });
  }
});

watch(currentTrack, () => {
  loadLyrics();
}, { deep: true });

onMounted(() => {
  loadLyrics();
});
</script>

<template>
  <div class="list-view">
    <div class="page-head">
      <div>
        <div class="kicker">NOW PLAYING · 正在播放</div>
        <h1>歌词写真<i>Lyrics</i></h1>
      </div>
      <div class="date">
        同步滚动中
      </div>
    </div>

    <!-- Empty/No track state -->
    <div v-if="!currentTrack" class="spinner">
      未选择正在播放的曲目。请在首页或搜索页点播。
    </div>

    <!-- Spinner -->
    <div v-else-if="loading" class="spinner">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
        <circle cx="12" cy="12" r="10" stroke="rgba(34,27,18,0.1)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
      </svg>
      译稿编撰中…
    </div>

    <!-- Lyric layout -->
    <div v-else class="lyric-container">
      <!-- Left cover & name -->
      <div class="lyric-left">
        <div class="big-cover">
          <!-- Stable img with inline-SVG fallback (computed). Avoids
               flicker by keeping the element mounted; cover swaps smoothly. -->
          <img :src="coverUrl" alt="cover" style="transition: opacity 0.2s ease;" />
        </div>
        <h2>{{ currentTrack.SongName }}</h2>
        <p>{{ currentTrack.SingerName }}</p>
      </div>

      <!-- Right scrolling lyrics -->
      <div class="lyric-right">
        <div class="lyric-scroll">
          <div 
            v-for="(line, idx) in parsedLyrics" 
            :key="idx"
            :id="`lyric-line-${idx}`"
            class="lyric-line"
            :class="{ active: idx === activeIndex }"
          >
            {{ line.text }}
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
/* Scoped overrides */
</style>
