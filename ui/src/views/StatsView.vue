<script setup lang="ts">
import { ref, computed, onMounted, watch, nextTick, type ComponentPublicInstance } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { normalizeTrack, type Track } from '../api/normalizer';
import { playAll, playerStore } from '../playback/playerStore';
import { animateBarHeight, animateCountUp, isReducedMotion } from '../shared/motion/motion';
import SkinPageHeader from '../shared/ui/SkinPageHeader.vue';
import SkinButton from '../shared/ui/SkinButton.vue';
import SkinEmptyState from '../shared/ui/SkinEmptyState.vue';

type Range = '1d' | '7d' | '30d';
const range = ref<Range>('30d');
const loading = ref(true);
const error = ref('');

interface Summary {
  total_plays: number;
  total_listened_seconds: number;
  unique_songs: number;
  unique_artists: number;
  completion_rate: number;
}
const summary = ref<Summary | null>(null);
const displayTotalPlays = ref(0);
const displayListenedSeconds = ref(0);
const displayUniqueSongs = ref(0);
const displayUniqueArtists = ref(0);
const displayCompletionPercent = ref(0);

interface TopItem {
  song_hash?: string;
  album_id?: string;
  name: string;
  singer?: string;
  album?: string;
  cover_url?: string;
  play_count: number;
  total_listened_seconds: number;
}
const topSongs = ref<TopItem[]>([]);
const topArtists = ref<TopItem[]>([]);
const topAlbums = ref<TopItem[]>([]);

/** Hero trophy: the most-played song's cover, square and quiet. */
const topCoverUrl = computed(() => topSongs.value[0]?.cover_url ?? '');

interface TimelineItem {
  date: string;
  count: number;
}
const timeline = ref<TimelineItem[]>([]);
const maxTimelineCount = ref(1);
const timelineBarEls = ref<HTMLElement[]>([]);
let statsRequestId = 0;

localStorage.removeItem('deepseek_api_key');
const aiApiKey = ref('');
const aiResult = ref('');
const aiLoading = ref(false);
const aiError = ref('');

const rangeLabels: Record<Range, string> = {
  '1d': '每日',
  '7d': '每周',
  '30d': '每月',
};

/** One LP ≈ 44 minutes — the turntable-night unit of listening time. */
const LP_SECONDS = 44 * 60;
const vinylCount = computed(() => (displayListenedSeconds.value / LP_SECONDS).toFixed(1));

function formatDuration(seconds: number): string {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  return h > 0 ? `${h}h ${m}m` : `${m}m`;
}

function topSongToTrack(item: TopItem): Track {
  return normalizeTrack({
    FileHash: item.song_hash || '',
    SongName: item.name,
    SingerName: item.singer || '',
    AlbumName: item.album || '',
    AlbumID: item.album_id || '',
    Duration: 0,
    Image: item.cover_url || '',
  });
}

function playTopSong(item: TopItem) {
  const tracks = topSongs.value
    .filter(song => !!song.song_hash)
    .map(topSongToTrack);
  const idx = tracks.findIndex(track => track.FileHash === item.song_hash);
  if (idx === -1) return;
  playAll(tracks, idx);
}

function isCurrentHash(hash?: string) {
  return !!hash && playerStore.currentTrack?.FileHash === hash;
}

function setTimelineBarEl(el: Element | ComponentPublicInstance | null, index: number) {
  if (el instanceof HTMLElement) timelineBarEls.value[index] = el;
}

function timelineHeight(item: TimelineItem): number {
  return Math.round((item.count / maxTimelineCount.value) * 100);
}

async function animateSummaryValues(s: Summary, isActive: () => boolean) {
  await Promise.all([
    animateCountUp(displayTotalPlays, s.total_plays, { delay: 0, isActive }),
    animateCountUp(displayListenedSeconds, s.total_listened_seconds, { delay: 0.04, isActive }),
    animateCountUp(displayUniqueSongs, s.unique_songs, { delay: 0.08, isActive }),
    animateCountUp(displayUniqueArtists, s.unique_artists, { delay: 0.1, isActive }),
    animateCountUp(displayCompletionPercent, Math.round(s.completion_rate * 100), { delay: 0.12, isActive }),
  ]);
}

