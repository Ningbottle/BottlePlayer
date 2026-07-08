<script setup lang="ts">
import { ref, computed, watch, onMounted, onUnmounted } from 'vue';
import { playerStore, playTrack } from '../api/playerStore';
import { apiGet } from '../api/backend';
import { useLyricFollow } from '../api/useLyricFollow';
import { lyricFullscreen, setLyricFullscreen } from '../api/lyricFullscreen';
import { gsap } from 'gsap';
import { isReducedMotion } from '../api/motion';

interface LyricLine {
  time: number;
  text: string;
}

const props = defineProps<{
  isQueueOpen?: boolean;
  isDrawerOpen?: boolean;
}>();

const loading = ref(false);
const rawLyricText = ref('');
const parsedLyrics = ref<LyricLine[]>([]);

// Template ref on the .big-cover container so the GSAP fullscreen tween
// targets the layout box (240×240, overflow-controlled) rather than the img,
// which would overflow the container and get stuck at the wrong size on exit.
const coverRef = ref<HTMLElement | null>(null);
const currentTrack = computed(() => playerStore.currentTrack);
const currentTime = computed(() => playerStore.currentTime);

// Reactive lyric alignment (reads from localStorage, same source as Drawer)
const lyricAlign = ref(localStorage.getItem('tweak_lyric_align') || 'center');
const isLyricLeft = computed(() => lyricAlign.value === 'left');

// Hide compact queue when drawer or full queue is open
const showCompactQueue = computed(() => 
  isLyricLeft.value && !props.isQueueOpen && !props.isDrawerOpen && upcomingTracks.value.length > 0
);

// Get next 5 songs in queue for compact display
const upcomingTracks = computed(() => {
  const idx = playerStore.currentIndex;
  if (idx < 0 || playerStore.queue.length === 0) return [];
  const start = idx + 1;
  const result = [];
  for (let i = 0; i < 5 && start + i < playerStore.queue.length; i++) {
    result.push(playerStore.queue[start + i]);
  }
  return result;
});

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

// Auto-follow state machine — scrolls to the active line while following,
// suspends on manual wheel/touch scroll, resumes after 3s idle or via the
// return-to-current button. See useLyricFollow.ts for the state machine.
function scrollToLine(idx: number) {
  const el = document.getElementById(`lyric-line-${idx}`);
  if (el) el.scrollIntoView({ behavior: 'smooth', block: 'center' });
}
const { autoFollowing, onUserScroll, resumeFollow, resetForTrack } = useLyricFollow({
  activeIndex,
  scrollToLine,
});

watch(currentTrack, (track) => {
  resetForTrack(track?.FileHash || '');
  loadLyrics();
}, { deep: true });

// Keep lyricAlign in sync when Drawer changes it
function onStorage(e: StorageEvent) {
  if (e.key === 'tweak_lyric_align' && e.newValue) {
    lyricAlign.value = e.newValue;
  }
}

let alignInterval: ReturnType<typeof setInterval> | null = null;

function onKeydown(e: KeyboardEvent) {
  if (e.key === 'Escape' && lyricFullscreen.value) setLyricFullscreen(false);
}

function toggleFullscreen() {
  setLyricFullscreen(!lyricFullscreen.value);
}

onMounted(() => {
  loadLyrics();
  window.addEventListener('storage', onStorage);
  window.addEventListener('keydown', onKeydown);
  // Also poll periodically (same-tab changes don't fire storage event)
  alignInterval = setInterval(() => {
    const v = localStorage.getItem('tweak_lyric_align') || 'center';
    if (v !== lyricAlign.value) lyricAlign.value = v;
  }, 500);
});

onUnmounted(() => {
  window.removeEventListener('storage', onStorage);
  window.removeEventListener('keydown', onKeydown);
  if (alignInterval) clearInterval(alignInterval);
  if (lyricFullscreen.value) setLyricFullscreen(false); // reset on unmount
});

