<script setup lang="ts">
import { computed } from 'vue';
import type { HomeViewModel } from './homeViewModel';
import type { Track } from '../../api/normalizer';
import type { PlaylistInfo } from '../../api/homeFeedStore';

const props = defineProps<{ model: HomeViewModel }>();

const emit = defineEmits<{
  (e: 'play-track', track: Track): void;
  (e: 'refresh'): void;
  (e: 'navigate', view: string, params?: any): void;
}>();

/** Classic late-edition Home layout (pre dual-skin) restored as Newsprint. */

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

const recommendations = computed(() => props.model.dailyTracks.slice(0, 10));

function onHeroPlay() {
  const t = props.model.heroTrack ?? props.model.dailyTracks[0];
  if (t) emit('play-track', t);
}

function onRecPlay(track: Track) {
  emit('play-track', track);
}

function onPlaylistClick(pl: PlaylistInfo) {
  emit('navigate', 'playlist', { id: pl.specialid, name: pl.specialname });
}

function formatDuration(sec: number): string {
  const m = Math.floor(sec / 60);
  const s = Math.floor(sec % 60);
  return `${m}:${String(s).padStart(2, '0')}`;
}

function formatPlays(n: number): string {
  if (!n) return '—';
  if (n >= 10000) return `${Math.floor(n / 10000)}万`;
  return String(n);
}

function formatDate(): string {
  const days = ['日', '一', '二', '三', '四', '五', '六'];
  return `星期${days[new Date().getDay()]} · ${new Date().toLocaleDateString('zh-CN', { month: 'long', day: 'numeric' })}`;
}
</script>

