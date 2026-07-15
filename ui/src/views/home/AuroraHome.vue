<script setup lang="ts">
import { ref, computed, watch, onMounted, onBeforeUpdate, onUnmounted, nextTick } from 'vue';
import type { HomeViewModel } from './homeViewModel';
import type { Track } from '../../api/normalizer';
import type { PlaylistInfo } from '../../api/homeFeedStore';
import { gsap } from 'gsap';
import type { HomeEnterMode } from '../../api/homeEnterSession';
import { animateStagger, startAmbientMotion, isReducedMotion } from '../../api/motion';
import AuroraAtmosphere from './AuroraAtmosphere.vue';

const props = withDefaults(
  defineProps<{
    model: HomeViewModel;
    enterMode?: HomeEnterMode | 'none';
    enterNonce?: number;
  }>(),
  {
    enterMode: 'none',
    enterNonce: 0,
  },
);

const emit = defineEmits<{
  (e: 'play-track', track: Track): void;
  (e: 'play-queue-track', track: Track): void;
  (e: 'refresh'): void;
  (e: 'navigate', view: string, params?: any): void;
  (e: 'clear-queue'): void;
}>();

const coverError = ref(false);
const stageEl = ref<HTMLElement | null>(null);
const recommendationEls = ref<HTMLElement[]>([]);
/** Stage + stagger enter handles (killed on re-enter). */
const enterHandles: Array<{ kill(): void }> = [];
/** Ambient only — started once, not replayed on return. */
const ambientHandles: Array<{ kill(): void }> = [];

watch(() => props.model.heroTrack, () => { coverError.value = false; });

function setRecommendationRef(el: unknown): void {
  if (el instanceof HTMLElement) recommendationEls.value.push(el);
}

onBeforeUpdate(() => {
  recommendationEls.value = [];
});

function killEnterHandles(): void {
  enterHandles.splice(0).forEach((handle) => handle.kill());
}

/**
 * Cold vs return home enter. Budgets (boosted for stronger Aurora feel):
 * - cold: stage ~0.72s expo.out, stagger max 14, larger fromY
 * - return: stage ~0.36s, stagger max 8
 * - reduced: set only (via motion helpers)
 *
 * Deferred to nextTick so stage/recommendation refs exist after mount / KeepAlive activate.
 */
function playHomeEnter(): void {
  const mode = props.enterMode;
  if (mode === 'none') return;

  killEnterHandles();

  const isCold = mode === 'cold';
  const stageFromY = isCold ? 36 : 16;
  const stageDuration = isCold ? 0.72 : 0.36;
  const staggerOptions = isCold
    ? { duration: 0.52, stagger: 0.055, maxItems: 14, fromY: 32 }
    : { duration: 0.32, stagger: 0.035, maxItems: 8, fromY: 16 };
  const nonceAtSchedule = props.enterNonce;

  void nextTick(() => {
    // Drop if a newer enter superseded this schedule, or mode cleared.
    if (props.enterMode === 'none' || props.enterNonce !== nonceAtSchedule) return;

    if (stageEl.value) {
      const el = stageEl.value;
      gsap.killTweensOf(el);
      if (isReducedMotion()) {
        gsap.set(el, { opacity: 1, y: 0 });
      } else {
        const tween = gsap.fromTo(
          el,
          { opacity: 0, y: stageFromY },
          { opacity: 1, y: 0, duration: stageDuration, ease: 'expo.out' },
        );
        enterHandles.push({
          kill: () => {
            tween.kill();
            gsap.killTweensOf(el);
          },
        });
      }
    }

    enterHandles.push(animateStagger(recommendationEls.value, 'cardEnter', staggerOptions));
  });
}

watch(
  [() => props.enterMode, () => props.enterNonce],
  () => {
    playHomeEnter();
  },
  { immediate: true, flush: 'post' },
);

onMounted(() => {
  if (stageEl.value) {
    ambientHandles.push(startAmbientMotion(stageEl.value, () => props.model.isPlaying));
  }
});

onUnmounted(() => {
  killEnterHandles();
  ambientHandles.splice(0).forEach((handle) => handle.kill());
});

const heroCover = computed(() => {
  if (coverError.value) return '';
  return props.model.heroTrack?.Image || '';
});