// Cover scale on fullscreen: animate the .big-cover *container* (not the img)
// between the CSS baseline (240px) and the fullscreen size (320px). On exit we
// clear the inline width so the CSS baseline (240px, or 180px with queue) takes
// over again — without clearProps the img would be stuck at the tweened value.
// Reduced-motion users still get the size change, just applied instantly.
watch(lyricFullscreen, (fs) => {
  const cover = coverRef.value;
  if (!cover) return;
  if (isReducedMotion()) {
    if (fs) gsap.set(cover, { width: 320 });
    else gsap.set(cover, { clearProps: 'width' });
    return;
  }
  if (fs) {
    gsap.to(cover, { width: 320, duration: 0.4, ease: 'power2.out' });
  } else {
    gsap.to(cover, { width: 240, duration: 0.4, ease: 'power2.out', clearProps: 'width' });
  }
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
      <button data-test="lyric-fullscreen-toggle" @click="toggleFullscreen">全屏</button>
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
    <div v-else class="lyric-container" :class="{ 'with-queue': isQueueOpen }">
      <!-- Left cover & name -->
      <div class="lyric-meta" @dblclick="setLyricFullscreen(true)">
        <div class="big-cover" ref="coverRef">
          <!-- Stable img with inline-SVG fallback (computed). Avoids
               flicker by keeping the element mounted; cover swaps smoothly. -->
          <img :src="coverUrl" alt="cover" style="transition: opacity 0.2s var(--ease-spa);" />
        </div>
        <h2>{{ currentTrack.SongName }}</h2>
        <p>{{ currentTrack.SingerName }}</p>
      </div>

      <!-- Right scrolling lyrics -->
      <div class="lyric-right">
        <div
          class="lyric-scroll"
          @wheel.passive="onUserScroll()"
          @touchmove.passive="onUserScroll()"
        >
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
        <button
          v-if="!autoFollowing"
          class="return-to-current"
          data-test="return-to-current"
          @click="resumeFollow()"
        >回到当前行</button>
      </div>

      <!-- Compact queue (album art only) when lyrics are left-aligned -->
      <div v-if="showCompactQueue" class="compact-queue">
        <div class="compact-queue-title">接下来</div>
        <div 
          v-for="track in upcomingTracks" 
          :key="track.FileHash"
          class="compact-cover"
          @click="playTrack(track)"
        >
          <img v-if="track.Image" :src="track.Image" alt="cover" />
          <svg v-else viewBox="0 0 48 48">
            <rect width="48" height="48" fill="var(--ink-mute)"/>
            <text x="24" y="30" text-anchor="middle" font-family="var(--font-serif)" font-style="italic" font-size="14" fill="var(--paper)">
              {{ track.SongName.slice(0, 2) }}
            </text>
          </svg>
        </div>
      </div>
    </div>

    <button v-if="lyricFullscreen" class="exit-fullscreen" @click="setLyricFullscreen(false)">退出全屏</button>
  </div>
</template>

<style scoped>
/* Return-to-current floating button (visible when auto-follow is suspended) */
.lyric-right {
  position: relative;
}
.return-to-current {
  position: absolute;
  bottom: 16px;
  left: 50%;
  transform: translateX(-50%);
  padding: 6px 14px;
  border: 1px solid var(--rule-soft);
  border-radius: 999px;
  background: var(--paper);
  color: var(--ink);
  font-family: var(--font-serif);
  font-style: italic;
  font-size: 12px;
  cursor: pointer;
  box-shadow: 0 2px 8px rgba(40, 28, 12, 0.12);
  transition: transform 0.15s var(--ease-spa), box-shadow 0.15s var(--ease-spa);
  z-index: 5;
}
.return-to-current:hover {
  transform: translateX(-50%) scale(1.04);
  box-shadow: 0 4px 14px rgba(40, 28, 12, 0.2);
}

/* Responsive layout squeeze when Queue is open */
.lyric-container {
  transition: padding 0.3s var(--ease-spa);
}

.lyric-container.with-queue {
  /* Push everything to the left to avoid being covered by QueuePanel */
  padding-right: 340px; 
}

/* Big cover overrides
   Note: no `transition` on .big-cover itself — the fullscreen size change is
   driven by a GSAP tween (see watch(lyricFullscreen)), and a CSS `transition:
   all` here would fight it frame-by-frame. The .with-queue squeeze below is
   applied instantly; the surrounding .lyric-container padding transition still
   smooths the overall layout shift. */
.with-queue .big-cover {
  width: 180px !important;
  height: 180px !important;
}

/* Text overrides */
.lyric-meta h2 {
  transition: font-size 0.3s var(--ease-spa);
}

.with-queue .lyric-meta h2 {
  font-size: 18px !important;
}

.with-queue .lyric-meta p {
  font-size: 14px !important;
}

/* Compact queue (album art only) */
.compact-queue {
  position: fixed;
  right: 64px;
  top: 50%;
  transform: translateY(-50%);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  z-index: 10;
}

.compact-queue-title {
  font-family: var(--font-serif);
  font-style: italic;
  font-size: 11px;
  color: var(--ink-mute);
  letter-spacing: 0.05em;
  margin-bottom: 4px;
}

.compact-cover {
  width: 56px;
  height: 56px;
  border-radius: 6px;
  overflow: hidden;
  cursor: pointer;
  box-shadow: 0 2px 8px rgba(40,28,12,0.15);
  border: 1px solid var(--rule-soft);
  transition: transform 0.2s var(--ease-spa), box-shadow 0.2s var(--ease-spa);
}

.compact-cover:hover {
  transform: scale(1.08);
  box-shadow: 0 4px 14px rgba(40,28,12,0.25);
}

.compact-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.compact-cover svg {
  width: 100%;
  height: 100%;
}
</style>
