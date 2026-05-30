<script setup lang="ts">
import { ref, onMounted, computed } from 'vue';
import { apiGet } from '../api/backend';
import { playTrack, playAll } from '../api/playerStore';
import { Track as SongInfo, normalizeTrack } from '../api/normalizer';

// Subtitle below the date — varies by hour so the "晚刊" feel is honest.
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

interface PlaylistInfo {
  specialid: number;
  specialname: string;
  imgurl: string;
  nickname: string;
  playcount: number;
  [key: string]: any;
}


const emit = defineEmits<{
  (e: 'navigate', view: string, params?: any): void;
}>();

const loading = ref(true);
const trendingSongs = ref<SongInfo[]>([]);
const recommendedPlaylists = ref<PlaylistInfo[]>([]);
const newAlbums = ref<PlaylistInfo[]>([]);

// A solid default track to play for the Headline
const headlineTrack = {
  FileHash: 'F2D87B5E148C20020020020020020020', // Placeholder hash
  SongName: '读懂一首歌 (晚秋)',
  SingerName: '毛不易 / 诗意精选',
  Duration: 249,
};

async function loadHomeData() {
  loading.value = true;
  try {
    // 1. Fetch trending songs for the sidebar list
    const songRes = await apiGet<any>('/everyday/recommend', {
      pagesize: 6
    });
    const songData = songRes.data?.data || songRes.data || {};
    const songList = songData.song_list || songData.info || songData.list;
    if (songRes.status === 1 && songList && songList.length > 0) {
      trendingSongs.value = songList.slice(0, 6).map(normalizeTrack);
    } else {
      // Fallback if everyday recommend is empty (e.g. not logged in)
      const fallbackRes = await apiGet<any>('/top/song', { pagesize: 6 });
      const fData = fallbackRes.data?.data || fallbackRes.data || {};
      const fList = fData.info || fData.list;
      if (fallbackRes.status === 1 && fList) {
        trendingSongs.value = fList.slice(0, 6).map(normalizeTrack);
      }
    }

    // 2. Fetch playlists for Editor's Picks
    const plRes = await apiGet<any>('/top/playlist', {
      pagesize: 5,
      sort: 2
    });
    const plData = plRes.data?.data || plRes.data || {};
    const plList = plData.info || plData.list;
    if (plRes.status === 1 && plList) {
      recommendedPlaylists.value = plList.slice(0, 5).map((pl: any) => ({
        ...pl,
        imgurl: pl.imgurl ? pl.imgurl.replace('{size}', '400') : pl.pic_url ? pl.pic_url.replace('{size}', '400') : ''
      }));
    }

    // 3. Fetch newly pressed playlists
    const newRes = await apiGet<any>('/top/playlist', {
      pagesize: 5,
      sort: 5 // Newest
    });
    const newPlData = newRes.data?.data || newRes.data || {};
    const newPlList = newPlData.info || newPlData.list;
    if (newRes.status === 1 && newPlList) {
      newAlbums.value = newPlList.slice(0, 5).map((pl: any) => ({
        ...pl,
        imgurl: pl.imgurl ? pl.imgurl.replace('{size}', '400') : pl.pic_url ? pl.pic_url.replace('{size}', '400') : ''
      }));
    }
  } catch (e) {
    console.error('Failed to load home data, fallback to mock', e);
  } finally {
    loading.value = false;
  }
}

onMounted(() => {
  loadHomeData();
});

function handlePlaySong(song: SongInfo) {
  // 用整个热门列表作为播放队列，从点击的这首开始。
  const idx = trendingSongs.value.findIndex(s => s.FileHash === song.FileHash);
  playAll(trendingSongs.value, idx >= 0 ? idx : 0);
}

function handlePlaylistClick(playlist: PlaylistInfo) {
  emit('navigate', 'playlist', { id: playlist.specialid || playlist.id, name: playlist.specialname || playlist.name });
}

function playHeadline() {
  // Try to play first trending song, or fallback to mock headline
  if (trendingSongs.value.length > 0) {
    handlePlaySong(trendingSongs.value[0]);
  } else {
    playTrack(headlineTrack);
  }
}
</script>

