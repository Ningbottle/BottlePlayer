<script setup lang="ts">
import { ref, computed, watch, onMounted, onBeforeUpdate, onUnmounted } from 'vue';
import type { HomeViewModel } from './homeViewModel';
import type { Track } from '../../api/normalizer';
import type { PlaylistInfo } from '../../api/homeFeedStore';
import { animateElement, animateStagger, startAmbientMotion } from '../../api/motion';

const props = defineProps<{ model: HomeViewModel }>();

const emit = defineEmits<{
  (e: 'play-track', track: Track): void;
  (e: 'refresh'): void;
  (e: 'navigate', view: string, params?: any): void;
}>();

const coverError = ref(false);
const stageEl = ref<HTMLElement | null>(null);
const recommendationEls = ref<HTMLElement[]>([]);
const motionHandles: Array<{ kill(): void }> = [];

watch(() => props.model.heroTrack, () => { coverError.value = false; });

function setRecommendationRef(el: unknown): void {
  if (el instanceof HTMLElement) recommendationEls.value.push(el);
}

onBeforeUpdate(() => {
  recommendationEls.value = [];
});

onMounted(() => {
  if (stageEl.value) {
    motionHandles.push(
      animateElement(stageEl.value, { opacity: 0, y: 20 }, { opacity: 1, y: 0 }, 'pageEnter'),
    );
  }
  motionHandles.push(animateStagger(recommendationEls.value, 'cardEnter'));
  if (stageEl.value) {
    motionHandles.push(startAmbientMotion(stageEl.value, () => props.model.isPlaying));
  }
});

onUnmounted(() => {
  motionHandles.splice(0).forEach((handle) => handle.kill());
});

const heroCover = computed(() => {
  if (coverError.value) return '';
  return props.model.heroTrack?.Image || '';
});

const heroSource = computed(() => {
  const t = props.model.heroTrack;
  if (!t) return '';
  if (t.AlbumName) return t.AlbumName;
  return '每日推荐';
});

const queueCount = computed(() => {
  return props.model.queueTotal ?? props.model.queuePreview.length;
});

const displayedQueuePreview = computed(() => props.model.queuePreview.slice(0, 12));

function onHeroPlay() {
  const t = props.model.heroTrack;
  if (t) onTrackPlay(t);
}

function onTrackPlay(track: Track): void {
  emit('play-track', track);
}

function onOpenLyrics(): void {
  emit('navigate', 'lyric');
}

function isActiveQueueTrack(track: Track): boolean {
  return track.FileHash === props.model.activeQueueHash;
}

function onPlaylistClick(pl: PlaylistInfo) {
  emit('navigate', 'playlist', { id: pl.specialid, name: pl.specialname });
}

function onCoverError() {
  coverError.value = true;
}

function formatDuration(sec: number): string {
  const m = Math.floor(sec / 60);
  const s = sec % 60;
  return `${m}:${String(s).padStart(2, '0')}`;
}
</script>

