<script setup lang="ts">
import { ref, onMounted, watch } from 'vue';
import { playerStore, playTrack } from '../api/playerStore';
import { fetchCoverImage } from '../api/normalizer';

defineProps<{
  collapsed: boolean;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
}>();

// Tweak variables
const paperWarmth = ref(parseInt(localStorage.getItem('tweak_warmth') || '32', 10));
const glassBlur = ref(parseInt(localStorage.getItem('tweak_blur') || '22', 10));
const grainAmount = ref(parseInt(localStorage.getItem('tweak_grain') || '28', 10));
const accent = ref(localStorage.getItem('tweak_accent') || '#a8311b');

const accentsList = [
  { value: '#a8311b', deep: '#7a2010' },
  { value: '#1f5a3a', deep: '#133b25' },
  { value: '#2a4a7a', deep: '#192e4f' },
  { value: '#7a4a1f', deep: '#522f11' }
];

function applyTweaks() {
  const w = paperWarmth.value / 100;
  const paper = `hsl(${42 - w * 4} ${20 + w * 22}% ${91 - w * 2}%)`;
  const paper2 = `hsl(${42 - w * 4} ${22 + w * 22}% ${86 - w * 2}%)`;
  const paperEdge = `hsl(${42 - w * 4} ${20 + w * 18}% ${80 - w * 2}%)`;

  const root = document.documentElement;
  root.style.setProperty('--paper', paper);
  root.style.setProperty('--paper-2', paper2);
  root.style.setProperty('--paper-edge', paperEdge);
  root.style.setProperty('--glass-blur', `${glassBlur.value}px`);
  root.style.setProperty('--grain', String(grainAmount.value / 100));
  root.style.setProperty('--accent', accent.value);
  
  const accentObj = accentsList.find(a => a.value === accent.value);
  if (accentObj) {
    root.style.setProperty('--accent-deep', accentObj.deep);
  }
}

watch([paperWarmth, glassBlur, grainAmount, accent], () => {
  localStorage.setItem('tweak_warmth', String(paperWarmth.value));
  localStorage.setItem('tweak_blur', String(glassBlur.value));
  localStorage.setItem('tweak_grain', String(grainAmount.value));
  localStorage.setItem('tweak_accent', accent.value);
  applyTweaks();
});

onMounted(() => {
  applyTweaks();
  fetchMissingCovers();
});

watch(() => playerStore.queue, () => {
  fetchMissingCovers();
}, { deep: true });

async function fetchMissingCovers() {
  for (const item of playerStore.queue) {
    if (!item.Image) {
      // Avoid fetching again if we already started or it's empty
      item.Image = ''; 
      fetchCoverImage(item.FileHash).then(img => {
        if (img) {
          item.Image = img;
          localStorage.setItem('player_queue', JSON.stringify(playerStore.queue));
        }
      });
    }
  }
}

const recentArtists = [
  { name: '周杰伦', color: '#3b3022', textFill: '#e9d7b3' },
  { name: '陈奕迅', color: '#6a4a32', textFill: '#f3dcb6' },
  { name: '周深', color: '#4a3a2a', textFill: '#ecd3a8' },
  { name: '毛不易', color: '#80553a', textFill: '#f6e0bb' }
];
</script>

<template>
  <aside class="drawer" :class="{ collapsed: collapsed }">
    <div class="drawer-head">
      <div class="dot-row"><span></span><span></span><span></span></div>
      <div class="title">SIDE PANEL · 抽屉</div>
      <button class="drawer-close" aria-label="close" @click="emit('close')">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
          <path d="M6 6l12 12M6 18L18 6"/>
        </svg>
      </button>
    </div>
    
    <div class="drawer-scroll">
      <!-- Tweaks Panel -->
      <div class="drawer-section">
        <h4>版面控制 <span class="en">Tweaks</span></h4>
        
        <div class="tweak-row">
          <label>纸张暖度 Paper warmth <span>{{ paperWarmth }}</span></label>
          <input type="range" v-model.number="paperWarmth" min="0" max="100" />
        </div>
        
        <div class="tweak-row">
          <label>玻璃模糊 Glass blur <span>{{ glassBlur }}</span>px</label>
          <input type="range" v-model.number="glassBlur" min="6" max="40" />
        </div>
        
        <div class="tweak-row">
          <label>纸张颗粒 Grain <span>{{ (grainAmount / 100).toFixed(2) }}</span></label>
          <input type="range" v-model.number="grainAmount" min="0" max="80" />
        </div>
        
        <div class="tweak-row">
          <label>强调色 Accent</label>
          <div class="swatches">
            <div 
              v-for="color in accentsList" 
              :key="color.value"
              class="sw" 
              :class="{ sel: accent === color.value }"
              :style="{ background: color.value }"
              @click="accent = color.value"
            ></div>
          </div>
        </div>
      </div>

      <!-- Artists -->
      <div class="drawer-section">
        <h4>常听艺人 <span class="en">Artists</span></h4>
        <div class="artists">
          <div v-for="art in recentArtists" :key="art.name" class="artist">
            <div class="ah">
              <svg viewBox="0 0 50 50">
                <rect width="50" height="50" :fill="art.color"/>
                <circle cx="25" cy="20" r="9" :fill="art.textFill"/>
                <ellipse cx="25" cy="44" rx="14" ry="10" :fill="art.textFill"/>
              </svg>
            </div>
            <div class="nm">{{ art.name }}</div>
          </div>
        </div>
      </div>

      <!-- Queue/Recent -->
      <div class="drawer-section">
        <h4>当前播放队列 <span class="en">Queue</span></h4>
        <div class="recent">
          <div 
            v-for="item in playerStore.queue.slice(0, 10)" 
            :key="item.FileHash" 
            class="item"
            @click="playTrack(item)"
          >
            <div class="mini">
              <img v-if="item.Image" :src="item.Image" alt="c" style="width:100%;height:100%;object-fit:cover;" />
              <svg v-else viewBox="0 0 36 36">
                <rect width="36" height="36" fill="#a8311b"/>
                <text x="18" y="22" text-anchor="middle" font-family="EB Garamond, serif" font-style="italic" font-size="12" fill="#f1ead8">
                  {{ item.SongName.slice(0, 2) }}
                </text>
              </svg>
            </div>
            <div class="info">
              <b>{{ item.SongName }}</b>
              <span>{{ item.SingerName }}</span>
            </div>
            <div class="dur">{{ Math.floor(item.Duration / 60) }}:{{ String(item.Duration % 60).padStart(2, '0') }}</div>
          </div>
          <div v-if="playerStore.queue.length === 0" style="padding: 16px; text-align: center; color: var(--ink-mute); font-style: italic;">
            队列为空
          </div>
        </div>
      </div>
    </div>
  </aside>
</template>

<style scoped>
/* Scoped tweaks styling */
</style>