async function animateTimelineBars(isActive: () => boolean) {
  await nextTick();
  if (!isActive()) return;
  timelineBarEls.value = timelineBarEls.value.slice(0, timeline.value.length);
  timeline.value.forEach((item, index) => {
    if (!isActive()) return;
    const el = timelineBarEls.value[index];
    if (!el) return;
    const height = timelineHeight(item);
    if (isReducedMotion()) {
      el.style.height = `${height}px`;
      return;
    }
    el.style.height = '2px';
    animateBarHeight(el, height, { delay: index * 0.015 });
  });
}

async function loadStats() {
  const requestId = ++statsRequestId;
  const requestedRange = range.value;
  const isActive = () => requestId === statsRequestId && requestedRange === range.value;
  loading.value = true;
  error.value = '';
  timelineBarEls.value = [];
  try {
    const [s, songs, artists, albums, tl] = await Promise.all([
      invoke<string>('stats_get_summary', { range: requestedRange }),
      invoke<string>('stats_get_top', { kind: 'song', range: requestedRange, limit: 10 }),
      invoke<string>('stats_get_top', { kind: 'artist', range: requestedRange, limit: 10 }),
      invoke<string>('stats_get_top', { kind: 'album', range: requestedRange, limit: 10 }),
      invoke<string>('stats_get_timeline', { range: requestedRange }),
    ]);
    if (!isActive()) return;
    summary.value = JSON.parse(s);
    topSongs.value = JSON.parse(songs).items || [];
    topArtists.value = JSON.parse(artists).items || [];
    topAlbums.value = JSON.parse(albums).items || [];
    timeline.value = JSON.parse(tl).items || [];
    maxTimelineCount.value = Math.max(1, ...timeline.value.map(t => t.count));
    loading.value = false;
    await nextTick();
    if (!isActive()) return;
    if (summary.value) await animateSummaryValues(summary.value, isActive);
    await animateTimelineBars(isActive);
  } catch (e) {
    if (!isActive()) return;
    console.error('Stats load failed:', e);
    error.value = '统计数据加载失败';
  } finally {
    if (isActive()) loading.value = false;
  }
}

async function runAIAnalysis() {
  if (!aiApiKey.value) {
    aiError.value = '请先输入 API Key';
    return;
  }
  aiLoading.value = true;
  aiError.value = '';
  aiResult.value = '';
  try {
    const [s, songs, artists, tl] = await Promise.all([
      invoke<string>('stats_get_summary', { range: range.value }),
      invoke<string>('stats_get_top', { kind: 'song', range: range.value, limit: 5 }),
      invoke<string>('stats_get_top', { kind: 'artist', range: range.value, limit: 5 }),
      invoke<string>('stats_get_timeline', { range: range.value }),
    ]);
    const statsJson = JSON.stringify({
      summary: JSON.parse(s),
      topSongs: JSON.parse(songs).items || [],
      topArtists: JSON.parse(artists).items || [],
      timeline: JSON.parse(tl).items || [],
    });
    aiResult.value = await invoke<string>('ai_analyze', {
      apiKey: aiApiKey.value,
      statsJson,
    });
  } catch (e: any) {
    aiError.value = e?.message || String(e);
  } finally {
    aiLoading.value = false;
  }
}

onMounted(loadStats);
watch(range, loadStats);
</script>