/** Album line only when it adds info beyond the song title (avoid triple-repeat clutter). */
const heroAlbumLine = computed(() => {
  const t = props.model.heroTrack;
  if (!t?.AlbumName) return '';
  const album = t.AlbumName.trim();
  const song = (t.SongName || '').trim();
  if (!album || album === song) return '';
  return album;
});

const queueCount = computed(() => {
  return props.model.queueTotal ?? props.model.queuePreview.length;
});

const isQueueHovered = ref(false);
const queueSnapshot = ref<readonly Track[]>(props.model.queuePreview.slice(0, 12));
const queueSnapshotStart = ref(props.model.queueWindowStart);
const queueSnapshotActiveHash = ref(props.model.activeQueueHash);

function syncQueueSnapshot(): void {
  queueSnapshot.value = props.model.queuePreview.slice(0, 12);
  queueSnapshotStart.value = props.model.queueWindowStart;
  queueSnapshotActiveHash.value = props.model.activeQueueHash;
}

watch(
  [
    () => props.model.queuePreview,
    () => props.model.queueWindowStart,
    () => props.model.activeQueueHash,
  ],
  () => {
    if (!isQueueHovered.value) syncQueueSnapshot();
  },
  { immediate: true },
);

function freezeQueueFollow(): void {
  isQueueHovered.value = true;
}

function resumeQueueFollow(): void {
  isQueueHovered.value = false;
  syncQueueSnapshot();
}

const displayedQueuePreview = computed(() => queueSnapshot.value);

const emptyQueueSuggestions = computed(() => {
  const daily = props.model.dailyTracks ?? [];
  return (Array.isArray(daily) ? daily : []).slice(0, 3);
});

function onHeroPlay() {
  const t = props.model.heroTrack;
  if (t) onTrackPlay(t);
}

function onTrackPlay(track: Track): void {
  emit('play-track', track);
}

function onQueueTrackPlay(track: Track): void {
  emit('play-queue-track', track);
}

function onOpenLyrics(): void {
  emit('navigate', 'lyric');
}

