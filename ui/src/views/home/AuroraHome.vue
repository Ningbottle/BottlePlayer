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
    <section
      ref="stageEl"
      class="aurora-stage"
      data-test="aurora-stage"
      :data-playing="model.isPlaying"
    >
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
          <div class="aurora-label">{{ model.isPlaying ? '正在播放' : '每日推荐' }}</div>
          <h1 class="aurora-song-name">{{ model.heroTrack?.SongName || '未在播放' }}</h1>
          <p class="aurora-artist">{{ model.heroTrack?.SingerName || '—' }}</p>
          <p class="aurora-source">{{ heroSource }}</p>
          <div class="aurora-quality-row" aria-label="音频信息">
            <span>无损</span>
            <span>96kHz / 24bit</span>
            <span>VIP</span>
          </div>
          <div class="aurora-now-playing-copy">
            <p>当前播放</p>
            <strong>{{ model.heroTrack?.AlbumName || '每日推荐歌单' }}</strong>
            <span>{{ model.heroTrack?.SingerName || '用音乐填满此刻' }}</span>
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

    <section v-if="model.dailyTracks.length > 0" class="aurora-recommendations">
      <div class="aurora-section-head">
        <h2>DAILY PICKS · 今日推荐</h2>
        <span>{{ model.isRefreshing ? '刷新中…' : '刷新推荐' }}</span>
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
.aurora-home {
  padding: 24px 28px 36px;
  min-height: 100%;
}

.aurora-stage {
  display: grid;
  grid-template-columns: minmax(0, 1.18fr) minmax(280px, 0.82fr);
  gap: clamp(20px, 2vw, 32px);
  align-items: stretch;
  margin-bottom: 30px;
  min-height: 430px;
  min-width: 0;
}

.aurora-stage-main {
  min-width: 0;
  display: grid;
  grid-template-columns: minmax(250px, 0.86fr) minmax(320px, 1.12fr);
  gap: clamp(20px, 2vw, 32px);
  align-items: center;
  padding: clamp(12px, 2vw, 26px);
  border-radius: 22px;
  background: radial-gradient(circle at 94% 56%, color-mix(in srgb, var(--accent) 18%, transparent), transparent 39%), var(--surface-1);
}

.aurora-cover {
  aspect-ratio: 1;
  width: 100%;
  max-width: 340px;
  border-radius: 18px;
  overflow: hidden;
  background: var(--surface-2);
  box-shadow: 0 18px 38px rgba(0, 0, 0, 0.24);
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
  gap: 10px;
  min-width: 0;
}

.aurora-label {
  font-size: 13px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--accent);
}

.aurora-song-name {
  font-family: Georgia, 'Noto Serif SC', serif;
  font-size: clamp(38px, 4vw, 58px);
  font-weight: 700;
  line-height: 1.06;
  word-break: break-word;
  overflow-wrap: break-word;
  margin: 0;
}

.aurora-artist {
  font-size: 22px;
  color: var(--text-secondary);
  margin: 0;
}

