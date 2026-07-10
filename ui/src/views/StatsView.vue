<script setup lang="ts">
import { ref, onMounted, watch, nextTick, type ComponentPublicInstance } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { normalizeTrack, type Track } from '../api/normalizer';
import { playAll, playerStore } from '../api/playerStore';
import { animateBarHeight, animateCountUp, isReducedMotion } from '../api/motion';

type Range = '7d' | '30d' | 'all';
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

interface TimelineItem {
  date: string;
  count: number;
}
const timeline = ref<TimelineItem[]>([]);
const maxTimelineCount = ref(1);
const timelineBarEls = ref<HTMLElement[]>([]);
let statsRequestId = 0;

const aiApiKey = ref(localStorage.getItem('deepseek_api_key') || '');
const aiResult = ref('');
const aiLoading = ref(false);
const aiError = ref('');

const rangeLabels: Record<Range, string> = {
  '7d': '7天',
  '30d': '30天',
  'all': '全部',
};

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
  localStorage.setItem('deepseek_api_key', aiApiKey.value);
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
  <div class="list-view">
    <div class="page-head">
      <div>
        <div class="kicker">LISTENING STATS · 听歌统计</div>
        <h1>我的统计<i>Statistics</i></h1>
      </div>
      <div class="date">
        <div class="range-tabs">
          <button
            v-for="r in (['7d', '30d', 'all'] as Range[])"
            :key="r"
            :class="{ active: range === r }"
            @click="range = r"
          >{{ rangeLabels[r] }}</button>
        </div>
      </div>
    </div>

    <div v-if="loading" class="spinner">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
        <circle cx="12" cy="12" r="10" stroke="var(--rule)"></circle>
        <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
      </svg>
      正在汇总数据…
    </div>

    <div v-else-if="error" class="spinner" style="color: var(--accent);">
      {{ error }} · <span class="retry-link" @click="loadStats">重试</span>
    </div>

    <template v-else-if="summary">
      <div class="stats-overview">
        <div class="stat-card">
          <span class="stat-value">{{ displayTotalPlays }}</span>
          <span class="stat-label">总播放</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ formatDuration(displayListenedSeconds) }}</span>
          <span class="stat-label">总时长</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ displayUniqueSongs }}</span>
          <span class="stat-label">不同歌曲</span>
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
            <img v-if="item.cover_url" :src="item.cover_url" class="top-cover" loading="lazy">
            <div v-else class="top-cover placeholder"></div>
            <div class="top-info">
              <span class="top-name">{{ item.name }}</span>
              <span class="top-sub" v-if="item.singer">{{ item.singer }}</span>
            </div>
            <span class="top-count">{{ item.play_count }}次</span>
          </div>
          <p v-if="topSongs.length === 0" class="empty">暂无数据</p>
        </div>

        <div class="top-section">
          <h3>Top 歌手</h3>
          <div v-for="(item, i) in topArtists" :key="i" class="top-item">
            <img v-if="item.cover_url" :src="item.cover_url" class="top-cover artist" loading="lazy">
            <div v-else class="top-cover placeholder artist"></div>
            <div class="top-info">
              <span class="top-name">{{ item.name }}</span>
            </div>
            <span class="top-count">{{ item.play_count }}次</span>
          </div>
          <p v-if="topArtists.length === 0" class="empty">暂无数据</p>
        </div>

        <div class="top-section">
          <h3>Top 专辑</h3>
          <div v-for="(item, i) in topAlbums" :key="i" class="top-item">
            <img v-if="item.cover_url" :src="item.cover_url" class="top-cover" loading="lazy">
            <div v-else class="top-cover placeholder"></div>
            <div class="top-info">
              <span class="top-name">{{ item.name }}</span>
              <span class="top-sub" v-if="item.singer">{{ item.singer }}</span>
            </div>
            <span class="top-count">{{ item.play_count }}次</span>
          </div>
          <p v-if="topAlbums.length === 0" class="empty">暂无数据</p>
        </div>
      </div>

      <!-- Timeline chart -->
      <div class="stats-timeline" v-if="timeline.length > 0">
        <h3>播放时间线</h3>
        <div class="timeline-chart">
          <div v-for="(item, i) in timeline" :key="item.date" class="timeline-bar">
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
          <input type="password" v-model="aiApiKey" placeholder="DeepSeek API Key" class="ai-key-input">
          <button @click="runAIAnalysis" :disabled="aiLoading" class="ai-btn">
            {{ aiLoading ? '分析中...' : 'AI 分析' }}
          </button>
        </div>
        <p v-if="aiError" class="ai-error">{{ aiError }}</p>
        <div v-if="aiResult" class="ai-result">{{ aiResult }}</div>
        <p v-if="!aiResult && !aiLoading && !aiError" class="ai-hint">
          输入你的 DeepSeek API Key，AI 会分析你的听歌习惯。Key 仅保存在本地浏览器。
        </p>
      </div>
    </template>
  </div>