<template>
  <div class="aurora-home">
    <!--
      Design composition (Codex 1586×1024):
      LEFT column defines height: cover+info → daily banner → DAILY PICKS
      RIGHT queue is a tall vertical panel matching LEFT height exactly (top→bottom of stage)
    -->
    <section
      ref="stageEl"
      class="aurora-stage"
      data-test="aurora-stage"
      :data-playing="model.isPlaying"
    >
      <div class="aurora-stage-left">
        <div class="aurora-stage-main">
          <div class="aurora-cover">
            <img
              v-if="heroCover"
              :src="heroCover"
              :alt="`${model.heroTrack?.SongName || '当前歌曲'}封面`"
              @error="onCoverError"
            />
            <div v-else class="aurora-cover-placeholder">封面暂缺</div>
          </div>

          <div class="aurora-info">
            <div class="aurora-label">
              <span class="aurora-label-dot" aria-hidden="true" />
              {{ model.isPlaying ? '正在播放' : '每日推荐' }}
            </div>
            <h1 class="aurora-song-name">{{ model.heroTrack?.SongName || '未在播放' }}</h1>
            <p class="aurora-artist">
              {{ model.heroTrack?.SingerName || '—' }}
              <span v-if="model.heroTrack" class="aurora-artist-chevron" aria-hidden="true">›</span>
            </p>
            <div class="aurora-quality-row" aria-label="音频信息">
              <span>无损</span>
              <span>96kHz / 24bit</span>
              <span>VIP</span>
            </div>
            <blockquote class="aurora-quote" v-if="model.heroTrack">
              <p>{{ model.heroTrack.AlbumName || heroSource || '用音乐填满此刻' }}</p>
              <p class="aurora-quote-accent">{{ model.heroTrack.SingerName }}</p>
              <p>{{ model.heroTrack.SongName }}</p>
            </blockquote>
            <div v-else class="aurora-quote aurora-quote-empty">
              <p>选择一首歌，开始沉浸聆听</p>
            </div>
            <div class="aurora-meta-row">
              <button class="aurora-play play-cta" data-test="hero-play" @click="onHeroPlay">
                <span aria-hidden="true">播放</span>
                <span class="sr-only">播放当前歌曲</span>
              </button>
              <button class="aurora-lyrics-link" type="button" @click="onOpenLyrics">查看歌词</button>
              <button
                class="aurora-refresh"
                data-test="refresh"
                :disabled="model.isRefreshing"
                @click="emit('refresh')"
              >
                {{ model.isRefreshing ? '刷新中…' : '刷新' }}
              </button>
            </div>
          </div>
        </div>

        <div class="aurora-daily-banner" v-if="model.dailyTracks.length || model.heroTrack">
          <span class="aurora-daily-pill">每日推荐</span>
          <p>根据你与「{{ model.heroTrack?.SingerName || '收藏' }}」的收听偏好，为你精选 · 每日合集</p>
        </div>

        <section
          v-if="model.dailyTracks.length > 0"
          class="aurora-recommendations"
          data-test="daily-picks"
        >
          <div class="aurora-section-head">
            <h2>DAILY PICKS · 今日推荐</h2>
            <button
              type="button"
              class="aurora-picks-refresh"
              :disabled="model.isRefreshing"
              @click="emit('refresh')"
            >
              {{ model.isRefreshing ? '刷新中…' : '刷新' }}
            </button>
          </div>
          <div class="aurora-recommendation-grid">
            <button
              v-for="track in model.dailyTracks.slice(0, 6)"
              :key="track.FileHash"
              :ref="setRecommendationRef"
              type="button"
              class="aurora-track-card"
              :data-test="`daily-track-${track.FileHash}`"
              @click="onTrackPlay(track)"
            >
              <span class="aurora-track-cover">
                <img v-if="track.Image" :src="track.Image" :alt="`${track.SongName}封面`" />
                <span v-else>推荐</span>
              </span>
              <strong>{{ track.SongName }}</strong>
              <small>{{ track.SingerName }}</small>
            </button>
          </div>
        </section>
      </div>

      <aside class="aurora-queue-rail" data-test="queue-rail" aria-label="播放队列">
        <header class="aurora-queue-rail-head">
          <h2>播放队列 <span>{{ queueCount }}</span></h2>
          <button type="button" class="aurora-queue-clear" disabled aria-label="清空播放队列">清空</button>
        </header>
        <ol v-if="displayedQueuePreview.length" class="aurora-queue-list">
          <li v-for="(track, index) in displayedQueuePreview" :key="track.FileHash" class="aurora-queue-row">
            <button
              type="button"
              :data-test="`queue-track-${track.FileHash}`"
              :class="{ 'is-active': isActiveQueueTrack(track) }"
              :aria-current="isActiveQueueTrack(track) ? 'true' : undefined"
              @click="onTrackPlay(track)"
            >
              <span class="aurora-queue-index">{{ String(index + 1).padStart(2, '0') }}</span>
              <span class="aurora-queue-copy"><b>{{ track.SongName }}</b><small>{{ track.SingerName }}</small></span>
              <span class="aurora-queue-duration">{{ formatDuration(track.Duration) }}</span>
            </button>
          </li>
        </ol>
        <div v-else class="aurora-queue-empty">暂无队列</div>
      </aside>
    </section>

    <div
      v-for="err in model.errors"
      :key="err.section"
      class="aurora-error"
    >
      {{ err.message }}
      <button class="aurora-retry" @click="emit('refresh')">重试</button>
    </div>

    <section v-if="model.playlists.length > 0" class="aurora-section">
      <div class="aurora-section-head">
        <h2>编辑推荐</h2>
        <span>{{ model.isRefreshing ? '刷新中…' : '本周精选' }}</span>
      </div>
      <div class="aurora-grid">
        <button
          v-for="pl in model.playlists"
          :key="pl.specialid"
          type="button"
          class="aurora-card"
          :data-test="`playlist-${pl.specialid}`"
          @click="onPlaylistClick(pl)"
        >
          <span class="aurora-card-cover">
            <img v-if="pl.imgurl" :src="pl.imgurl" :alt="`${pl.specialname}封面`" />
            <span v-else class="aurora-card-placeholder">歌单</span>
          </span>
          <span class="aurora-card-meta">
            <span class="aurora-card-title">{{ pl.specialname }}</span>
            <span class="aurora-card-sub">{{ pl.nickname }}</span>
          </span>
        </button>
      </div>
    </section>

    <section v-if="model.albums.length > 0" class="aurora-section">
      <div class="aurora-section-head">
        <h2>最新歌单</h2>
        <span>{{ model.isRefreshing ? '刷新中…' : '全部歌单' }}</span>
      </div>
      <div class="aurora-grid">
        <button
          v-for="pl in model.albums"
          :key="pl.specialid"
          type="button"
          class="aurora-card"
          :data-test="`playlist-${pl.specialid}`"
          @click="onPlaylistClick(pl)"
        >
          <span class="aurora-card-cover">
            <img v-if="pl.imgurl" :src="pl.imgurl" :alt="`${pl.specialname}封面`" />
            <span v-else class="aurora-card-placeholder">新碟</span>
          </span>
          <span class="aurora-card-meta">
            <span class="aurora-card-title">{{ pl.specialname }}</span>
            <span class="aurora-card-sub">{{ pl.nickname }}</span>
          </span>
        </button>
      </div>
    </section>
  </div>