<template>
  <!-- np-home root kept for skin structure tests; body uses classic newspaper classes from style.css -->
  <div class="np-home list-view" data-test="newsprint-home">
    <div class="page-head np-masthead">
      <div>
        <div class="kicker">Late Edition · 晚刊</div>
        <h1>为你精选<i>For You</i></h1>
      </div>
      <div class="date">
        <b>{{ formatDate() }}</b>
        {{ timeOfDayPhrase }}
      </div>
    </div>

    <div
      v-if="model.errors.length"
      class="np-error-summary"
      data-test="home-error-summary"
      role="alert"
    >
      <p>{{ model.errorSummary || '部分内容加载失败' }}</p>
      <button type="button" class="more" data-test="home-error-retry-all" @click="emit('refresh')">
        全部重试
      </button>
    </div>

    <div class="feature">
      <div class="hero">
        <div>
          <div class="label">Daily Picks · 私荐</div>
          <h2>今日适合这几首</h2>
          <p>
            {{
              model.heroTrack
                ? `根据每日推荐为你排好一组歌。想少一点选择困难，就从「${model.heroTrack.SongName}」开始慢慢听。`
                : '根据每日推荐为你排好一组歌。想少一点选择困难，就从第一首开始慢慢听。'
            }}
          </p>
        </div>
        <button
          type="button"
          class="play-cta"
          data-test="hero-play"
          :disabled="!model.heroTrack && !model.dailyTracks.length"
          @click="onHeroPlay"
        >
          <span class="pp">
            <svg viewBox="0 0 24 24" fill="currentColor" width="10" height="10">
              <polygon points="6,4 20,12 6,20" />
            </svg>
          </span>
          {{ model.heroTrack || model.dailyTracks.length ? '立即收听 · 每日推荐' : '暂无推荐可播放' }}
        </button>
        <svg class="hero-art" viewBox="0 0 200 200" fill="none" aria-hidden="true">
          <defs>
            <pattern id="np-ht" width="3" height="3" patternUnits="userSpaceOnUse">
              <circle cx="1" cy="1" r="0.6" fill="rgba(34,27,18,0.45)" />
            </pattern>
          </defs>
          <circle cx="100" cy="100" r="78" fill="rgba(34,27,18,0.06)" />
          <circle cx="100" cy="100" r="78" fill="url(#np-ht)" opacity="0.6" />
          <circle cx="100" cy="100" r="20" fill="var(--accent)" />
          <circle cx="100" cy="100" r="3" fill="var(--paper)" />
          <circle cx="100" cy="100" r="55" fill="none" stroke="rgba(34,27,18,0.25)" stroke-width="0.6" />
          <circle cx="100" cy="100" r="42" fill="none" stroke="rgba(34,27,18,0.25)" stroke-width="0.6" />
          <circle cx="100" cy="100" r="68" fill="none" stroke="rgba(34,27,18,0.25)" stroke-width="0.6" />
        </svg>
      </div>

      <div class="side-list">
        <div class="sl-head">
          <h3>
            每日推荐
            <i style="font-style: italic; font-family: 'EB Garamond', serif; font-weight: 400; color: var(--ink-mute); font-size: 0.7em">
              Daily Picks
            </i>
          </h3>
          <span
            class="more"
            data-test="refresh"
            role="button"
            tabindex="0"
            @click="emit('refresh')"
            @keydown.enter="emit('refresh')"
          >
            {{ model.isRefreshing ? '刷新中…' : '刷新推荐 ↻' }}
          </span>
        </div>

        <ol class="np-rec-list">
          <li
            v-for="(song, idx) in recommendations"
            :key="song.FileHash"
            class="np-rec-item"
            @click="onRecPlay(song)"
          >
            <span class="n np-num">{{ String(idx + 1).padStart(2, '0') }}</span>
            <span class="t">
              <b>{{ song.SongName }}</b>
              <span>{{ song.SingerName }}</span>
            </span>
            <span class="dur">{{ formatDuration(song.Duration) }}</span>
          </li>
          <li v-if="!recommendations.length" style="padding: 10px; font-style: italic; color: var(--ink-mute); cursor: default">
            暂时没有推荐歌曲
          </li>
        </ol>
      </div>
    </div>

    <div v-if="model.playlists.length" class="section-bar">
      <h2>编辑推荐<i>Editor's Picks</i></h2>
      <span class="more">{{ model.isRefreshing ? '刷新中…' : '本周精选' }}</span>
    </div>
    <div v-if="model.playlists.length" class="grid">
      <article
        v-for="pl in model.playlists"
        :key="pl.specialid"
        class="card"
        :data-test="`playlist-${pl.specialid}`"
        @click="onPlaylistClick(pl)"
      >
        <div class="cover">
          <img v-if="pl.imgurl" :src="pl.imgurl" alt="cover" />
          <svg v-else viewBox="0 0 200 200">
            <rect width="200" height="200" fill="#ecdfbe" />
            <text x="100" y="110" text-anchor="middle" font-family="Noto Serif SC" font-weight="700" font-size="24" fill="#221b12">歌单</text>
          </svg>
          <div class="corner">精品</div>
          <button type="button" class="play" aria-label="play" @click.stop="onPlaylistClick(pl)">
            <svg viewBox="0 0 24 24" fill="currentColor" width="14" height="14">
              <polygon points="6,4 20,12 6,20" />
            </svg>
          </button>
        </div>
        <div class="meta-row">
          <div>
            <div class="title">{{ pl.specialname }}</div>
            <div class="sub">By {{ pl.nickname }}</div>
          </div>
          <div class="plays">{{ formatPlays(pl.playcount) }}</div>
        </div>
      </article>
    </div>

    <div v-if="model.albums.length" class="section-bar" style="margin-top: 34px">
      <h2>最新歌单<i>Newly Pressed</i></h2>
      <span class="more">{{ model.isRefreshing ? '刷新中…' : '全部歌单' }}</span>
    </div>
    <div v-if="model.albums.length" class="grid">
      <article
        v-for="pl in model.albums"
        :key="`a-${pl.specialid}`"
        class="card"
        :data-test="`playlist-${pl.specialid}`"
        @click="onPlaylistClick(pl)"
      >
        <div class="cover">
          <img v-if="pl.imgurl" :src="pl.imgurl" alt="cover" />
          <svg v-else viewBox="0 0 200 200">
            <rect width="200" height="200" fill="#dee6d4" />
            <text x="100" y="110" text-anchor="middle" font-family="Noto Serif SC" font-weight="700" font-size="24" fill="#3b5a3a">新碟</text>
          </svg>
          <div class="corner">NEW</div>
          <button type="button" class="play" aria-label="play" @click.stop="onPlaylistClick(pl)">
            <svg viewBox="0 0 24 24" fill="currentColor" width="14" height="14">
              <polygon points="6,4 20,12 6,20" />
            </svg>
          </button>
        </div>
        <div class="meta-row">
          <div>
            <div class="title">{{ pl.specialname }}</div>
            <div class="sub">{{ pl.nickname }}</div>
          </div>
          <div class="plays">NEW</div>
        </div>
      </article>
    </div>
  </div>
</template>

<script lang="ts">
export default { name: 'NewsprintHome' };
</script>

<style scoped>
.np-home {
  box-sizing: border-box;
  width: 100%;
  max-width: 100%;
  margin-inline: 0;
  padding-bottom: 28px;
  min-width: 0;
}

:deep(.grid) {
  grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
  width: 100%;
}

.np-error-summary {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 12px;
  margin-bottom: 16px;
  border: 1px solid var(--rule);
  color: var(--accent);
  font-size: 13px;
  font-style: italic;
}

.np-error-summary p {
  margin: 0;
}

.play-cta:disabled {
  opacity: 0.45;
  cursor: not-allowed;
}

/* Dark mode still uses paper-ink variables via tokens */
:global(:root[data-mode='dark']) .feature .side-list {
  background: rgba(255, 252, 243, 0.04);
}

@media (max-width: 960px) {
  :deep(.feature) {
    grid-template-columns: 1fr;
  }
  :deep(.grid) {
    grid-template-columns: repeat(3, minmax(0, 1fr));
  }
}

@media (max-width: 640px) {
  :deep(.grid) {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

@media (min-width: 1600px) {
  .np-home {
    max-width: 1440px;
  }
}
</style>