</template>

<style scoped>
.range-tabs {
  display: flex;
  gap: 4px;
}
.range-tabs button {
  background: var(--paper-2);
  border: 1px solid var(--rule);
  padding: 4px 14px;
  border-radius: 4px;
  cursor: pointer;
  color: var(--ink-soft);
  font-size: 12px;
  font-family: var(--font-sans);
  transition: background 0.15s, color 0.15s;
}
.range-tabs button:hover {
  background: var(--paper-edge);
}
.range-tabs button.active {
  background: var(--accent);
  color: var(--paper);
  border-color: var(--accent);
}
.retry-link {
  cursor: pointer;
  text-decoration: underline;
}
.stats-overview {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 14px;
  margin-bottom: 28px;
}
.stat-card {
  background: var(--paper-2);
  border: 1px solid var(--rule);
  border-radius: 8px;
  padding: 18px;
  text-align: center;
}
.stat-value {
  display: block;
  font-size: 28px;
  font-weight: 700;
  color: var(--ink);
  font-family: var(--font-serif);
}
.stat-label {
  display: block;
  font-size: 12px;
  color: var(--ink-mute);
  margin-top: 4px;
  letter-spacing: 0.06em;
}
.stats-tops {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 20px;
}
.top-section h3 {
  font-family: var(--font-serif);
  color: var(--ink);
  font-size: 15px;
  font-weight: 600;
  margin: 0 0 10px;
  padding-bottom: 6px;
  border-bottom: 1px solid var(--rule);
}
.top-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 7px 0;
  border-bottom: 1px solid var(--rule-soft);
}
.top-item.playable {
  cursor: pointer;
}
.top-item.playable:hover,
.top-item.active {
  background: var(--paper-edge);
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
  background: var(--paper-edge);
}
.top-cover.placeholder.artist {
  border-radius: 50%;
}
.top-info {
  flex: 1;
  min-width: 0;
}
.top-name {
  display: block;
  font-size: 13px;
  color: var(--ink);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.top-sub {
  display: block;
  font-size: 11px;
  color: var(--ink-mute);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.top-count {
  font-size: 12px;
  color: var(--ink-soft);
  white-space: nowrap;
  font-family: var(--font-sans);
}
.empty {
  color: var(--ink-mute);
  font-size: 13px;
  padding: 16px 0;
  font-style: italic;
  text-align: center;
}

/* Timeline chart */
.stats-timeline {
  margin: 24px 0;
}
.stats-timeline h3 {
  font-family: var(--font-serif);
  color: var(--ink);
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
}
.bar-fill {
  width: 20px;
  background: var(--accent);
  border-radius: 2px 2px 0 0;
  min-height: 2px;
}
.bar-label {
  font-size: 10px;
  color: var(--ink-mute);
  margin-top: 4px;
}
.bar-count {
  font-size: 9px;
  color: var(--ink-soft);
  position: absolute;
  top: -14px;
}

/* AI Analysis */
.stats-ai {
  margin: 24px 0;
}
.stats-ai h3 {
  font-family: var(--font-serif);
  color: var(--ink);
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
  border: 1px solid var(--rule);
  border-radius: 4px;
  background: var(--paper);
  color: var(--ink);
  font-size: 13px;
  font-family: var(--font-sans);
}
.ai-btn {
  padding: 6px 16px;
  border: none;
  border-radius: 4px;
  background: var(--accent);
  color: var(--paper);
  cursor: pointer;
  font-size: 13px;
  font-family: var(--font-sans);
}
.ai-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
.ai-error {
  color: #e53935;
  font-size: 12px;
}
.ai-result {
  background: var(--paper-2);
  border: 1px solid var(--rule);
  border-radius: 8px;
  padding: 16px;
  font-size: 13px;
  line-height: 1.6;
  color: var(--ink);
  white-space: pre-wrap;
}
.ai-hint {
  font-size: 12px;
  color: var(--ink-mute);
}

@media (max-width: 768px) {
  .stats-overview {
    grid-template-columns: repeat(2, 1fr);
  }
  .stats-tops {
    grid-template-columns: 1fr;
  }
}
</style>
