<script setup lang="ts">
import { ref, computed } from 'vue';
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
  return props.model.queuePreview.length;
});

function onHeroPlay() {
  const t = props.model.heroTrack;
  if (t) emit('play-track', t);
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
  <div class="aurora-home" ref="rootEl">
    <div class="aurora-stage">
      <div class="aurora-cover">
        <img
          v-if="heroCover"
          :src="heroCover"
          alt="cover"
          @error="onCoverError"
        />
        <div v-else class="aurora-cover-placeholder">
          <svg viewBox="0 0 200 200" fill="none">
            <circle cx="100" cy="100" r="70" fill="currentColor" opacity="0.08" />
            <circle cx="100" cy="100" r="50" fill="none" stroke="currentColor" opacity="0.2" stroke-width="1" />
            <circle cx="100" cy="100" r="30" fill="none" stroke="currentColor" opacity="0.2" stroke-width="1" />
            <circle cx="100" cy="100" r="10" fill="currentColor" opacity="0.3" />
          </svg>
        </div>
      </div>

      <div class="aurora-info">
        <div class="aurora-label">每日推荐 · Daily</div>
        <h2 class="aurora-song-name">{{ model.heroTrack?.SongName || '未在播放' }}</h2>
        <p class="aurora-artist">{{ model.heroTrack?.SingerName || '—' }}</p>
        <p class="aurora-source">{{ heroSource }}</p>
        <div class="aurora-meta-row">
          <span class="aurora-queue-count">队列 {{ queueCount }} 首</span>
          <button
            class="aurora-play play-cta"
            data-test="hero-play"
            @click="onHeroPlay"
          >
            <svg viewBox="0 0 24 24" fill="currentColor" width="14" height="14">
              <polygon points="6,4 20,12 6,20" />
            </svg>
            <span>播放</span>
          </button>
          <button
            class="aurora-refresh"
            data-test="refresh"
            :disabled="model.isRefreshing"
            @click="emit('refresh')"
          >
            {{ model.isRefreshing ? '刷新中…' : '刷新 ↻' }}
          </button>
        </div>
      </div>

      <div class="aurora-queue-preview">
        <h3>接下来 · Up Next</h3>
        <ul v-if="model.queuePreview.length > 0">
          <li
            v-for="track in model.queuePreview"
            :key="track.FileHash"
            class="aurora-queue-item"
          >
            <span class="aurora-q-title">{{ track.SongName }}</span>
            <span class="aurora-q-artist">{{ track.SingerName }}</span>
            <span class="aurora-q-dur">{{ formatDuration(track.Duration) }}</span>
          </li>
        </ul>
        <div v-else class="aurora-queue-empty">
          <p>暂无队列</p>
        </div>
      </div>
    </div>

    <div
      v-for="err in model.errors"
      :key="err.section"
      class="aurora-error"
    >
      {{ err.message }}
      <button class="aurora-retry" @click="emit('refresh')">重试</button>
    </div>

    <div v-if="model.playlists.length > 0" class="aurora-section">
      <div class="aurora-section-head">
        <h3>编辑推荐</h3>
        <span>{{ model.isRefreshing ? '刷新中…' : '本周精选' }}</span>
      </div>
      <div class="aurora-grid">
        <article
          v-for="pl in model.playlists"
          :key="pl.specialid"
          class="aurora-card"
          :data-test="`playlist-${pl.specialid}`"
          @click="onPlaylistClick(pl)"
        >
          <div class="aurora-card-cover">
            <img v-if="pl.imgurl" :src="pl.imgurl" alt="cover" />
            <div v-else class="aurora-card-placeholder">歌单</div>
          </div>
          <div class="aurora-card-meta">
            <div class="aurora-card-title">{{ pl.specialname }}</div>
            <div class="aurora-card-sub">{{ pl.nickname }}</div>
          </div>
        </article>
      </div>
    </div>

    <div v-if="model.albums.length > 0" class="aurora-section">
      <div class="aurora-section-head">
        <h3>最新歌单</h3>
        <span>{{ model.isRefreshing ? '刷新中…' : '全部歌单' }}</span>
      </div>
      <div class="aurora-grid">
        <article
          v-for="pl in model.albums"
          :key="pl.specialid"
          class="aurora-card"
          :data-test="`playlist-${pl.specialid}`"
          @click="onPlaylistClick(pl)"
        >
          <div class="aurora-card-cover">
            <img v-if="pl.imgurl" :src="pl.imgurl" alt="cover" />
            <div v-else class="aurora-card-placeholder">新碟</div>
          </div>
          <div class="aurora-card-meta">
            <div class="aurora-card-title">{{ pl.specialname }}</div>
            <div class="aurora-card-sub">{{ pl.nickname }}</div>
          </div>
        </article>
      </div>
    </div>
  </div>
</template>

<script lang="ts">
export default { name: 'AuroraHome' };
</script>

<style scoped>
.aurora-home {
  padding: 28px 32px;
  min-height: 100%;
}

.aurora-stage {
  display: grid;
  grid-template-columns: minmax(0, 1fr) minmax(0, 1fr) minmax(0, 0.8fr);
  gap: 28px;
  align-items: center;
  margin-bottom: 36px;
}

.aurora-cover {
  aspect-ratio: 1;
  width: 100%;
  max-width: 320px;
  border-radius: 16px;
  overflow: hidden;
  background: var(--surface-1);
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.18);
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
  color: var(--text-secondary);
}

