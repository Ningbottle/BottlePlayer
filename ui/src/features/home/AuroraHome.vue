<script setup lang="ts">
import { ref, computed, watch, onBeforeUpdate, onMounted, onActivated, onDeactivated, onUnmounted, nextTick } from 'vue';
import type { HomeViewModel } from './homeViewModel';
import type { Track } from '../../shared/music/track';
import type { PlaylistInfo } from './homeFeedStore';
import { gsap } from 'gsap';
import type { HomeEnterMode } from './homeEnterSession';
import { animateStagger, isReducedMotion } from '../../shared/motion/motion';
import { playerStore, togglePlay as storeTogglePlay } from '../../playback/index';
import { createPlaybackAudioLevelMonitor, type AudioLevelMonitor } from '../../playback/index';
import { flyCoverToDock } from '../../playback/index';
import { extractDominantColor, type RGB } from './coverColor';
import { PhPause, PhPlay } from '@phosphor-icons/vue';
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

/** Scroll-reveal for the below-fold sections (编辑推荐 / 最新歌单). */
let revealObserver: IntersectionObserver | null = null;
const revealedSections = new WeakSet<Element>();

function observeSections(): void {
  if (isReducedMotion() || typeof IntersectionObserver === 'undefined') return;
  if (!revealObserver) {
    revealObserver = new IntersectionObserver((entries) => {
      for (const entry of entries) {
        if (!entry.isIntersecting || revealedSections.has(entry.target)) continue;
        revealedSections.add(entry.target);
        const cards = Array.from(entry.target.querySelectorAll('.aurora-card'));
        animateStagger(cards, 'cardEnter', { duration: 0.4, stagger: 0.04, maxItems: 10, fromY: 20 });
        revealObserver?.unobserve(entry.target);
      }
    }, { threshold: 0.15 });
  }
  const root = stageEl.value?.closest('.aurora-home') ?? stageEl.value?.parentElement;
  root?.querySelectorAll('.aurora-section').forEach((section) => {
    if (!revealedSections.has(section)) revealObserver?.observe(section);
  });
}
const coverEl = ref<HTMLElement | null>(null);

/** Live loudness tap for the cone dust (falls back to a static 0). */
let levelMonitor: AudioLevelMonitor | null = null;
const fallbackLevel = ref(0);
const atmosphereLevel = computed(() => levelMonitor?.level ?? fallbackLevel);

/** Cover-derived tint for the cone wash/dust (never touches the user's accent). */
const coverTint = ref<RGB | null>(null);
let tintToken = 0;

function bootLevelMonitor(): void {
  if (levelMonitor) return;
  levelMonitor = createPlaybackAudioLevelMonitor();
  levelMonitor?.start();
}

// hasAudio is the store's reactive projection of MediaRuntime audio
// availability (the element itself is not reactive state).
watch(() => playerStore.hasAudio, () => bootLevelMonitor());

const recommendationEls = ref<HTMLElement[]>([]);
/** Stage + stagger enter handles (killed on re-enter). */
const enterHandles: Array<{ kill(): void }> = [];

watch(() => props.model.heroTrack, () => {
  coverError.value = false;
});

onMounted(() => {
  bootLevelMonitor();
  void nextTick(() => observeSections());
});

watch(
  [() => props.model.playlists.length, () => props.model.albums.length],
  () => {
    void nextTick(() => observeSections());
  },
);

onActivated(() => {
  bootLevelMonitor();
});

onDeactivated(() => {
  levelMonitor?.stop();
  levelMonitor = null;
});

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