<template>
  <div class="list-view stats-page">
    <SkinPageHeader title="我的统计" kicker="LISTENING STATS · 听歌统计" subtitle="Statistics">
      <template #actions>
        <div class="range-tabs">
          <button
            v-for="r in (['1d', '7d', '30d'] as Range[])"
            :key="r"
            :class="{ active: range === r }"
            @click="range = r"
          >{{ rangeLabels[r] }}</button>
        </div>
      </template>
    </SkinPageHeader>

    <div v-if="loading" class="spinner">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
        <circle cx="12" cy="12" r="10" stroke="var(--border-subtle)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
      </svg>
      正在汇总数据…
    </div>

    <div v-else-if="error" class="spinner" style="color: var(--accent);">
      {{ error }} · <span class="retry-link" @click="loadStats">重试</span>
    </div>

    <template v-else-if="summary">
      <!-- Listening clock: total time as the page hero, measured in LPs -->
      <section class="stats-hero" data-test="stats-hero">
        <div class="stats-hero-copy">
          <span class="stats-hero-kicker">LISTENING TIME · 听歌时钟</span>
          <span class="stats-hero-value">{{ formatDuration(displayListenedSeconds) }}</span>
          <span class="stats-hero-vinyl">≈ {{ vinylCount }} 张黑胶</span>
        </div>
        <div class="stats-hero-square" aria-hidden="true">
          <img v-if="topCoverUrl" :src="topCoverUrl" alt="" />
        </div>
      </section>

      <div class="stats-overview">
        <div class="stat-card">
          <span class="stat-value">{{ displayTotalPlays }}</span>
          <span class="stat-label">总播放</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ displayUniqueSongs }}</span>
          <span class="stat-label">不同歌曲</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ displayUniqueArtists }}</span>
          <span class="stat-label">不同艺人</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ displayCompletionPercent }}%</span>
          <span class="stat-label">完成率</span>
        </div>
      </div>

      <div class="stats-tops">
        <div class="top-section">
          <h3>Top 歌曲</h3>
          <div
            v-for="(item, i) in topSongs"
            :key="item.song_hash || i"
            class="top-item"
            :class="{ playable: !!item.song_hash, active: isCurrentHash(item.song_hash) }"
            @click="playTopSong(item)"
          >
            <span class="vinyl-thumb" aria-hidden="true">
              <img v-if="item.cover_url" :src="item.cover_url" class="top-cover" loading="lazy">
              <span v-else class="top-cover placeholder"></span>
              <span class="vinyl-thumb-spindle"></span>
            </span>
            <div class="top-info">
              <span class="top-name">{{ item.name }}</span>
              <span class="top-sub" v-if="item.singer">{{ item.singer }}</span>
            </div>
            <span
              v-if="isCurrentHash(item.song_hash)"
              class="aurora-eq"
              :class="{ 'is-live': playerStore.isPlaying }"
              aria-hidden="true"
            ><i /><i /><i /></span>
            <span class="top-count">{{ item.play_count }}次</span>
          </div>
          <div v-if="topSongs.length === 0" class="empty-placeholder"><SkinEmptyState message="暂无数据" /></div>
        </div>

        <div class="top-section">
          <h3>Top 歌手</h3>
          <div class="top-cover-grid">
            <div
              v-for="(item, i) in topArtists"
              :key="i"
              class="top-cover-cell"
              :title="`${item.name} · ${item.play_count}次`"
            >
              <img v-if="item.cover_url" :src="item.cover_url" class="top-cover artist" loading="lazy">
              <div v-else class="top-cover placeholder artist"></div>
              <span class="top-cover-name">{{ item.name }}</span>
            </div>
          </div>
          <div v-if="topArtists.length === 0" class="empty-placeholder"><SkinEmptyState message="暂无数据" /></div>
        </div>

        <div class="top-section">
          <h3>Top 专辑</h3>
          <div class="top-cover-grid">
            <div
              v-for="(item, i) in topAlbums"
              :key="i"
              class="top-cover-cell"
              :title="`${item.name}${item.singer ? ' · ' + item.singer : ''} · ${item.play_count}次`"
            >
              <img v-if="item.cover_url" :src="item.cover_url" class="top-cover" loading="lazy">
              <div v-else class="top-cover placeholder"></div>
              <span class="top-cover-name">{{ item.name }}</span>
            </div>
          </div>
          <div v-if="topAlbums.length === 0" class="empty-placeholder"><SkinEmptyState message="暂无数据" /></div>
        </div>
      </div>

      <!-- Timeline: grooves the needle sweeps across -->
      <div class="stats-timeline" v-if="timeline.length > 0">
        <h3>播放时间线</h3>
        <div class="timeline-chart">
          <div
            v-for="(item, i) in timeline"
            :key="item.date"
            class="timeline-bar"
            :title="`${item.date} · ${item.count} 次`"
          >
            <span class="bar-count">{{ item.count }}</span>
            <div
              class="bar-fill"
              :ref="(el) => setTimelineBarEl(el, i)"
              :style="{ height: isReducedMotion() ? timelineHeight(item) + 'px' : '2px' }"
            ></div>
            <span class="bar-label">{{ item.date.slice(5) }}</span>
          </div>
        </div>
      </div>

      <!-- AI Analysis -->
      <div class="stats-ai">
        <h3>AI 听歌分析</h3>
        <div class="ai-input-row">
          <input
            type="password"
            v-model="aiApiKey"
            placeholder="DeepSeek API Key"
            class="ai-key-input"
            autocomplete="off"
            spellcheck="false"
          >
          <SkinButton variant="primary" size="md" :disabled="aiLoading" @click="runAIAnalysis">
            {{ aiLoading ? '分析中...' : 'AI 分析' }}
          </SkinButton>
        </div>
        <p v-if="aiError" class="ai-error">{{ aiError }}</p>
        <div v-if="aiResult" class="ai-result">{{ aiResult }}</div>
        <p v-if="!aiResult && !aiLoading && !aiError" class="ai-hint">
          API Key 仅在当前页面会话中使用，不会保存到磁盘。听歌统计摘要会发送给 DeepSeek 生成分析结果。
        </p>
      </div>
    </template>
  </div>
