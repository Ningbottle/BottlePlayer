<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import type { HomeViewModel } from './homeViewModel';
import type { Track } from '../../api/normalizer';
import type { PlaylistInfo } from '../../api/homeFeedStore';

const props = defineProps<{ model: HomeViewModel }>();

const emit = defineEmits<{
  (e: 'play-track', track: Track): void;
  (e: 'refresh'): void;
  (e: 'navigate', view: string, params?: any): void;
}>();

const coverError = ref(false);

watch(() => props.model.heroTrack, () => { coverError.value = false; });

const heroCover = computed(() => {
  if (coverError.value) return '';
  return props.model.heroTrack?.Image || '';
});

const timeOfDayPhrase = computed(() => {
  const h = new Date().getHours();
  if (h < 5) return '夜深人静，适合低吟';
  if (h < 9) return '清晨的轻语时光';
  if (h < 12) return '上午的舒缓节拍';
  if (h < 14) return '正午的悠扬时分';
  if (h < 18) return '适合慢听的午后';
  if (h < 22) return '晚归路上的回响';
  return '深夜的安眠曲';
});

const recommendations = computed(() => {
  return props.model.dailyTracks.slice(0, 10);
});

function onHeroPlay() {
  const t = props.model.heroTrack;
  if (t) emit('play-track', t);
}

