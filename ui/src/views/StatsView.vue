<script setup lang="ts">
import { ref, onMounted, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';

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

interface TopItem {
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

interface RecentItem {
  name: string;
  singer: string;
  album: string;
  cover_url: string;
  duration_seconds: number;
  completed: boolean;
  listened_seconds: number;
  quality: string;
  played_at: number;
}
const recent = ref<RecentItem[]>([]);

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

function formatTimeAgo(ts: number): string {
  const diff = Date.now() - ts;
  const m = Math.floor(diff / 60000);
  if (m < 1) return '刚刚';
  if (m < 60) return `${m}分钟前`;
  const h = Math.floor(m / 60);
  if (h < 24) return `${h}小时前`;
  const d = Math.floor(h / 24);
  if (d === 1) return '昨天';
  if (d < 7) return `${d}天前`;
  return new Date(ts).toLocaleDateString('zh-CN');
}

async function loadTimelineAndRecent() {
  try {
    const [tl, rec] = await Promise.all([
      invoke<string>('stats_get_timeline', { range: range.value }),
      invoke<string>('stats_get_recent', { offset: 0, limit: 20 }),
    ]);
    timeline.value = JSON.parse(tl).items || [];
    maxTimelineCount.value = Math.max(1, ...timeline.value.map(t => t.count));
    recent.value = JSON.parse(rec).items || [];
  } catch (e) {
    console.error('Timeline/recent load failed:', e);
  }
}

async function loadStats() {
  loading.value = true;
  error.value = '';
  try {
    const [s, songs, artists, albums] = await Promise.all([
      invoke<string>('stats_get_summary', { range: range.value }),
      invoke<string>('stats_get_top', { kind: 'song', range: range.value, limit: 10 }),
      invoke<string>('stats_get_top', { kind: 'artist', range: range.value, limit: 10 }),
      invoke<string>('stats_get_top', { kind: 'album', range: range.value, limit: 10 }),
    ]);
    summary.value = JSON.parse(s);
    topSongs.value = JSON.parse(songs).items || [];
    topArtists.value = JSON.parse(artists).items || [];
    topAlbums.value = JSON.parse(albums).items || [];
    await loadTimelineAndRecent();
  } catch (e) {
    console.error('Stats load failed:', e);
    error.value = '统计数据加载失败';
  } finally {
    loading.value = false;
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
          <span class="stat-value">{{ summary.total_plays }}</span>
          <span class="stat-label">总播放</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ formatDuration(summary.total_listened_seconds) }}</span>
          <span class="stat-label">总时长</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ summary.unique_songs }}</span>
          <span class="stat-label">不同歌曲</span>
        </div>
        <div class="stat-card">
          <span class="stat-value">{{ Math.round(summary.completion_rate * 100) }}%</span>
          <span class="stat-label">完成率</span>
        </div>
      </div>

      <div class="stats-tops">
        <div class="top-section">
          <h3>Top 歌曲</h3>
          <div v-for="(item, i) in topSongs" :key="i" class="top-item">
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
            <div class="top-cover placeholder artist"></div>
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
          <div v-for="item in timeline" :key="item.date" class="timeline-bar">
            <span class="bar-count">{{ item.count }}</span>
            <div class="bar-fill" :style="{ height: (item.count / maxTimelineCount * 100) + '%' }"></div>
            <span class="bar-label">{{ item.date.slice(5) }}</span>
          </div>
        </div>
      </div>

      <!-- Recent plays -->
      <div class="stats-recent">
        <h3>最近播放</h3>
        <div v-for="(item, i) in recent" :key="i" class="recent-item">
          <img v-if="item.cover_url" :src="item.cover_url" class="recent-cover" loading="lazy">
          <div v-else class="recent-cover placeholder"></div>
          <div class="recent-info">
            <span class="recent-name">{{ item.name }}</span>
            <span class="recent-sub">{{ item.singer }} · {{ item.album }}</span>
          </div>
          <div class="recent-meta">
            <span class="recent-time">{{ formatTimeAgo(item.played_at) }}</span>
            <span class="recent-detail">{{ formatDuration(item.listened_seconds) }} / {{ formatDuration(item.duration_seconds) }}</span>
            <span class="recent-badge" :class="{ completed: item.completed }">{{ item.completed ? '听完' : '跳过' }}</span>
          </div>
        </div>
        <p v-if="recent.length === 0" class="empty">暂无播放记录</p>
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
.top-cover {
  width: 36px;
  height: 36px;
  border-radius: 4px;
  object-fit: cover;
  flex-shrink: 0;
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

/* Recent plays */
.stats-recent {
  margin: 24px 0;
}
.stats-recent h3 {
  font-family: var(--font-serif);
  color: var(--ink);
  font-size: 14px;
  margin: 0 0 8px;
}
.recent-item {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 0;
  border-bottom: 1px solid var(--rule-soft);
}
.recent-cover {
  width: 40px;
  height: 40px;
  border-radius: 4px;
  object-fit: cover;
  flex-shrink: 0;
}
.recent-cover.placeholder {
  background: var(--paper-edge);
}
.recent-info {
  flex: 1;
  min-width: 0;
}
.recent-name {
  display: block;
  font-size: 13px;
  color: var(--ink);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.recent-sub {
  display: block;
  font-size: 11px;
  color: var(--ink-mute);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.recent-meta {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 2px;
}
.recent-time {
  font-size: 11px;
  color: var(--ink-mute);
}
.recent-detail {
  font-size: 10px;
  color: var(--ink-soft);
}
.recent-badge {
  font-size: 10px;
  padding: 1px 6px;
  border-radius: 3px;
  background: var(--rule);
  color: var(--ink-soft);
}
.recent-badge.completed {
  background: var(--accent);
  color: var(--paper);
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