</template>

<style scoped>
.stats-page {
  box-sizing: border-box;
  width: 100%;
  max-width: 100%;
  min-width: 0;
  padding-bottom: 24px;
  overflow-x: hidden;
}

.range-tabs {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
}
.range-tabs button {
  background: var(--surface-2);
  border: 1px solid var(--border-subtle);
  padding: 4px 14px;
  border-radius: 8px;
  cursor: pointer;
  color: var(--text-secondary);
  font-size: 12px;
  font-family: var(--font-sans);
  transition: background 0.15s, color 0.15s;
}
.range-tabs button:hover {
  background: var(--surface-elevated);
}
.range-tabs button.active {
  background: var(--accent);
  color: var(--app-bg);
  border-color: var(--accent);
}
.retry-link {
  cursor: pointer;
  text-decoration: underline;
}

/* ── Listening clock hero ── */
.stats-hero {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 20px;
  margin: 4px 0 22px;
  padding: 4px 2px 0;
  min-width: 0;
}

.stats-hero-copy {
  display: flex;
  flex-direction: column;
  gap: 6px;
  min-width: 0;
}

.stats-hero-kicker {
  font-family: 'Inter', 'Microsoft YaHei UI', 'PingFang SC', system-ui, sans-serif;
  font-size: 11px;
  font-weight: 500;
  letter-spacing: 0.12em;
  text-transform: uppercase;
  color: var(--accent);
}

.stats-hero-value {
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  font-size: clamp(40px, 4.6vw, 64px);
  font-weight: 700;
  line-height: 1.05;
  letter-spacing: -0.02em;
  color: var(--text-primary);
  font-variant-numeric: tabular-nums;
}

.stats-hero-vinyl {
  font-size: 14px;
  color: var(--text-secondary);
  font-variant-numeric: tabular-nums;
}