function onRecPlay(track: Track) {
  emit('play-track', track);
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

function formatDate(): string {
  const days = ['日', '一', '二', '三', '四', '五', '六'];
  return `星期${days[new Date().getDay()]} · ${new Date().toLocaleDateString('zh-CN', { month: 'long', day: 'numeric' })}`;
}
</script>

<template>
  <div class="np-home" ref="rootEl">
    <div class="np-masthead">
      <div class="np-headline-area">
        <div class="np-kicker">Late Edition · 晚刊</div>
        <h1 class="np-headline-title">为你精选<i>For You</i></h1>
        <p class="np-editorial-phrase">{{ timeOfDayPhrase }}</p>
        <div class="np-date">{{ formatDate() }}</div>
        <button
          class="np-play play-cta"
          data-test="hero-play"
          @click="onHeroPlay"
        >
          <span class="np-play-icon">
            <svg viewBox="0 0 24 24" fill="currentColor" width="10" height="10">
              <polygon points="6,4 20,12 6,20" />
            </svg>
          </span>
          立即收听 · 每日推荐
        </button>
      </div>

      <div class="np-hero-cover">
        <img
          v-if="heroCover"
          :src="heroCover"
          alt="cover"
          @error="onCoverError"
        />
        <div v-else class="np-cover-placeholder">
          <svg viewBox="0 0 200 200" fill="none">
            <rect width="200" height="200" fill="var(--surface-1)" />
            <circle cx="100" cy="100" r="60" fill="none" stroke="currentColor" opacity="0.2" stroke-width="0.6" />
            <circle cx="100" cy="100" r="40" fill="none" stroke="currentColor" opacity="0.2" stroke-width="0.6" />
            <circle cx="100" cy="100" r="8" fill="currentColor" opacity="0.3" />
          </svg>
        </div>
        <div class="np-cover-overlay"></div>
        <div class="np-cover-meta">
          <b>{{ model.heroTrack?.SongName || '—' }}</b>
          <span>{{ model.heroTrack?.SingerName || '—' }}</span>
        </div>
      </div>
    </div>

    <div class="np-rec-section">
      <div class="np-rec-head">
        <h3>每日推荐 <i>Daily Picks</i></h3>
        <button
          class="np-refresh"
          data-test="refresh"
          :disabled="model.isRefreshing"
          @click="emit('refresh')"
        >
          {{ model.isRefreshing ? '刷新中…' : '刷新推荐 ↻' }}
        </button>
      </div>

      <div
        v-for="err in model.errors"
        :key="err.section"
        class="np-error"
      >
        {{ err.message }}
        <button class="np-retry" @click="emit('refresh')">重试</button>
      </div>

      <ol class="np-rec-list">
        <li
          v-for="(track, idx) in recommendations"
          :key="track.FileHash"
          class="np-rec-item"
          :style="{ '--rec-delay': `${idx * 0.025}s` }"
          @click="onRecPlay(track)"
        >
          <span class="np-num">{{ String(idx + 1).padStart(2, '0') }}</span>
          <span class="np-rec-title">
            <b>{{ track.SongName }}</b>
            <span>{{ track.SingerName }}</span>
          </span>
          <span class="np-rec-dur">{{ formatDuration(track.Duration) }}</span>
        </li>
      </ol>
    </div>

    <div v-if="model.playlists.length > 0" class="np-section">
      <div class="np-section-bar">
        <h2>编辑推荐<i>Editor's Picks</i></h2>
      </div>
      <div class="np-grid">
        <article
          v-for="pl in model.playlists"
          :key="pl.specialid"
          class="np-card"
          :data-test="`playlist-${pl.specialid}`"
          @click="onPlaylistClick(pl)"
        >
          <div class="np-card-cover">
            <img v-if="pl.imgurl" :src="pl.imgurl" alt="cover" />
            <div v-else class="np-card-placeholder">歌单</div>
          </div>
          <div class="np-card-meta">
            <div class="np-card-title">{{ pl.specialname }}</div>
            <div class="np-card-sub">By {{ pl.nickname }}</div>
          </div>
        </article>
      </div>
    </div>

    <div v-if="model.albums.length > 0" class="np-section">
      <div class="np-section-bar">
        <h2>最新歌单<i>Newly Pressed</i></h2>
      </div>
      <div class="np-grid">
        <article
          v-for="pl in model.albums"
          :key="pl.specialid"
          class="np-card"
          :data-test="`playlist-${pl.specialid}`"
          @click="onPlaylistClick(pl)"
        >
          <div class="np-card-cover">
            <img v-if="pl.imgurl" :src="pl.imgurl" alt="cover" />
            <div v-else class="np-card-placeholder">新碟</div>
          </div>
          <div class="np-card-meta">
            <div class="np-card-title">{{ pl.specialname }}</div>
            <div class="np-card-sub">{{ pl.nickname }}</div>
          </div>
        </article>
      </div>
    </div>
  </div>
</template>

<script lang="ts">
export default { name: 'NewsprintHome' };
</script>

<style scoped>
.np-home {
  padding: 28px 32px;
  min-height: 100%;
}

.np-masthead {
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 32px;
  align-items: end;
  margin-bottom: 28px;
  border-bottom: 2px solid var(--ink, var(--text-primary));
  padding-bottom: 20px;
}

.np-headline-area {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.np-kicker {
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.12em;
  color: var(--text-secondary);
  font-family: var(--font-serif, serif);
}

.np-headline-title {
  font-size: 32px;
  font-weight: 800;
  margin: 0;
  line-height: 1.1;
  font-family: var(--font-serif, serif);
}

.np-headline-title i {
  font-style: italic;
  font-weight: 400;
  font-size: 0.6em;
  margin-left: 8px;
  color: var(--text-secondary);
}

.np-editorial-phrase {
  font-size: 14px;
  color: var(--text-secondary);
  margin: 0;
  font-style: italic;
}

.np-date {
  font-size: 13px;
  color: var(--text-secondary);
  font-weight: 600;
}

.np-play {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 10px 20px;
  border: 2px solid var(--ink, var(--text-primary));
  background: transparent;
  color: var(--ink, var(--text-primary));
  font-size: 14px;
  font-weight: 700;
  cursor: pointer;
  transition: transform 0.1s ease-out, background 0.2s ease, color 0.2s ease;
  margin-top: 8px;
  align-self: flex-start;
  font-family: var(--font-serif, serif);
}

.np-play:hover {
  background: var(--ink, var(--text-primary));
  color: var(--paper, var(--bg));
}

.np-play:active {
  transform: scale(0.96);
}

.np-play-icon {
  display: inline-flex;
}

.np-hero-cover {
  position: relative;
  width: 200px;
  height: 200px;
  flex-shrink: 0;
  overflow: hidden;
  border-radius: 4px;
}

.np-hero-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  filter: saturate(0.7);
  transition: filter 0.4s ease;
}

.np-hero-cover:hover img {
  filter: saturate(1);
}

.np-cover-overlay {
  position: absolute;
  inset: 0;
  background: rgba(0, 0, 0, 0.06);
  transition: opacity 0.4s ease;
  pointer-events: none;
}

.np-hero-cover:hover .np-cover-overlay {
  opacity: 0;
}

.np-cover-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  color: var(--text-secondary);
  background: var(--surface-1);
}