</template>

<script lang="ts">
export default { name: 'AuroraHome' };
</script>

<style scoped>
/*
  Codex design (1586×1024):
  LEFT (defines height): cover+info → 每日推荐条 → DAILY PICKS 小专辑一排
  RIGHT: 竖长队列，高度严格 = 左侧整列（触底到舞台底，不是撑满整窗空白）
*/
.aurora-home {
  padding: 12px 22px 8px;
  box-sizing: border-box;
  min-height: 100%;
}

.aurora-stage {
  position: relative;
  /* Leave room for absolute queue on the right */
  padding-right: calc(332px + 24px);
  margin: 0;
  min-width: 0;
  box-sizing: border-box;
}

.aurora-stage-left {
  display: flex;
  flex-direction: column;
  gap: 14px;
  min-width: 0;
}

.aurora-stage-main {
  min-width: 0;
  display: grid;
  grid-template-columns: 340px minmax(0, 1fr);
  gap: 28px;
  align-items: start;
  padding: 4px 0 0;
  background:
    radial-gradient(ellipse 55% 65% at 70% 35%, color-mix(in srgb, var(--accent) 11%, transparent), transparent 62%);
}

.aurora-cover {
  aspect-ratio: 1;
  width: 340px;
  max-width: 100%;
  height: auto;
  border-radius: 16px;
  overflow: hidden;
  background: var(--surface-2);
  box-shadow:
    0 18px 40px rgba(0, 0, 0, 0.32),
    0 0 0 1px color-mix(in srgb, #fff 6%, transparent);
  flex: none;
}

.aurora-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.aurora-cover-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  font-size: 13px;
}