function isActiveQueueTrack(track: Track): boolean {
  return track.FileHash === queueSnapshotActiveHash.value;
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
      Layout:
      1) Stage hero row = cover/info | queue (grid, not absolute) — no dead gutter under rail
      2) Full-width DAILY PICKS / 编辑推荐 / 最新歌单 below — fill content width
    -->
    <section
      ref="stageEl"
      class="aurora-stage"
      data-test="aurora-stage"
      :data-playing="model.isPlaying"
    >
      <AuroraAtmosphere :is-playing="model.isPlaying" />
      <div class="aurora-stage-hero">
        <div v-if="model.heroTrack" class="aurora-stage-main">
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
            <h1 class="aurora-song-name">{{ model.heroTrack.SongName }}</h1>
            <p class="aurora-artist">
              {{ model.heroTrack.SingerName }}
              <span class="aurora-artist-chevron" aria-hidden="true">›</span>
            </p>
            <div
              v-if="model.heroQualityChips.length"
              class="aurora-quality-row"
              data-test="hero-quality-chips"
              aria-label="音频信息"
            >
              <span v-for="chip in model.heroQualityChips" :key="chip">{{ chip }}</span>
            </div>
            <p v-if="heroAlbumLine" class="aurora-album-line" data-test="hero-album-line">
              {{ heroAlbumLine }}
            </p>
            <div class="aurora-meta-row">
              <button class="aurora-play play-cta" data-test="hero-play" @click="onHeroPlay">
                <span aria-hidden="true">播放</span>
                <span class="sr-only">播放当前歌曲</span>
              </button>
              <button class="aurora-lyrics-link" type="button" @click="onOpenLyrics">查看歌词</button>
              <button
                class="aurora-refresh"
                data-test="refresh"
                @click="emit('refresh')"
              >
                刷新
              </button>
            </div>
          </div>
        </div>

        <div
          v-else-if="model.sections.daily.loading"
          class="aurora-stage-main aurora-stage-loading"
          data-test="aurora-stage-loading"
          aria-busy="true"
          aria-live="polite"
        >
          <div class="aurora-cover aurora-cover-skeleton" aria-hidden="true" />
          <div class="aurora-info aurora-info-skeleton">
            <p class="aurora-label"><span class="aurora-label-dot" aria-hidden="true" />正在加载推荐</p>
            <span class="aurora-skeleton-line aurora-skeleton-title" aria-hidden="true" />
            <span class="aurora-skeleton-line aurora-skeleton-copy" aria-hidden="true" />
          </div>
        </div>

        <div v-else class="aurora-stage-empty" data-test="aurora-stage-empty">
          <p class="aurora-label"><span class="aurora-label-dot" aria-hidden="true" />还没有开始播放</p>
          <h1>选择一首歌，开始沉浸聆听</h1>
          <p>从每日推荐或左侧歌单开始，舞台会随播放状态展开。</p>
          <button
            type="button"
            class="aurora-play"
            data-test="empty-stage-refresh"
            :disabled="model.sections.daily.loading || model.sections.daily.refreshing"
            @click="model.sections.daily.retry()"
          >
            {{ model.sections.daily.error ? '重试' : model.sections.daily.refreshing ? '刷新中…' : '刷新推荐' }}
          </button>
        </div>

        <aside class="aurora-queue-rail" data-test="queue-rail" aria-label="播放队列">
          <header class="aurora-queue-rail-head">
            <h2>播放队列 <span>{{ queueCount }}</span></h2>
            <button
              type="button"
              class="aurora-queue-clear"
              data-test="queue-clear"
              :disabled="!model.queueTotal"
              aria-label="清空播放队列"
              @click="emit('clear-queue')"
            >清空</button>
          </header>
          <ol
            v-if="displayedQueuePreview.length"
            class="aurora-queue-list"
            @mouseenter="freezeQueueFollow"
            @mouseleave="resumeQueueFollow"
          >
            <li v-for="(track, index) in displayedQueuePreview" :key="track.FileHash" class="aurora-queue-row">
              <button
                type="button"
                :data-test="`queue-track-${track.FileHash}`"
                :class="{ 'is-active': isActiveQueueTrack(track) }"
                :aria-current="isActiveQueueTrack(track) ? 'true' : undefined"
                @click="onQueueTrackPlay(track)"
              >
                <span class="aurora-queue-index">{{ String(queueSnapshotStart + index + 1).padStart(2, '0') }}</span>
                <span class="aurora-queue-copy"><b>{{ track.SongName }}</b><small>{{ track.SingerName }}</small></span>
                <span class="aurora-queue-duration">{{ formatDuration(track.Duration) }}</span>
              </button>
            </li>
          </ol>
          <div
            v-else
            class="aurora-queue-empty"
            data-test="queue-empty-state"
          >
            <p class="aurora-queue-empty-title">队列还是空的</p>
            <p class="aurora-queue-empty-hint">播放每日推荐或歌单后，曲目会出现在这里</p>
            <ul
              v-if="emptyQueueSuggestions.length"
              class="aurora-queue-suggestions"
            >
              <li v-for="track in emptyQueueSuggestions" :key="track.FileHash">
                <button type="button" @click="onTrackPlay(track)">
                  {{ track.SongName }}
                  <small>{{ track.SingerName }}</small>
                </button>
              </li>
            </ul>
          </div>
        </aside>
      </div>
    </section>

    <section
      v-if="model.dailyTracks.length > 0 || model.sections.daily.loading || model.sections.daily.error || model.sections.daily.isEmpty"
      class="aurora-recommendations"
      data-test="daily-picks"
    >
      <div class="aurora-section-head">
        <div class="aurora-section-head-copy">
          <h2>今日推荐 · DAILY PICKS</h2>
          <p class="aurora-section-sub">
            根据你与「{{ model.heroTrack?.SingerName || '收藏' }}」的收听偏好精选
          </p>
        </div>
        <button
          type="button"
          class="aurora-picks-refresh"
          data-test="daily-section-retry"
          :disabled="model.sections.daily.loading || model.sections.daily.refreshing"
          @click="model.sections.daily.retry()"
        >
          {{ model.sections.daily.error ? '重试' : model.sections.daily.loading ? '加载中…' : model.sections.daily.refreshing ? '刷新中…' : model.sections.daily.isEmpty ? '暂无内容' : '刷新' }}
        </button>
      </div>
      <div class="aurora-recommendation-grid">
        <button
          v-for="track in model.dailyTracks.slice(0, 18)"
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

    <div
      v-if="model.errors.length"
      class="aurora-error-summary"
      data-test="home-error-summary"
      role="alert"
    >
      <p>{{ model.errorSummary || '部分内容加载失败' }}</p>
      <button
        type="button"
        class="aurora-retry"
        data-test="home-error-retry-all"
        @click="emit('refresh')"
      >
        全部重试
      </button>
    </div>

    <section v-if="model.playlists.length > 0 || model.sections.playlists.loading || model.sections.playlists.error || model.sections.playlists.isEmpty" class="aurora-section">
      <div class="aurora-section-head">
        <h2>编辑推荐</h2>
        <button
          v-if="model.sections.playlists.error"
          type="button"
          class="aurora-picks-refresh"
          data-test="playlists-section-retry"
          @click="model.sections.playlists.retry()"
        >重试</button>
        <span v-else data-test="playlists-section-status">
          {{ model.sections.playlists.loading ? '加载中…' : model.sections.playlists.refreshing ? '刷新中…' : model.sections.playlists.isEmpty ? '暂无内容' : '本周精选' }}
        </span>
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

    <section v-if="model.albums.length > 0 || model.sections.albums.loading || model.sections.albums.error || model.sections.albums.isEmpty" class="aurora-section">
      <div class="aurora-section-head">
        <h2>最新歌单</h2>
        <button
          v-if="model.sections.albums.error"
          type="button"
          class="aurora-picks-refresh"
          data-test="albums-section-retry"
          @click="model.sections.albums.retry()"
        >重试</button>
        <span v-else data-test="albums-section-status">
          {{ model.sections.albums.loading ? '加载中…' : model.sections.albums.refreshing ? '刷新中…' : model.sections.albums.isEmpty ? '暂无内容' : '全部歌单' }}
        </span>
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
  Hero row: cover+info | queue (CSS grid, equal height)
  Below: full-width DAILY PICKS / 编辑推荐 / 最新歌单 (no rail gutter)
