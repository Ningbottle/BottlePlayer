<script setup lang="ts">
import { computed } from 'vue';
import type { HomeViewModel } from './homeViewModel';
import type { Track } from '../../shared/music/track';
import type { HomeSection, PlaylistInfo } from './homeFeedStore';
import { ArrowRight } from '@lucide/vue';

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
const featureTrack = computed(() => props.model.dailyTracks[0] ?? props.model.heroTrack);

function onHeroPlay() {
  const t = featureTrack.value;
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

function sectionStatus(section: HomeSection, idle: string): string {
  const state = props.model.sections[section];
  if (state.error) return '重试';
  if (state.loading) return '加载中…';
  if (state.refreshing) return '刷新中…';
  return state.isEmpty ? '暂无内容' : idle;
}

function retrySection(section: HomeSection): void {
  void props.model.sections[section].retry();
}
</script>

<template>
  <!-- np-home root kept for skin structure tests; body uses classic newspaper classes from style.css -->
  <div class="np-home list-view" data-test="newsprint-home">
    <div class="page-head np-masthead">
      <div>
        <div class="kicker">晚刊 · Late Edition</div>
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

    <div
      v-if="!model.heroTrack && !model.dailyTracks.length && model.sections.daily.loading"
      class="newsprint-stage-loading"
      data-test="newsprint-stage-loading"
      aria-busy="true"
      aria-live="polite"
      aria-label="正在加载每日推荐"
    >
      <div class="newsprint-skeleton-masthead" aria-hidden="true" />
      <div class="newsprint-skeleton-copy" aria-hidden="true">
        <span class="newsprint-skeleton-line newsprint-skeleton-title" />
        <span class="newsprint-skeleton-line" />
        <span class="newsprint-skeleton-line newsprint-skeleton-short" />
      </div>
    </div>

    <div
      v-else-if="!model.heroTrack && !model.dailyTracks.length"
      class="newsprint-stage-empty"
      data-test="newsprint-stage-empty"
    >
      <div class="label">今日无推荐 · 私荐</div>
      <h2>还没有可播放的歌曲</h2>
      <p>刷新每日推荐，找到下一首适合此刻的歌。</p>
      <div class="newsprint-empty-actions">
        <button
          type="button"
          class="play-cta"
          data-test="hero-play"
          disabled
        >暂无推荐可播放</button>
        <button
          type="button"
          class="more"
          data-test="newsprint-empty-retry"
          :disabled="model.sections.daily.loading || model.sections.daily.refreshing"
          @click="retrySection('daily')"
        >
          {{ model.sections.daily.error ? '重试' : model.sections.daily.refreshing ? '刷新中…' : '刷新推荐' }}
        </button>
      </div>
    </div>

    <div v-else class="feature">
      <div class="hero">
        <div>
          <div class="label">私荐 · Daily Picks</div>
          <h2>今日适合这几首</h2>
          <p>
            {{
              featureTrack
                ? `根据每日推荐为你排好一组歌。想少一点选择困难，就从「${featureTrack.SongName}」开始慢慢听。`
                : '根据每日推荐为你排好一组歌。想少一点选择困难，就从第一首开始慢慢听。'
            }}
          </p>
        </div>
        <button
          type="button"
          class="play-cta"
          data-test="hero-play"
          :disabled="!featureTrack"
          @click="onHeroPlay"
        >
          <span class="pp">
            <svg viewBox="0 0 24 24" fill="currentColor" width="10" height="10">
              <polygon points="6,4 20,12 6,20" />
            </svg>
          </span>
          {{ featureTrack ? '立即收听 · 每日推荐' : '暂无推荐可播放' }}
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
            role="button"
            tabindex="0"
            data-test="daily-section-status"
            @click="retrySection('daily')"
            @keydown.enter="retrySection('daily')"
          >
            {{ sectionStatus('daily', '刷新推荐 ↻') }}
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
            {{ sectionStatus('daily', '暂时没有推荐歌曲') }}
          </li>
        </ol>
      </div>
    </div>

    <div v-if="model.playlists.length || model.sections.playlists.loading || model.sections.playlists.error || model.sections.playlists.isEmpty" class="section-bar">
      <h2>编辑推荐<i>Editor's Picks</i></h2>
      <button
        v-if="model.sections.playlists.error"
        type="button"
        class="more"
        data-test="playlists-section-retry"
        @click="retrySection('playlists')"
      >重试</button>
      <span v-else class="more" data-test="playlists-section-status">{{ sectionStatus('playlists', '本周精选') }}</span>
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
          <button
            type="button"
            class="play"
            :data-test="`playlist-open-${pl.specialid}`"
            :aria-label="`打开歌单：${pl.specialname}`"
            :title="`打开歌单：${pl.specialname}`"
            @click.stop="onPlaylistClick(pl)"
          >
            <ArrowRight :size="14" :stroke-width="1.8" aria-hidden="true" />
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

    <div v-if="model.albums.length || model.sections.albums.loading || model.sections.albums.error || model.sections.albums.isEmpty" class="section-bar" style="margin-top: 34px">
      <h2>最新歌单<i>Newly Pressed</i></h2>
      <button
        v-if="model.sections.albums.error"
        type="button"
        class="more"
        data-test="albums-section-retry"
        @click="retrySection('albums')"
      >重试</button>
      <span v-else class="more" data-test="albums-section-status">{{ sectionStatus('albums', '全部歌单') }}</span>
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
          <button
            type="button"
            class="play"
            :data-test="`album-open-${pl.specialid}`"
            :aria-label="`打开歌单：${pl.specialname}`"
            :title="`打开歌单：${pl.specialname}`"
            @click.stop="onPlaylistClick(pl)"
          >
            <ArrowRight :size="14" :stroke-width="1.8" aria-hidden="true" />
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

/* feature row */
.feature {
  display:grid;
  grid-template-columns: 1.15fr 1fr;
  gap: 22px;
  margin-bottom: 30px;
}
.feature .hero {
  position: relative;
  border: 1px solid var(--rule);
  background: var(--paper-2);
  padding: 22px;
  overflow:hidden;
  min-height: 220px;
  display:flex; flex-direction:column; justify-content:space-between;
}
.feature .hero::before {
  content:""; position:absolute; inset:6px;
  border: 1px dashed rgba(34,27,18,0.12);
  pointer-events:none;
}
.feature .hero .label {
  font-family:"EB Garamond",serif; font-style:italic; font-size: 12px;
  letter-spacing: 0.16em; text-transform: uppercase; color: var(--ink-mute);
}
.feature .hero h2 {
  font-size: 30px; margin: 6px 0 4px; line-height: 1.05; max-width: 78%;
  font-weight: 600;
}
.feature .hero p {
  font-size: 13px; color: var(--ink-soft); max-width: 70%; line-height: 1.6;
  margin: 4px 0 0;
}
.feature .hero .hero-art {
  position: absolute; right: -10px; bottom: -10px; width: 200px; height: 200px;
  opacity:.95;
}
.feature .hero .play-cta {
  align-self: flex-start;
  display:inline-flex; align-items:center; gap:10px;
  background: var(--ink); color: var(--paper);
  padding: 8px 16px 8px 8px; border-radius: 999px;
  font-size: 13px; letter-spacing: 0.08em;
  cursor:pointer;
  margin-top: 14px;
  z-index: 2;
}
.feature .hero .play-cta .pp {
  width:22px; height:22px; background: var(--accent);
  border-radius:50%; display:grid; place-items:center; color: var(--paper);
}

.feature .side-list {
  border: 1px solid var(--rule);
  background: rgba(255,252,243,0.35);
  padding: 14px 16px;
  display:flex; flex-direction:column;
}
.side-list .sl-head {
  display:flex; align-items:baseline; justify-content:space-between;
  padding-bottom: 8px; margin-bottom: 6px;
  border-bottom: 1px solid var(--rule);
}
.side-list .sl-head h3 {
  margin:0; font-size: 16px; font-weight: 600; letter-spacing: 0.02em;
}
.side-list .sl-head .more {
  font-family:"EB Garamond",serif; font-style:italic; font-size: 11px; color: var(--ink-mute);
}
.side-list ol { list-style:none; margin:0; padding:0; }
.side-list li {
  display:grid; grid-template-columns: 22px 1fr auto; gap: 10px; align-items:center;
  padding: 6px 0;
  border-bottom: 1px dotted var(--rule-soft);
  font-size: 13px;
  cursor: pointer;
}
.side-list li:hover { background: rgba(34,27,18,0.02); }
.side-list li:last-child { border-bottom: none; }
.side-list li .n {
  font-family:"EB Garamond",serif; font-style:italic; color: var(--ink-mute); font-size: 14px;
  text-align: right;
}
.side-list li .t { display:flex; flex-direction:column; }
.side-list li .t b { font-weight: 500; }
.side-list li .t span { font-size: 11px; color: var(--ink-mute); }
.side-list li .dur { font-family:"EB Garamond",serif; font-style:italic; color: var(--ink-mute); font-size:12px; }

/* ---------- Card grid ---------- */
.section-bar {
  display:flex; align-items:baseline; justify-content:space-between;
  margin: 22px 0 14px;
  padding-bottom: 6px;
  border-bottom: 1px solid var(--rule);
}
.section-bar h2 {
  margin:0; font-size: 20px; font-weight: 600; letter-spacing: 0.02em;
}
.section-bar h2 i {
  font-family:"EB Garamond",serif; font-weight:400; font-style:italic; color: var(--ink-mute); font-size: .8em; margin-left:6px;
}
.section-bar .more {
  font-family:"EB Garamond",serif; font-style: italic;
  font-size: 12px; color: var(--ink-mute); cursor:pointer;
}

.grid {
  display: grid;
  /* 5 on typical width; more columns when maximized */
  grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
  gap: 24px 22px;
  width: 100%;
}
.card {
  display:flex; flex-direction:column; gap:10px;
  cursor:pointer;
}
.card .cover {
  position: relative;
  aspect-ratio: 1/1;
  border: 1px solid var(--rule);
  background: var(--paper-2);
  overflow: hidden;
  box-shadow: 0 2px 0 -1px var(--paper-edge), 0 1px 12px -6px rgba(40,28,12,0.25);
}
.card .cover::after {
  content:""; position:absolute; inset:0;
  background-image: radial-gradient(rgba(34,27,18,0.5) 0.5px, transparent 1px);
  background-size: 2.5px 2.5px;
  mix-blend-mode: multiply;
  opacity: .25;
  pointer-events:none;
}
.card .cover img { width:100%; height:100%; object-fit: cover; display:block; }
.card .cover svg { width:100%; height:100%; display:block; }
.card .cover .corner {
  position:absolute; top:8px; left:8px;
  font-family:"EB Garamond",serif; font-style:italic;
  font-size: 10px; color: rgba(255,250,236,0.85);
  letter-spacing: 0.1em; text-transform: uppercase;
  mix-blend-mode: difference;
}
.card .cover .play {
  position:absolute; right:10px; bottom:10px;
  width: 34px; height: 34px; border-radius:50%;
  background: var(--paper);
  backdrop-filter: none;
  -webkit-backdrop-filter: none;
  border: 1px solid var(--rule);
  display:grid; place-items:center;
  color: var(--ink);
  opacity:0; transform: translateY(4px);
  transition: opacity .2s ease, transform .2s ease;
}
.card:hover .play { opacity:1; transform:none; }
.card .meta-row {
  display:flex; align-items:baseline; justify-content:space-between;
  gap: 8px;
}
.card .title {
  font-size: 14px; font-weight: 500; line-height:1.3;
  display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; overflow:hidden;
}
.card .sub {
  font-family:"EB Garamond","Noto Serif SC",serif;
  font-style: italic; font-size: 11px; color: var(--ink-mute);
  margin-top: 2px;
}
.card .plays {
  font-family:"EB Garamond",serif; font-style:italic;
  font-size: 11px; color: var(--ink-mute);
  white-space:nowrap;
}

/* Dark-mode overrides (migrated from style.css; values verbatim).
   Every selector is qualified by the .np-home root and wrapped whole in
   :global — a partial :global(:root) prefix makes Vue's scoped compiler
   drop the descendant part, and unqualified rules would leak globally. */
:global(:root[data-mode='dark'] .np-home .card .cover::after) { mix-blend-mode: normal; opacity: .06; }
:global(:root[data-mode='dark'] .np-home .feature .hero::before) { border-color: rgba(255,255,255,0.08); }
:global(:root[data-mode='dark'] .np-home .side-list li:hover) { background: rgba(255, 255, 255, 0.04); }
:global(:root[data-mode='dark'] .np-home .card) { box-shadow: 0 2px 0 -1px var(--paper-edge), 0 1px 12px -6px rgba(0,0,0,0.5); }
:global(:root[data-mode='dark'] .np-home .hero-art circle[fill="rgba(34,27,18,0.06)"]) { fill: rgba(255,255,255,0.08); }
:global(:root[data-mode='dark'] .np-home .hero-art circle[fill="rgba(34,27,18,0.45)"]) { fill: rgba(255,255,255,0.3); }
:global(:root[data-mode='dark'] .np-home .hero-art circle[stroke="rgba(34,27,18,0.25)"]) { stroke: rgba(255,255,255,0.2); }
:global(:root[data-mode='dark'] .np-home .card .cover .play) { background: rgba(0,0,0,0.55); border-color: rgba(255,255,255,0.12); color: #fff; }

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

/* Dark mode still uses paper-ink variables via tokens.
   Qualified by .np-home and wrapped whole in :global — the previous partial
   form compiled to ":root[data-mode='dark'] { background: ... }", painting
   the document root instead of the side list. */
:global(:root[data-mode='dark'] .np-home .feature .side-list) {
  background: rgba(255, 255, 255, 0.04);
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
</style>