.aurora-source {
  font-size: 13px;
  color: var(--text-tertiary, var(--text-secondary));
  margin: 0;
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

.aurora-now-playing-copy {
  display: grid;
  gap: 4px;
  margin-top: 8px;
  color: var(--text-secondary);
}

.aurora-now-playing-copy p,
.aurora-now-playing-copy strong,
.aurora-now-playing-copy span {
  margin: 0;
}

.aurora-now-playing-copy p {
  color: var(--text-muted);
  font-size: 12px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.aurora-now-playing-copy strong {
  color: var(--text-primary);
  font-size: 17px;
}

.aurora-meta-row {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-top: 8px;
  flex-wrap: wrap;
}

.aurora-queue-count {
  font-size: 13px;
  color: var(--text-secondary);
}

.aurora-play {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 10px 20px;
  border-radius: 24px;
  border: none;
  background: var(--accent);
  color: var(--on-accent, #fff);
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
  transition: transform 0.2s ease, box-shadow 0.2s ease;
  flex-shrink: 0;
}

.aurora-play:hover { transform: translateY(-2px); }
.aurora-play:active { transform: scale(0.95); }

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

.aurora-queue-rail {
  min-width: 0;
  min-height: 0;
  overflow: hidden;
  border: 1px solid var(--border-subtle);
  border-radius: 16px;
  background: var(--surface-1);
  padding: 16px;
}

.aurora-queue-rail-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--border-subtle);
}

.aurora-queue-rail-head h2 {
  margin: 0;
  font-size: 16px;
}

.aurora-queue-rail-head h2 span { color: var(--text-muted); font-size: 13px; }

.aurora-queue-clear {
  border: 0;
  background: transparent;
  color: var(--text-muted);
  font: inherit;
  font-size: 12px;
}

.aurora-queue-list {
  list-style: none;
  margin: 8px 0 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  max-height: 390px;
  overflow: auto;
}

.aurora-queue-row {
  border-bottom: 1px solid var(--border-subtle);
}

.aurora-queue-row button {
  width: 100%;
  display: grid;
  grid-template-columns: 28px minmax(0, 1fr) auto;
  gap: 9px;
  align-items: center;
  padding: 10px 0;
  border: 0;
  background: transparent;
  color: inherit;
  cursor: pointer;
  text-align: left;
}

.aurora-queue-row button:hover,
.aurora-queue-row button:focus-visible { color: var(--accent); outline: none; }

.aurora-queue-row button.is-active { color: var(--accent); }

.aurora-queue-index,
.aurora-queue-duration { color: var(--text-muted); font-size: 12px; font-variant-numeric: tabular-nums; }

.aurora-queue-copy { display: grid; min-width: 0; gap: 2px; }
.aurora-queue-copy b,
.aurora-queue-copy small { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.aurora-queue-copy b { font-size: 13px; font-weight: 600; }
.aurora-queue-copy small { color: var(--text-muted); font-size: 12px; }

.aurora-queue-row button.is-active .aurora-queue-copy small { color: var(--accent); }

.aurora-queue-empty {
  display: grid;
  place-items: center;
  min-height: 180px;
  color: var(--text-muted);
}

.aurora-recommendations { margin-bottom: 34px; }

.aurora-recommendation-grid {
  display: grid;
  grid-template-columns: repeat(6, minmax(0, 1fr));
  gap: 14px;
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
  gap: 7px;
}

.aurora-track-card strong,
.aurora-track-card small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.aurora-track-card strong { font-size: 13px; }
.aurora-track-card small { color: var(--text-secondary); font-size: 12px; }

.aurora-track-cover {
  display: grid;
  aspect-ratio: 1;
  overflow: hidden;
  border-radius: 12px;
  background: var(--surface-2);
}

.aurora-track-cover img { width: 100%; height: 100%; object-fit: cover; }
.aurora-track-cover > span { display: grid; place-items: center; color: var(--text-muted); font-size: 12px; }

.aurora-track-card:hover .aurora-track-cover,
.aurora-track-card:focus-visible .aurora-track-cover { transform: translateY(-3px); }

.aurora-track-cover { transition: transform 0.24s ease; }

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
  margin-bottom: 28px;
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

@media (max-width: 1099px) {
  .aurora-stage {
    grid-template-columns: minmax(0, 1fr);
  }

  .aurora-queue-rail {
    display: none;
  }

  .aurora-recommendation-grid {
    grid-auto-flow: column;
    grid-auto-columns: minmax(132px, 1fr);
    grid-template-columns: none;
    overflow-x: auto;
    padding-bottom: 6px;
  }
}

@media (max-width: 899px) {
  .aurora-stage {
    grid-template-columns: 1fr;
  }

  .aurora-stage-main {
    grid-template-columns: 1fr;
    text-align: center;
  }

  .aurora-cover {
    width: min(72vw, 320px);
    max-width: 320px;
    margin-inline: auto;
  }

  .aurora-meta-row {
    justify-content: center;
  }
}
</style>