*/
.aurora-home {
  padding: 12px clamp(16px, 2vw, 28px) 18px;
  box-sizing: border-box;
  min-height: 100%;
  max-width: 100%;
  overflow-x: hidden;
  display: flex;
  flex-direction: column;
  /* Tight stack: hero → DAILY PICKS (no tall dead gap) */
  gap: 12px;
}

.aurora-stage {
  position: relative;
  z-index: 1;
  margin: 0;
  min-width: 0;
  isolation: isolate;
  box-sizing: border-box;
  flex: none;
}

.aurora-stage-hero {
  position: relative;
  z-index: 1;
  display: grid;
  grid-template-columns: minmax(0, 1fr) minmax(280px, 320px);
  gap: 18px;
  /* Match queue to cover height; cover owns the row height */
  align-items: stretch;
  min-width: 0;
}

.aurora-stage-main {
  min-width: 0;
  display: grid;
  grid-template-columns: minmax(240px, 320px) minmax(0, 1fr);
  gap: 22px;
  align-items: center;
  padding: 0;
  background:
    radial-gradient(ellipse 55% 65% at 70% 35%, color-mix(in srgb, var(--accent) 11%, transparent), transparent 62%);
}

.aurora-stage-empty {
  min-height: 280px;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  justify-content: center;
  gap: 12px;
  padding: 28px clamp(20px, 4vw, 48px);
  border: 1px solid color-mix(in srgb, var(--accent) 22%, transparent);
  border-radius: 20px;
  background: color-mix(in srgb, var(--surface-2) 78%, transparent);
  box-shadow: 0 20px 54px color-mix(in srgb, var(--accent) 10%, transparent);
  min-width: 0;
}

.aurora-stage-loading {
  min-height: 320px;
  pointer-events: none;
}

.aurora-cover-skeleton,
.aurora-skeleton-line {
  background: linear-gradient(
    112deg,
    color-mix(in srgb, var(--surface-2) 94%, var(--text-primary) 6%),
    color-mix(in srgb, var(--surface-2) 82%, var(--text-primary) 18%),
    color-mix(in srgb, var(--surface-2) 94%, var(--text-primary) 6%)
  );
}

.aurora-info-skeleton {
  min-width: 0;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  justify-content: center;
  gap: 16px;
}

.aurora-info-skeleton .aurora-label {
  margin: 0;
}

.aurora-skeleton-line {
  display: block;
  height: 14px;
  border-radius: 999px;
}