.aurora-info {
  display: flex;
  flex-direction: column;
  gap: 8px;
  min-width: 0;
  padding-top: 0;
}

.aurora-label {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  letter-spacing: 0.04em;
  color: var(--accent);
  font-weight: 500;
}

.aurora-label-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--accent);
  box-shadow: 0 0 10px color-mix(in srgb, var(--accent) 60%, transparent);
}

.aurora-song-name {
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  /* Spec §6.4: 48–56px desktop */
  font-size: clamp(40px, 3.6vw, 54px);
  font-weight: 700;
  line-height: 1.08;
  word-break: break-word;
  overflow-wrap: break-word;
  margin: 0;
  letter-spacing: -0.02em;
  color: var(--text-primary);
}

.aurora-artist {
  font-size: 20px;
  color: var(--text-secondary);
  margin: 0;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.aurora-artist-chevron {
  opacity: 0.55;
  font-size: 20px;
  line-height: 1;
}

.aurora-quote {
  margin: 6px 0 2px;
  padding: 0 0 0 2px;
  border: 0;
  color: var(--text-secondary);
  font-size: 14px;
  line-height: 1.55;
  max-width: 28em;
}

.aurora-quote p {
  margin: 0 0 2px;
}

.aurora-quote-accent {
  color: var(--accent) !important;
  font-weight: 500;
}

.aurora-quote-empty {
  color: var(--text-muted);
}

.aurora-daily-banner {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 2px 0 0;
  min-width: 0;
}

.aurora-daily-pill {
  flex: none;
  padding: 6px 12px;
  border-radius: 999px;
  background: color-mix(in srgb, var(--accent) 18%, var(--surface-2));
  border: 1px solid color-mix(in srgb, var(--accent) 40%, transparent);
  color: var(--accent);
  font-size: 12px;
  font-weight: 600;
}

.aurora-daily-banner p {
  margin: 0;
  font-size: 13px;
  color: var(--text-secondary);
  line-height: 1.45;
  min-width: 0;
}

.aurora-quality-row {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.aurora-quality-row span {
  padding: 3px 7px;
  border: 1px solid var(--border-subtle);
  border-radius: 5px;
  color: var(--text-secondary);
  font-size: 12px;
}

.aurora-quality-row span:first-child {
  border-color: color-mix(in srgb, var(--accent) 52%, transparent);
  color: var(--accent);
}

.aurora-meta-row {
  display: flex;
  align-items: center;
  gap: 14px;
  margin-top: 6px;
  flex-wrap: wrap;
}

.aurora-play {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 11px 22px;
  border-radius: 999px;
  border: none;
  background: var(--accent);
  color: #07120e;
  font-size: 14px;
  font-weight: 700;
  cursor: pointer;
  transition: filter 0.2s ease, box-shadow 0.2s ease;
  flex-shrink: 0;
  box-shadow: 0 6px 18px color-mix(in srgb, var(--accent) 35%, transparent);
}

.aurora-play:hover { filter: brightness(1.06); }
.aurora-play:active { filter: brightness(0.96); }

.aurora-lyrics-link {
  border: 0;
  background: transparent;
  color: var(--text-secondary);
  cursor: pointer;
  font: inherit;
  padding: 8px 0;
}

.aurora-lyrics-link:hover { color: var(--accent); }

.aurora-refresh {
  display: inline-flex;
  align-items: center;
  padding: 8px 16px;
  border-radius: 20px;
  border: 1px solid var(--border-subtle);
  background: transparent;
  color: var(--text-secondary);
  font-size: 13px;
  cursor: pointer;
  transition: background 0.2s ease;
}

.aurora-refresh:hover {
  background: var(--surface-1);
}

.aurora-refresh:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

/*
  Queue = absolute tall vertical rect pinned to stage left column height.
  top/bottom:0 → 与左侧同高并触到舞台底（设计图竖长列表）。
*/
.aurora-queue-rail {
  position: absolute;
  top: 0;
  right: 0;
  bottom: 0;
  width: 332px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  border: 1px solid color-mix(in srgb, var(--text-primary) 8%, transparent);
  border-radius: 14px;
  background: color-mix(in srgb, var(--surface-2) 88%, transparent);
  padding: 14px 12px 12px;
  box-sizing: border-box;
}

.aurora-queue-rail-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  padding: 0 4px 12px;
  border-bottom: 1px solid color-mix(in srgb, var(--text-primary) 7%, transparent);
  flex: none;
}

.aurora-queue-rail-head h2 {
  margin: 0;
  font-size: 14px;
  font-weight: 600;
  color: var(--text-primary);
}

.aurora-queue-rail-head h2 span {
  color: var(--text-muted);
  font-size: 12px;
  font-weight: 500;
  margin-left: 4px;
}

.aurora-queue-clear {
  border: 0;
  background: transparent;
  color: var(--text-muted);
  font: inherit;
  font-size: 12px;
  cursor: default;
}

.aurora-queue-list {
  list-style: none;
  margin: 0;
  padding: 4px 0 0;
  display: flex;
  flex-direction: column;
  flex: 1 1 auto;
  min-height: 0;
  overflow-x: hidden;
  overflow-y: auto;
  gap: 0;
  /* Prevent flex min-content from blowing open the rail */
  overscroll-behavior: contain;
}

.aurora-queue-row {
  border-bottom: 0;
  flex: none;
}

.aurora-queue-row button {
  width: 100%;
  display: grid;
  grid-template-columns: 22px minmax(0, 1fr) auto;
  gap: 8px;
  align-items: center;
  padding: 9px 6px;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: inherit;
  cursor: pointer;
  text-align: left;
}

.aurora-queue-row button:hover,
.aurora-queue-row button:focus-visible {
  background: color-mix(in srgb, var(--text-primary) 5%, transparent);
  color: var(--text-primary);
  outline: none;
}

.aurora-queue-row button.is-active {
  color: var(--accent);
  background: color-mix(in srgb, var(--accent) 8%, transparent);
}

.aurora-queue-index,
.aurora-queue-duration {
  color: var(--text-muted);
  font-size: 11px;
  font-variant-numeric: tabular-nums;
}

.aurora-queue-copy {
  display: grid;
  min-width: 0;
  gap: 1px;
}

.aurora-queue-copy b,
.aurora-queue-copy small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.aurora-queue-copy b {
  font-size: 12.5px;
  font-weight: 600;
}

.aurora-queue-copy small {
  color: var(--text-muted);
  font-size: 11px;
}

.aurora-queue-row button.is-active .aurora-queue-copy small {
  color: color-mix(in srgb, var(--accent) 80%, var(--text-muted));
}

.aurora-queue-empty {
  display: grid;
  place-items: center;
  flex: 1;
  min-height: 200px;
  color: var(--text-muted);
  font-size: 13px;
}

/* Spec §6.5: DAILY PICKS — 6 small albums in a tight row under hero */
.aurora-recommendations {
  margin: 0;
  min-width: 0;
  padding: 4px 0 0;
}

.aurora-recommendations .aurora-section-head {
  margin-bottom: 12px;
  display: flex;
  align-items: baseline;
  justify-content: space-between;
}

.aurora-recommendations .aurora-section-head h2 {
  font-size: 13px;
  font-weight: 600;
  letter-spacing: 0.06em;
  color: var(--text-secondary);
  margin: 0;
}

.aurora-picks-refresh {
  border: 0;
  background: transparent;
  color: var(--text-muted);
  font: inherit;
  font-size: 12px;
  cursor: pointer;
  padding: 0;
}

.aurora-picks-refresh:hover {
  color: var(--accent);
}

.aurora-recommendation-grid {
  display: grid;
  grid-template-columns: repeat(6, minmax(0, 1fr));
  gap: 14px;
  max-width: 780px;
}

.aurora-track-card,
.aurora-card {
  min-width: 0;
  padding: 0;
  border: 0;
  background: transparent;
  color: var(--text-primary);
  cursor: pointer;
  text-align: left;
}

.aurora-track-card {
  display: grid;
  gap: 6px;
}

.aurora-track-card strong,
.aurora-track-card small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.aurora-track-card strong { font-size: 12px; font-weight: 600; }
.aurora-track-card small { color: var(--text-muted); font-size: 11px; }

.aurora-track-cover {
  display: grid;
  aspect-ratio: 1;
  width: 100%;
  overflow: hidden;
  border-radius: 10px;
  background: var(--surface-2);
  box-shadow: 0 6px 16px rgba(0, 0, 0, 0.24);
}

.aurora-track-cover img { width: 100%; height: 100%; object-fit: cover; display: block; }
.aurora-track-cover > span { display: grid; place-items: center; color: var(--text-muted); font-size: 11px; }

.aurora-track-card:hover .aurora-track-cover,
.aurora-track-card:focus-visible .aurora-track-cover { transform: translateY(-2px); }

.aurora-track-cover { transition: transform 0.2s ease; }

.sr-only {
  position: absolute;
  width: 1px;
  height: 1px;
  padding: 0;
  margin: -1px;
  overflow: hidden;
  clip: rect(0, 0, 0, 0);
  white-space: nowrap;
  border: 0;
}

.aurora-error {
  padding: 10px 14px;
  margin-bottom: 8px;
  border-radius: 8px;
  background: var(--surface-1);
  color: var(--accent);
  font-size: 13px;
  display: flex;
  align-items: center;
  gap: 12px;
}

.aurora-retry {
  background: none;
  border: none;
  color: var(--accent);
  cursor: pointer;
  font-size: 13px;
  text-decoration: underline;
}

.aurora-section {
  flex: none;
  margin-top: 10px;
  margin-bottom: 0;
  overflow: auto;
  min-height: 0;
}

.aurora-section-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  margin-bottom: 14px;
}