onUnmounted(() => {
  levelMonitor?.stop();
  levelMonitor = null;
  revealObserver?.disconnect();
  revealObserver = null;
  killEnterHandles();
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

watch(heroCover, (url) => {
  const token = ++tintToken;
  coverTint.value = null;
  if (!url) return;
  void extractDominantColor(url).then((tint) => {
    if (token === tintToken) coverTint.value = tint;
  });
});

/**
 * Stage-right rail:
 * - While a personalFm session is active, follow the live playback queue so
 *   newly appended recommendations appear as you listen.
 * - Otherwise show the home daily snapshot (refreshable, not the live queue).
 */
const DAILY_RAIL_LIMIT = 12;

const isLiveRecoRail = computed(
  () => props.model.queueMode === 'personalFm' && props.model.queueTotal > 0,
);

const dailyRailTracks = computed(() => {
  if (isLiveRecoRail.value) {
    return props.model.queuePreview ?? [];
  }
  const daily = props.model.dailyTracks ?? [];
  return (Array.isArray(daily) ? daily : []).slice(0, DAILY_RAIL_LIMIT);
});

const dailyRailCount = computed(() =>
  isLiveRecoRail.value ? props.model.queueTotal : (props.model.dailyTracks?.length ?? 0),
);

const dailyRailTitle = computed(() =>
  isLiveRecoRail.value ? '正在推荐' : '每日推荐',
);

const dailyRailIndexOffset = computed(() =>
  isLiveRecoRail.value ? props.model.queueWindowStart : 0,
);

function onHeroPlay() {
  const t = props.model.heroTrack;
  if (!t) return;
  if (isHeroCurrent.value) {
    if (props.model.isPlaybackLoading) return;
    storeTogglePlay();
    return;
  }
  flyFromVinyl();
  onTrackPlay(t);
}

/** GSAP Flip flight from the hero cover into the dock cover slot. */
function flyFromVinyl(): void {
  if (coverEl.value && heroCover.value) flyCoverToDock(coverEl.value, heroCover.value);
}

function coverElFromEvent(e: MouseEvent): HTMLElement | undefined {
  const el = (e.currentTarget as HTMLElement | null)?.querySelector('.aurora-track-cover');
  return el instanceof HTMLElement ? el : undefined;
}

/** Hero is the loaded track → the vinyl acts as the deck's play/pause. */
const isHeroCurrent = computed(
  () => !!props.model.heroTrack && props.model.heroTrack.FileHash === props.model.activeQueueHash,
);

const isHeroPlaybackLoading = computed(
  () => isHeroCurrent.value && props.model.isPlaybackLoading,
);

const vinylActionLabel = computed(() => {
  if (!isHeroCurrent.value) return '播放';
  if (props.model.isPlaybackLoading) return '取消加载';
  if (props.model.isPlaying) return '暂停';
  return '播放';
});

const vinylShowsPause = computed(
  () => isHeroCurrent.value && (props.model.isPlaying || props.model.isPlaybackLoading),
);

const heroPlayLabel = computed(() => {
  if (!isHeroCurrent.value) return '播放';
  if (props.model.isPlaybackLoading) return '正在加载…';
  if (props.model.isPlaying) return '暂停';
  return '播放';
});

function onVinylToggle(): void {
  const t = props.model.heroTrack;
  if (!t) return;
  if (isHeroCurrent.value) {
    storeTogglePlay();
  } else {
    flyFromVinyl();
    onTrackPlay(t);
  }
}

function onTrackPlay(track: Track, fromEl?: HTMLElement): void {
  if (fromEl && track.Image) flyCoverToDock(fromEl, track.Image);
  emit('play-track', track);
}

function onOpenLyrics(): void {
  emit('navigate', 'lyric');
}

function isActiveDailyTrack(track: Track): boolean {
  return !!props.model.activeQueueHash && track.FileHash === props.model.activeQueueHash;
}

function onPlaylistClick(pl: PlaylistInfo) {
  emit('navigate', 'playlist', { id: pl.specialid, name: pl.specialname });
}

function onCoverError() {
  coverError.value = true;
}

/** Safe mm:ss; missing/invalid duration shows an em dash (never NaN). */
function formatDuration(sec: number | undefined | null): string {
  const n = Number(sec);
  if (!Number.isFinite(n) || n <= 0) return '—';
  const total = Math.floor(n);
  const m = Math.floor(total / 60);
  const s = total % 60;
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
      <AuroraAtmosphere :is-playing="model.isPlaying" :level="atmosphereLevel" :tint="coverTint" />
      <div class="aurora-stage-hero">
        <div v-if="model.heroTrack" class="aurora-stage-main">
          <div ref="coverEl" class="aurora-cover aurora-cover-square" data-test="hero-vinyl">
            <img
              v-if="heroCover"
              :src="heroCover"
              :alt="`${model.heroTrack?.SongName || '当前歌曲'}封面`"
              @error="onCoverError"
            />
            <div v-else class="aurora-cover-placeholder">封面暂缺</div>
            <button
              type="button"
              class="aurora-cover-toggle"
              data-test="vinyl-toggle"
              :class="{ 'is-cancelling': isHeroPlaybackLoading }"
              :aria-label="vinylActionLabel"
              :title="vinylActionLabel"
              @click.stop="onVinylToggle"
            >
              <PhPause v-if="vinylShowsPause" :size="14" weight="fill" aria-hidden="true" />
              <PhPlay v-else :size="14" weight="fill" aria-hidden="true" />
            </button>
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
              <button
                class="aurora-play play-cta"
                type="button"
                data-test="hero-play"
                :disabled="isHeroPlaybackLoading"
                :aria-label="heroPlayLabel"
                :title="heroPlayLabel"
                @click="onHeroPlay"
              >
                {{ heroPlayLabel }}
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
          <div class="aurora-cover aurora-cover-square aurora-cover-empty" aria-hidden="true">
            <div class="aurora-cover-placeholder">封面暂缺</div>
          </div>
          <div class="aurora-stage-empty-copy">
            <p class="aurora-label"><span class="aurora-label-dot" aria-hidden="true" />还没有开始播放</p>
            <p class="aurora-stage-empty-title">选择一首歌，开始聆听</p>
            <p class="aurora-stage-empty-hint">从每日推荐或左侧歌单开始，舞台会随播放状态展开。</p>
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
        </div>

        <aside
          class="aurora-queue-rail"
          data-test="queue-rail"
          :aria-label="dailyRailTitle"
          :data-live-reco="isLiveRecoRail ? 'true' : 'false'"
        >
          <header class="aurora-queue-rail-head">
            <h2>{{ dailyRailTitle }} <span>{{ dailyRailCount }}</span></h2>
            <button
              v-if="!isLiveRecoRail"
              type="button"
              class="aurora-queue-clear"
              data-test="daily-rail-refresh"
              :disabled="model.sections.daily.loading || model.sections.daily.refreshing"
              aria-label="刷新每日推荐"
              @click="model.sections.daily.retry()"
            >{{ model.sections.daily.refreshing ? '刷新中' : '刷新' }}</button>
          </header>
          <ol
            v-if="dailyRailTracks.length"
            class="aurora-queue-list"
            data-test="daily-rail-list"
          >
            <li v-for="(track, index) in dailyRailTracks" :key="track.FileHash" class="aurora-queue-row">
              <button
                type="button"
                :data-test="`queue-track-${track.FileHash}`"
                :class="{ 'is-active': isActiveDailyTrack(track) }"
                :aria-current="isActiveDailyTrack(track) ? 'true' : undefined"
                @click="onTrackPlay(track)"
              >
                <span class="aurora-queue-lead">
                  <span class="aurora-queue-play" aria-hidden="true"><PhPlay :size="11" weight="fill" /></span>
                  <span class="aurora-queue-index">
                    <span
                      v-if="isActiveDailyTrack(track)"
                      class="aurora-eq"
                      :class="{ 'is-live': model.isPlaying }"
                      aria-hidden="true"
                    ><i /><i /><i /></span>
                    <template v-else>{{ String(dailyRailIndexOffset + index + 1).padStart(2, '0') }}</template>
                  </span>
                </span>
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
            <p class="aurora-queue-empty-title">今日推荐还在路上</p>
            <p class="aurora-queue-empty-hint">点击刷新拉取每日推荐；真实播放队列在底部播放栏列表中</p>
            <button
              type="button"
              class="aurora-play"
              data-test="daily-rail-empty-retry"
              :disabled="model.sections.daily.loading || model.sections.daily.refreshing"
              @click="model.sections.daily.retry()"
            >
              {{ model.sections.daily.error ? '重试' : '刷新推荐' }}
            </button>
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
          @click="onTrackPlay(track, coverElFromEvent($event))"
        >
          <span class="aurora-track-cover">
            <img v-if="track.Image" :src="track.Image" :alt="`${track.SongName}封面`" />
            <span v-else>推荐</span>
            <span class="aurora-track-hover" aria-hidden="true"><PhPlay :size="18" weight="fill" /></span>
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

/* Turntable night: static light cone from the top-right (zero rAF cost) */
.aurora-stage::before {
  content: '';
  position: absolute;
  inset: -10% -8% 32% 28%;
  background: radial-gradient(ellipse 50% 66% at 84% 0%, color-mix(in srgb, var(--accent) 9%, transparent), transparent 62%);
  pointer-events: none;
  z-index: 0;
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
}

.aurora-stage-empty {
  min-height: 280px;
  display: grid;
  grid-template-columns: minmax(200px, 280px) minmax(0, 1fr);
  align-items: center;
  gap: 22px;
  padding: 0;
  border: 0;
  background: transparent;
  box-shadow: none;
  min-width: 0;
}

.aurora-stage-empty-copy {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 10px;
  min-width: 0;
}

.aurora-stage-empty-title {
  margin: 0;
  font-family: Georgia, 'Noto Serif SC', 'Songti SC', serif;
  font-size: clamp(20px, 2vw, 26px);
  line-height: 1.3;
  color: var(--text-primary);
}

.aurora-stage-empty-hint {
  margin: 0;
  max-width: 36rem;
  color: var(--text-secondary);
  font-size: 13px;
  line-height: 1.65;
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



/* Turntable night: the cover is a vinyl record, not a rounded card */
.aurora-cover {
  aspect-ratio: 1;
  width: 100%;
  max-width: 320px;
  height: auto;
  border-radius: 50%;
  flex: none;
}

.aurora-cover.aurora-vinyl {
  position: relative;
  background: #0a0a09;
  box-shadow:
    0 24px 48px rgba(0, 0, 0, 0.45),
    0 0 0 1px color-mix(in srgb, #fff 5%, transparent);
  overflow: visible;
}

.aurora-vinyl-disc {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  overflow: hidden;
  will-change: transform;
}

.aurora-vinyl-disc img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
  border-radius: 50%;
}

.aurora-cover-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  font-size: 13px;
  border-radius: 50%;
}

/* Grooves + the aurora specular arc — rotates with the disc */
.aurora-vinyl-grooves {
  position: absolute;
  inset: 0;
  border-radius: 50%;
  background:
    conic-gradient(from 210deg,
      transparent 0deg,
      color-mix(in srgb, var(--accent) 14%, transparent) 18deg,
      transparent 55deg),
    repeating-radial-gradient(circle at 50% 50%,
      rgba(255, 255, 255, 0.05) 0 1px,
      transparent 1px 4px);
  pointer-events: none;
}

/* Static center label + spindle hole (the aurora dot color) */
.aurora-vinyl-spindle {
  position: absolute;
  left: 50%;
  top: 50%;
  width: 26%;
  aspect-ratio: 1;
  transform: translate(-50%, -50%);
  border-radius: 50%;
  background: radial-gradient(circle at 50% 50%,
    var(--app-bg) 0 11%,
    color-mix(in srgb, var(--accent) 82%, #000 18%) 12% 100%);
  box-shadow: 0 0 0 1px color-mix(in srgb, #fff 8%, transparent);
  pointer-events: none;
}

.aurora-vinyl-empty .aurora-vinyl-disc {
  background: #0a0a09;
}

.aurora-cover-square.aurora-cover-empty {
  max-width: 280px;
}

.aurora-cover-square.aurora-cover-empty .aurora-cover-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-muted);
  font-size: 13px;
  border: 1px dashed var(--border-subtle);
  border-radius: inherit;
  box-sizing: border-box;
}

/* Square hero cover — big art first, playback badge tucked in the corner */
.aurora-cover.aurora-cover-square {
  position: relative;
  aspect-ratio: 1;
  width: 100%;
  max-width: 320px;
  height: auto;
  border-radius: 12px;
  overflow: hidden;
  background: var(--surface-2);
  box-shadow:
    0 18px 40px rgba(0, 0, 0, 0.32),
    0 0 0 1px color-mix(in srgb, #fff 6%, transparent);
  flex: none;
}

.aurora-cover-square > img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.aurora-cover-toggle {
  position: absolute;
  right: 10px;
  bottom: 10px;
  width: 34px;
  height: 34px;
  border: 0;
  border-radius: 50%;
  background: color-mix(in srgb, var(--accent) 92%, #000 8%);
  color: #0a1410;
  display: grid;
  place-items: center;
  cursor: pointer;
  opacity: 0;
  transition: opacity 0.15s ease, filter 0.15s ease;
  box-shadow: 0 4px 12px rgba(0, 0, 0, 0.4);
}

.aurora-cover-square:hover .aurora-cover-toggle,
.aurora-cover-toggle:focus-visible {
  opacity: 1;
}

.aurora-cover-toggle.is-cancelling { opacity: 1; }

.aurora-cover-toggle:hover { filter: brightness(1.06); }

.aurora-cover-toggle:focus-visible {
  outline: 2px solid var(--focus-ring);
  outline-offset: 2px;
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
  font-family: 'Inter', 'Microsoft YaHei UI', 'PingFang SC', system-ui, sans-serif;
  font-size: 11px;
  letter-spacing: 0.12em;
  text-transform: uppercase;
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
  font-size: clamp(26px, 2.6vw, 36px);
  font-weight: 700;
  line-height: 1.1;
  word-break: break-word;
  overflow-wrap: break-word;
  margin: 0;
  letter-spacing: -0.01em;
  color: var(--text-primary);
}

.aurora-artist {
  font-size: 16px;
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
.aurora-play:disabled {
  cursor: not-allowed;
  opacity: 0.55;
  filter: none;
  box-shadow: none;
}
.aurora-play:disabled:hover { filter: none; }

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
  border: 1px solid var(--border-subtle);
  border-radius: 8px;
  background: color-mix(in srgb, var(--surface-1) 72%, transparent);
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
  grid-template-columns: 24px minmax(0, 1fr) auto;
  gap: 8px;
  align-items: center;
  padding: 6px 8px;
  border: 0;
  border-radius: 6px;
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
  white-space: nowrap;
  flex: none;
  min-width: 2.25em;
  text-align: right;
}

/* Lead cell: index/eq swap for a play glyph on row hover */
.aurora-queue-lead {
  position: relative;
  display: grid;
  place-items: center;
  min-width: 24px;
}

.aurora-queue-play {
  position: absolute;
  inset: 0;
  display: grid;
  place-items: center;
  color: var(--accent);
  opacity: 0;
  transition: opacity 0.12s ease;
}

.aurora-queue-row button:hover .aurora-queue-play,
.aurora-queue-row button:focus-visible .aurora-queue-play {
  opacity: 1;
}

.aurora-queue-row button:hover .aurora-queue-index,
.aurora-queue-row button:focus-visible .aurora-queue-index {
  visibility: hidden;
}

/* Playing-row equalizer: three bars, live only while playing */
.aurora-eq {
  display: inline-flex;
  align-items: flex-end;
  gap: 1.5px;
  height: 10px;
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
  position: relative;
  display: grid;
  aspect-ratio: 1;
  width: 100%;
  overflow: hidden;
  border-radius: 8px;
  background: var(--surface-2);
  box-shadow: none;
  border: 1px solid var(--border-subtle);
}

/* Hover reveals a play disc — the whole card already plays on click */
.aurora-track-hover {
  position: absolute;
  left: 50%;
  top: 50%;
  width: 40px;
  height: 40px;
  transform: translate(-50%, -50%);
  border-radius: 50%;
  background: color-mix(in srgb, var(--accent) 92%, #000 8%);
  color: #0a1410;
  display: grid;
  place-items: center;
  opacity: 0;
  transition: opacity 0.15s ease;
  box-shadow: 0 4px 14px rgba(0, 0, 0, 0.4);
  pointer-events: none;
}

.aurora-track-card:hover .aurora-track-hover,
.aurora-track-card:focus-visible .aurora-track-hover {
  opacity: 1;
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
  border-radius: 8px;
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
    grid-template-columns: 1fr;
    justify-items: center;
  }

  .aurora-stage-empty-copy {
    align-items: center;
    text-align: center;
  }

  .aurora-vinyl-empty {
    width: min(40vw, 180px);
  }

  .aurora-recommendation-grid {
    grid-template-columns: repeat(auto-fit, minmax(min(108px, 46%), 1fr));
    gap: 10px 12px;
  }
}
</style>