.aurora-skeleton-title {
  width: min(62%, 310px);
  height: 42px;
}

.aurora-skeleton-copy {
  width: min(42%, 220px);
}

.aurora-stage-empty h1,
.aurora-stage-empty p {
  margin: 0;
}

.aurora-stage-empty h1 {
  max-width: min(20ch, 100%);
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  font-size: clamp(30px, 3vw, 46px);
  line-height: 1.2;
  color: var(--text-primary);
  text-wrap: balance;
}

.aurora-stage-empty > p:not(.aurora-label) {
  max-width: 36rem;
  color: var(--text-secondary);
  line-height: 1.65;
}

.aurora-cover {
  aspect-ratio: 1;
  width: 100%;
  max-width: 320px;
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
  justify-content: center;
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
  font-size: clamp(30px, 3vw, 44px);
  font-weight: 700;
  line-height: 1.1;
  word-break: break-word;
  overflow-wrap: break-word;
  margin: 0;
  letter-spacing: -0.02em;
  color: var(--text-primary);
}

.aurora-artist {
  font-size: 18px;
  color: var(--text-secondary);
  margin: 0;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.aurora-artist-chevron {
  opacity: 0.55;
  font-size: 18px;
  line-height: 1;
}

/* Single album subtitle — no re-listing artist/song under the title */
.aurora-album-line {
  margin: 2px 0 0;
  max-width: 36em;
  color: var(--text-muted);
  font-size: 13px;
  line-height: 1.45;
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
  margin-top: 10px;
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

/* Queue matches cover row height (capped so empty state can't invent ~600px blank) */
.aurora-queue-rail {
  position: relative;
  z-index: 1;
  width: auto;
  min-width: 0;
  min-height: 0;
  max-height: 320px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  border: 1px solid color-mix(in srgb, var(--text-primary) 8%, transparent);
  border-radius: 14px;
  background: color-mix(in srgb, var(--surface-2) 88%, transparent);
  padding: 12px 10px 10px;
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

.aurora-queue-clear:disabled {
  opacity: 0.35;
  cursor: not-allowed;
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
  display: flex;
  flex-direction: column;
  align-items: stretch;
  justify-content: center;
  flex: 1;
  min-height: 0;
  padding: 12px 8px;
  gap: 6px;
  color: var(--text-muted);
  font-size: 13px;
  box-sizing: border-box;
}

.aurora-queue-empty-title {
  margin: 0;
  font-size: 14px;
  font-weight: 600;
  color: var(--text-secondary);
  text-align: center;
}

.aurora-queue-empty-hint {
  margin: 0;
  font-size: 12px;
  line-height: 1.5;
  color: var(--text-muted);
  text-align: center;
}

.aurora-queue-suggestions {
  list-style: none;
  margin: 10px 0 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.aurora-queue-suggestions li {
  margin: 0;
}

.aurora-queue-suggestions button {
  width: 100%;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 2px;
  padding: 8px 8px;
  border: 0;
  border-radius: 8px;
  background: color-mix(in srgb, var(--text-primary) 4%, transparent);
  color: var(--text-primary);
  cursor: pointer;
  text-align: left;
  font: inherit;
  font-size: 12.5px;
  font-weight: 600;
}

.aurora-queue-suggestions button small {
  color: var(--text-muted);
  font-size: 11px;
  font-weight: 400;
}

.aurora-queue-suggestions button:hover,
.aurora-queue-suggestions button:focus-visible {
  background: color-mix(in srgb, var(--accent) 10%, transparent);
  outline: none;
}

/* Full-width DAILY PICKS — larger covers so they own the space under hero */
.aurora-recommendations {
  margin: 0;
  min-width: 0;
  width: 100%;
  padding: 4px 0 0;
  flex: none;
}

.aurora-recommendations .aurora-section-head {
  margin-bottom: 14px;
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  gap: 12px;
}

.aurora-section-head-copy {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.aurora-recommendations .aurora-section-head h2 {
  font-size: 18px;
  font-weight: 700;
  letter-spacing: 0.02em;
  color: var(--text-primary);
  margin: 0;
}

.aurora-section-sub {
  margin: 0;
  font-size: 12px;
  color: var(--text-muted);
  line-height: 1.4;
}

.aurora-picks-refresh {
  border: 0;
  background: transparent;
  color: var(--text-muted);
  font: inherit;
  font-size: 13px;
  cursor: pointer;
  padding: 0 0 2px;
  flex-shrink: 0;
}

.aurora-picks-refresh:hover {
  color: var(--accent);
}

/*
  Bigger cards (≈140–168px): fill the band under hero instead of a dark void.
  auto-fit collapses empty tracks on wide windows.
*/
.aurora-recommendation-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(min(140px, 100%), 1fr));
  gap: 16px 18px;
  width: 100%;
  max-width: none;
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
  gap: 8px;
}

.aurora-track-card strong,
.aurora-track-card small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.aurora-track-card strong { font-size: 13px; font-weight: 600; }
.aurora-track-card small { color: var(--text-muted); font-size: 12px; }

.aurora-track-cover {
  display: grid;
  aspect-ratio: 1;
  width: 100%;
  overflow: hidden;
  border-radius: 12px;
  background: var(--surface-2);
  box-shadow: 0 8px 20px rgba(0, 0, 0, 0.28);
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

.aurora-error-summary {
  padding: 10px 14px;
  margin-bottom: 8px;
  border-radius: 8px;
  background: var(--surface-1);
  color: var(--text-secondary, var(--accent));
  font-size: 13px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.aurora-error-summary p {
  margin: 0;
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
  margin: 0;
  overflow: visible;
  min-height: 0;
  width: 100%;
  min-width: 0;
}

.aurora-section-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  margin-bottom: 12px;
  gap: 12px;
  min-width: 0;
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
  flex-shrink: 0;
}

/* Editor picks / new albums: fill width without ghost empty columns */
.aurora-grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(min(132px, 100%), 1fr));
  gap: 14px 16px;
  width: 100%;
}

@media (min-width: 1600px) {
  .aurora-grid {
    grid-template-columns: repeat(auto-fit, minmax(min(148px, 100%), 1fr));
  }
  .aurora-recommendation-grid {
    grid-template-columns: repeat(auto-fit, minmax(min(152px, 100%), 1fr));
  }

  .aurora-cover {
    max-width: 340px;
  }

  .aurora-stage-main {
    grid-template-columns: minmax(260px, 340px) minmax(0, 1fr);
  }

  .aurora-queue-rail {
    max-height: 340px;
  }
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
  .aurora-stage-hero {
    grid-template-columns: minmax(0, 1fr) minmax(250px, 290px);
    gap: 14px;
  }

  .aurora-stage-main {
    grid-template-columns: minmax(200px, 280px) minmax(0, 1fr);
    gap: 16px;
  }

  .aurora-cover {
    max-width: 280px;
  }

  .aurora-queue-rail {
    max-height: 280px;
  }

  .aurora-recommendation-grid {
    grid-template-columns: repeat(auto-fit, minmax(min(128px, 100%), 1fr));
  }
}

/* Hide queue earlier so mid/small desktops aren't cramped */
@media (max-width: 1279px) {
  .aurora-home {
    padding: 12px 14px 16px;
    gap: 12px;
  }

  .aurora-stage-hero {
    grid-template-columns: minmax(0, 1fr);
  }

  .aurora-queue-rail {
    display: none;
  }

  .aurora-recommendation-grid {
    grid-template-columns: repeat(auto-fit, minmax(min(120px, 30%), 1fr));
    gap: 12px 14px;
  }

  .aurora-grid {
    grid-template-columns: repeat(auto-fit, minmax(min(118px, 30%), 1fr));
    gap: 12px;
  }
}

@media (max-width: 899px) {
  .aurora-stage-main {
    grid-template-columns: 1fr;
    text-align: center;
  }

  .aurora-cover {
    width: min(58vw, 240px);
    max-width: 240px;
    margin-inline: auto;
  }

  .aurora-meta-row {
    justify-content: center;
    text-align: center;
  }

  .aurora-album-line {
    margin-inline: auto;
  }

  .aurora-section-head-copy {
    text-align: left;
  }

  .aurora-stage-empty {
    min-height: 200px;
    padding: 22px 18px;
  }

  .aurora-stage-empty h1 {
    font-size: clamp(24px, 6vw, 34px);
  }

  .aurora-recommendation-grid {
    grid-template-columns: repeat(auto-fit, minmax(min(108px, 46%), 1fr));
    gap: 10px 12px;
  }
}
</style>