.np-cover-meta {
  position: absolute;
  bottom: 0;
  left: 0;
  right: 0;
  padding: 8px 10px;
  background: linear-gradient(transparent, rgba(0, 0, 0, 0.6));
  color: #fff;
  display: flex;
  flex-direction: column;
  font-size: 12px;
}

.np-cover-meta b {
  font-weight: 700;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.np-cover-meta span {
  opacity: 0.8;
  font-size: 11px;
}

.np-rec-section {
  margin-bottom: 28px;
}

.np-rec-head {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  margin-bottom: 12px;
}

.np-rec-head h3 {
  font-size: 18px;
  font-weight: 700;
  margin: 0;
  font-family: var(--font-serif, serif);
}

.np-rec-head h3 i {
  font-style: italic;
  font-weight: 400;
  font-size: 0.7em;
  color: var(--text-secondary);
}

.np-refresh {
  background: none;
  border: none;
  color: var(--text-secondary);
  cursor: pointer;
  font-size: 13px;
  font-family: var(--font-serif, serif);
}

.np-refresh:hover {
  color: var(--accent);
}

.np-refresh:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.np-error {
  padding: 8px 12px;
  margin-bottom: 8px;
  font-style: italic;
  color: var(--accent);
  font-size: 13px;
  display: flex;
  align-items: center;
  gap: 8px;
}

.np-retry {
  background: none;
  border: none;
  color: var(--accent);
  cursor: pointer;
  font-size: 13px;
  text-decoration: underline;
}

.np-rec-list {
  list-style: none;
  margin: 0;
  padding: 0;
}

.np-rec-item {
  display: grid;
  grid-template-columns: 32px 1fr auto;
  align-items: center;
  gap: 12px;
  padding: 8px 4px;
  border-bottom: 1px solid var(--border-subtle);
  cursor: pointer;
  transition: background 0.15s ease;
  animation: np-rec-enter 0.3s ease both;
  animation-delay: var(--rec-delay, 0s);
}

.np-rec-item:hover {
  background: var(--surface-1);
}

@keyframes np-rec-enter {
  from {
    opacity: 0;
    transform: translateY(8px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

.np-num {
  font-size: 16px;
  font-weight: 700;
  color: var(--text-secondary);
  font-family: var(--font-serif, serif);
  text-align: right;
}

.np-rec-title {
  display: flex;
  flex-direction: column;
  min-width: 0;
}

.np-rec-title b {
  font-size: 14px;
  font-weight: 600;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.np-rec-title span {
  font-size: 12px;
  color: var(--text-secondary);
}

.np-rec-dur {
  font-size: 12px;
  color: var(--text-secondary);
}

.np-section {
  margin-bottom: 28px;
}

.np-section-bar {
  margin-bottom: 14px;
}

.np-section-bar h2 {
  font-size: 22px;
  font-weight: 800;
  margin: 0;
  font-family: var(--font-serif, serif);
}

.np-section-bar h2 i {
  font-style: italic;
  font-weight: 400;
  font-size: 0.6em;
  color: var(--text-secondary);
}

.np-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
  gap: 16px;
}

.np-card {
  cursor: pointer;
  transition: transform 0.15s ease;
}

.np-card:hover {
  transform: translateY(-2px);
}

.np-card-cover {
  aspect-ratio: 1;
  border-radius: 4px;
  overflow: hidden;
  background: var(--surface-1);
  margin-bottom: 6px;
}

.np-card-cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.np-card-placeholder {
  width: 100%;
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  color: var(--text-secondary);
}

.np-card-title {
  font-size: 13px;
  font-weight: 600;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.np-card-sub {
  font-size: 12px;
  color: var(--text-secondary);
}

@media (max-width: 700px) {
  .np-masthead {
    grid-template-columns: 1fr;
  }

  .np-hero-cover {
    width: 100%;
    max-width: 240px;
  }
}
</style>