.aurora-section-head h3 {
  font-size: 18px;
  font-weight: 700;
  margin: 0;
}

.aurora-section-head h2 {
  font-size: 18px;
  font-weight: 700;
  margin: 0;
}

.aurora-section-head span {
  font-size: 13px;
  color: var(--text-secondary);
}

.aurora-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
  gap: 16px;
}

.aurora-card {
  transition: transform 0.2s ease;
}

.aurora-card:hover {
  transform: translateY(-2px);
}

.aurora-card-cover {
  display: grid;
  aspect-ratio: 1;
  border-radius: 10px;
  overflow: hidden;
  background: var(--surface-1);
  margin-bottom: 8px;
}

.aurora-card-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.aurora-card-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  color: var(--text-secondary);
}

.aurora-card-cover > img { width: 100%; height: 100%; object-fit: cover; }

.aurora-card-meta { display: grid; gap: 2px; }

.aurora-card-title {
  font-size: 13px;
  font-weight: 600;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.aurora-card-sub {
  font-size: 12px;
  color: var(--text-secondary);
}

@media (max-width: 1359px) {
  .aurora-stage {
    padding-right: calc(300px + 20px);
  }

  .aurora-queue-rail {
    width: 300px;
  }

  .aurora-stage-main {
    grid-template-columns: minmax(260px, 300px) minmax(0, 1fr);
  }

  .aurora-cover {
    width: 100%;
    max-width: 300px;
  }
}

@media (max-width: 1099px) {
  .aurora-stage {
    padding-right: 0;
  }

  .aurora-queue-rail {
    display: none;
  }

  .aurora-recommendation-grid {
    grid-auto-flow: column;
    grid-auto-columns: minmax(100px, 120px);
    grid-template-columns: none;
    overflow-x: auto;
    max-width: none;
    padding-bottom: 4px;
  }
}

@media (max-width: 899px) {
  .aurora-stage-main {
    grid-template-columns: 1fr;
    text-align: center;
  }

  .aurora-cover {
    width: min(70vw, 300px);
    max-width: 300px;
    margin-inline: auto;
  }

  .aurora-meta-row,
  .aurora-daily-banner {
    justify-content: center;
    text-align: center;
  }

  .aurora-quote {
    margin-inline: auto;
  }
}
</style>