/* Hero trophy cover — most-played, square */
.stats-hero-square {
  width: clamp(84px, 9vw, 128px);
  aspect-ratio: 1;
  border-radius: 8px;
  overflow: hidden;
  background: var(--surface-2);
  border: 1px solid var(--border-subtle);
  box-shadow: 0 16px 36px rgba(0, 0, 0, 0.4);
  flex: none;
}

.stats-hero-square img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

/* ── Dashboard cards ── */
.stats-overview {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 12px;
  margin-bottom: 28px;
  min-width: 0;
}
.stat-card {
  background: color-mix(in srgb, var(--surface-1) 72%, transparent);
  border: 1px solid var(--border-subtle);
  border-radius: 8px;
  padding: 16px 18px;
}
.stat-value {
  display: block;
  font-size: 26px;
  font-weight: 700;
  color: var(--text-primary);
  font-variant-numeric: tabular-nums;
}
.stat-label {
  display: block;
  font-size: 12px;
  color: var(--text-muted);
  margin-top: 4px;
  letter-spacing: 0.06em;
}

/* ── Top sections ── */
.stats-tops {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 20px;
  min-width: 0;
}

@media (max-width: 1100px) {
  .stats-overview {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
  .stats-tops {
    grid-template-columns: 1fr;
  }
}
.top-section h3 {
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  color: var(--text-primary);
  font-size: 15px;
  font-weight: 600;
  margin: 0 0 10px;
  padding-bottom: 6px;
  border-bottom: 1px solid var(--border-subtle);
}
.top-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 7px 4px;
  border-bottom: 1px solid var(--border-subtle);
  border-radius: 6px;
}
.top-item.playable {
  cursor: pointer;
}
.top-item.playable:hover,
.top-item.active {
  background: color-mix(in srgb, var(--text-primary) 5%, transparent);
}
.top-cover {
  width: 36px;
  height: 36px;
  border-radius: 4px;
  object-fit: cover;
  flex-shrink: 0;
}
.top-cover.artist {
  border-radius: 50%;
}
.top-cover.placeholder {
  background: var(--surface-2);
  display: block;
}
.top-cover.placeholder.artist {
  border-radius: 50%;
}