<template>
  <div class="list-view">
    <div class="page-head">
      <div>
        <div class="kicker">Late Edition · 晚刊</div>
        <h1>为你精选<i>For You</i></h1>
      </div>
      <div class="date">
        <b>星期{{ ['日','一','二','三','四','五','六'][new Date().getDay()] }} · {{ new Date().toLocaleDateString('zh-CN', { month: 'long', day: 'numeric' }) }}</b>
        {{ timeOfDayPhrase }}
      </div>
    </div>

    <!-- FEATURE row -->
    <div class="feature">
      <div class="hero">
        <div>
          <div class="label">Headline · 头条</div>
          <h2>读懂一首歌</h2>
          <p>每周一期 · 编辑部为你拆解一段旋律的来路与去处。本期：从〈晚秋〉到〈八月将军令〉，民谣在城市边缘的二十年。</p>
        </div>
        <button class="play-cta" @click="playHeadline">
          <span class="pp">
            <svg viewBox="0 0 24 24" fill="currentColor" width="10" height="10">
              <polygon points="6,4 20,12 6,20"/>
            </svg>
          </span>
          立即收听 · 推荐头条
        </button>
        <!-- Engraved Circle Art -->
        <svg class="hero-art" viewBox="0 0 200 200" fill="none">
          <defs>
            <pattern id="ht" width="3" height="3" patternUnits="userSpaceOnUse">
              <circle cx="1" cy="1" r="0.6" fill="rgba(34,27,18,0.45)"/>
            </pattern>
          </defs>
          <circle cx="100" cy="100" r="78" fill="rgba(34,27,18,0.06)"/>
          <circle cx="100" cy="100" r="78" fill="url(#ht)" opacity="0.6"/>
          <circle cx="100" cy="100" r="20" fill="var(--accent)"/>
          <circle cx="100" cy="100" r="3" fill="var(--paper)"/>
          <circle cx="100" cy="100" r="55" fill="none" stroke="rgba(34,27,18,0.25)" stroke-width="0.6"/>
          <circle cx="100" cy="100" r="42" fill="none" stroke="rgba(34,27,18,0.25)" stroke-width="0.6"/>
          <circle cx="100" cy="100" r="68" fill="none" stroke="rgba(34,27,18,0.25)" stroke-width="0.6"/>
        </svg>
      </div>
      
      <!-- Trending sidebar list -->
      <div class="side-list">
        <div class="sl-head">
          <h3>每周飙升 <i style="font-style:italic;font-family:'EB Garamond',serif;font-weight:400;color:var(--ink-mute);font-size:.7em">Trending</i></h3>
          <span class="more" @click="loadHomeData">刷新自检 ↻</span>
        </div>
        
        <div v-if="loading && trendingSongs.length === 0" class="spinner">
          <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round">
            <circle cx="12" cy="12" r="10" stroke="rgba(34,27,18,0.1)"></circle>
            <path d="M12 2a10 10 0 0 1 10 10" stroke="currentColor"></path>
          </svg>
          正在拉取电讯…
        </div>
        
        <ol v-else>
          <li 
            v-for="(song, idx) in trendingSongs" 
            :key="song.FileHash"
            @click="handlePlaySong(song)"
          >
            <span class="n">0{{ idx + 1 }}</span>
            <span class="t">
              <b>{{ song.SongName }}</b>
              <span>{{ song.SingerName }}</span>
            </span>
            <span class="dur">{{ Math.floor(song.Duration / 60) }}:{{ String(song.Duration % 60).padStart(2, '0') }}</span>
          </li>
          <li v-if="trendingSongs.length === 0" style="padding: 10px; font-style: italic; color: var(--ink-mute);">
            暂时没有热门歌曲
          </li>
        </ol>
      </div>
    </div>

    <!-- Grid 1: Editor's picks -->
    <div class="section-bar">
      <h2>编辑推荐<i>Editor's Picks</i></h2>
      <span class="more">本周精选 →</span>
    </div>

    <div v-if="loading && recommendedPlaylists.length === 0" class="spinner">
      加载推荐歌单中…
    </div>
    
    <div v-else class="grid">
      <article 
        v-for="pl in recommendedPlaylists" 
        :key="pl.specialid" 
        class="card"
        @click="handlePlaylistClick(pl)"
      >
        <div class="cover">
          <img v-if="pl.imgurl" :src="pl.imgurl" alt="cover" />
          <svg v-else viewBox="0 0 200 200">
            <rect width="200" height="200" fill="#ecdfbe"/>
            <text x="100" y="110" text-anchor="middle" font-family="Noto Serif SC" font-weight="700" font-size="24" fill="#221b12">歌单</text>
          </svg>
          <div class="corner">精品</div>
          <button class="play" aria-label="play">
            <svg viewBox="0 0 24 24" fill="currentColor" width="14" height="14">
              <polygon points="6,4 20,12 6,20"/>
            </svg>
          </button>
        </div>
        <div class="meta-row">
          <div>
            <div class="title">{{ pl.specialname }}</div>
            <div class="sub">By {{ pl.nickname }}</div>
          </div>
          <div class="plays">{{ pl.playcount > 10000 ? Math.floor(pl.playcount/10000)+'万' : pl.playcount }}次</div>
        </div>
      </article>
      
      <!-- Mock cards if list empty -->
      <template v-if="recommendedPlaylists.length === 0">
        <div style="grid-column: span 5; text-align: center; color: var(--ink-mute); font-style: italic; padding: 20px;">
          暂无歌单推荐
        </div>
      </template>
    </div>

    <!-- Grid 2: Newly pressed -->
    <div class="section-bar" style="margin-top: 34px;">
      <h2>最新歌单<i>Newly Pressed</i></h2>
      <span class="more">全部歌单 →</span>
    </div>

    <div v-if="loading && newAlbums.length === 0" class="spinner">
      新近发布更新中…
    </div>
    
    <div v-else class="grid">
      <article 
        v-for="pl in newAlbums" 
        :key="pl.specialid" 
        class="card"
        @click="handlePlaylistClick(pl)"
      >
        <div class="cover">
          <img v-if="pl.imgurl" :src="pl.imgurl" alt="cover" />
          <svg v-else viewBox="0 0 200 200">
            <rect width="200" height="200" fill="#dee6d4"/>
            <text x="100" y="110" text-anchor="middle" font-family="Noto Serif SC" font-weight="700" font-size="24" fill="#3b5a3a">新碟</text>
          </svg>
          <div class="corner">NEW</div>
          <button class="play" aria-label="play">
            <svg viewBox="0 0 24 24" fill="currentColor" width="14" height="14">
              <polygon points="6,4 20,12 6,20"/>
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
      
      <template v-if="newAlbums.length === 0">
        <div style="grid-column: span 5; text-align: center; color: var(--ink-mute); font-style: italic; padding: 20px;">
          暂无最新发布
        </div>
      </template>
    </div>
  </div>
</template>

<style scoped>
/* Scoped styles */
</style>