.aurora-cover-placeholder svg {
  width: 60%;
  height: 60%;
}

.aurora-info {
  display: flex;
  flex-direction: column;
  gap: 8px;
  min-width: 0;
}

.aurora-label {
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  color: var(--text-secondary);
}

.aurora-song-name {
  font-size: 24px;
  font-weight: 700;
  line-height: 1.3;
  word-break: break-word;
  overflow-wrap: break-word;
  margin: 0;
}

.aurora-artist {
  font-size: 15px;
  color: var(--text-secondary);
  margin: 0;
}

.aurora-source {
  font-size: 13px;
  color: var(--text-tertiary, var(--text-secondary));
  margin: 0;
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
  padding: 10px 24px;
  border-radius: 24px;
  border: none;
  background: var(--accent);
  color: var(--on-accent, #fff);
  font-size: 14px;
  font-weight: 600;
  cursor: pointer;
  transition: transform 0.1s ease-out, box-shadow 0.2s ease;
  flex-shrink: 0;
}

.aurora-play:hover {
  transform: scale(1.04);
}

.aurora-play:active {
  transform: scale(0.94);
}

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

.aurora-queue-preview {
  background: var(--surface-1);
  border-radius: 12px;
  padding: 16px;
  min-height: 200px;
}

.aurora-queue-preview h3 {
  font-size: 14px;
  font-weight: 600;
  margin: 0 0 12px;
  color: var(--text-secondary);
}

.aurora-queue-preview ul {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.aurora-queue-item {
  display: grid;
  grid-template-columns: 1fr auto;
  grid-template-rows: auto auto;
  gap: 0 8px;
  padding: 6px 0;
  border-bottom: 1px solid var(--border-subtle);
}

.aurora-q-title {
  font-size: 13px;
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.aurora-q-artist {
  font-size: 12px;
  color: var(--text-secondary);
  grid-column: 1;
}

.aurora-q-dur {
  font-size: 12px;
  color: var(--text-tertiary, var(--text-secondary));
  grid-column: 2;
  grid-row: 1 / 3;
  align-self: center;
}

.aurora-queue-empty {
  display: flex;
  align-items: center;
  justify-content: center;
  min-height: 120px;
  color: var(--text-secondary);
  font-size: 13px;
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
  cursor: pointer;
  transition: transform 0.15s ease;
}

.aurora-card:hover {
  transform: translateY(-2px);
}

.aurora-card-cover {
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

@media (max-width: 900px) {
  .aurora-stage {
    grid-template-columns: 1fr;
    text-align: center;
  }

  .aurora-cover {
    max-width: 240px;
    margin: 0 auto;
  }

  .aurora-meta-row {
    justify-content: center;
  }
}
</style>