/* Vinyl thumb for top songs: disc + spindle, quarter-turn on hover */
.vinyl-thumb {
  position: relative;
  width: 36px;
  height: 36px;
  border-radius: 50%;
  background: #0a0a09;
  overflow: hidden;
  flex-shrink: 0;
  box-shadow: 0 0 0 1px color-mix(in srgb, #fff 5%, transparent);
  transition: transform 0.25s ease;
}
.vinyl-thumb::after {
  content: '';
  position: absolute;
  inset: 0;
  border-radius: 50%;
  background: repeating-radial-gradient(circle at 50% 50%,
    rgba(255, 255, 255, 0.06) 0 1px,
    transparent 1px 3px);
  pointer-events: none;
}
.vinyl-thumb .top-cover {
  width: 100%;
  height: 100%;
  border-radius: 50%;
}
.vinyl-thumb-spindle {
  position: absolute;
  left: 50%;
  top: 50%;
  width: 30%;
  aspect-ratio: 1;
  transform: translate(-50%, -50%);
  border-radius: 50%;
  background: radial-gradient(circle at 50% 50%,
    var(--app-bg) 0 16%,
    color-mix(in srgb, var(--accent) 82%, #000 18%) 17% 100%);
  pointer-events: none;
}
.top-item.playable:hover .vinyl-thumb {
  transform: rotate(20deg);
}

/* Playing-row equalizer (same language as the home rail) */
.aurora-eq {
  display: inline-flex;
  align-items: flex-end;
  gap: 1.5px;
  height: 10px;
  flex: none;
}
.aurora-eq i {
  width: 2px;
  height: 30%;
  background: var(--accent);
  border-radius: 1px;
}
.aurora-eq.is-live i {
  animation: aurora-eq-bounce 0.9s ease-in-out infinite;
}
.aurora-eq.is-live i:nth-child(2) { animation-delay: 0.25s; }
.aurora-eq.is-live i:nth-child(3) { animation-delay: 0.5s; }
@keyframes aurora-eq-bounce {
  0%, 100% { height: 25%; }
  50% { height: 100%; }
}
@media (prefers-reduced-motion: reduce) {
  .aurora-eq.is-live i {
    animation: none;
    height: 60%;
  }
}

/* Cover grids for artists / albums — airy, name-only, counts in tooltip */
.top-cover-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 12px;
}

.top-cover-cell {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  min-width: 0;
}

.top-cover-cell .top-cover {
  width: 88px;
  height: 88px;
  border-radius: 8px;
}

.top-cover-cell .top-cover.artist {
  border-radius: 50%;
}

.top-cover-cell .top-cover.placeholder {
  background: var(--surface-2);
}

.top-cover-name {
  max-width: 100%;
  font-size: 12px;
  color: var(--text-secondary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  text-align: center;
}

.top-info {
  flex: 1;
  min-width: 0;
}
.top-name {
  display: block;
  font-size: 13px;
  color: var(--text-primary);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.top-sub {
  display: block;
  font-size: 11px;
  color: var(--text-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.top-count {
  font-size: 12px;
  color: var(--text-secondary);
  white-space: nowrap;
  font-variant-numeric: tabular-nums;
}
.empty-placeholder {
  padding: 0;
  margin: 0;
}

/* ── Timeline grooves ── */
.stats-timeline {
  margin: 24px 0;
}
.stats-timeline h3 {
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  color: var(--text-primary);
  font-size: 14px;
  margin: 0 0 8px;
}
.timeline-chart {
  display: flex;
  gap: 2px;
  align-items: flex-end;
  height: 100px;
  overflow-x: auto;
  padding-bottom: 20px;
}
.timeline-bar {
  display: flex;
  flex-direction: column;
  align-items: center;
  min-width: 30px;
  height: 100%;
  justify-content: flex-end;
  position: relative;
  cursor: default;
}
.bar-fill {
  width: 20px;
  background: var(--accent);
  border-radius: 2px 2px 0 0;
  min-height: 2px;
}
.timeline-bar:hover .bar-fill {
  filter: brightness(1.15);
}
.bar-label {
  font-size: 10px;
  color: var(--text-muted);
  margin-top: 4px;
  font-variant-numeric: tabular-nums;
}
.bar-count {
  font-size: 9px;
  color: var(--text-secondary);
  position: absolute;
  top: -14px;
  font-variant-numeric: tabular-nums;
}

/* ── AI Analysis ── */
.stats-ai {
  margin: 24px 0;
}
.stats-ai h3 {
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  color: var(--text-primary);
  font-size: 14px;
  margin: 0 0 8px;
}
.ai-input-row {
  display: flex;
  gap: 8px;
  margin-bottom: 8px;
}
.ai-key-input {
  flex: 1;
  padding: 6px 10px;
  border: 1px solid var(--border-subtle);
  border-radius: 8px;
  background: var(--surface-1);
  color: var(--text-primary);
  font-size: 13px;
  font-family: var(--font-sans);
}
.ai-error {
  color: #e53935;
  font-size: 12px;
}
.ai-result {
  background: var(--surface-1);
  border: 1px solid var(--border-subtle);
  border-radius: 8px;
  padding: 16px;
  font-size: 13px;
  line-height: 1.6;
  color: var(--text-primary);
  white-space: pre-wrap;
}
.ai-hint {
  font-size: 12px;
  color: var(--text-muted);
}

@media (max-width: 768px) {
  .stats-hero {
    flex-direction: column-reverse;
    align-items: flex-start;
  }
  .stats-overview {
    grid-template-columns: repeat(2, 1fr);
  }
  .stats-tops {
    grid-template-columns: 1fr;
  }
}
</style>
