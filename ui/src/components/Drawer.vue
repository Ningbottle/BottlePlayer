<script setup lang="ts">
import { ref, onMounted, watch } from 'vue';

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

const isCompact = ref(localStorage.getItem('tweak_compact') === 'true');
const lyricAlign = ref(localStorage.getItem('tweak_lyric_align') || 'center');
const fontFamily = ref(localStorage.getItem('tweak_font') || 'serif');
const bgImageUrl = ref(localStorage.getItem('tweak_bg_img') || '');
const bgDim = ref(parseInt(localStorage.getItem('tweak_bg_dim') || '50', 10));

const accentsList = [
  { value: '#a8311b', deep: '#7a2010' },
  { value: '#1f5a3a', deep: '#133b25' },
  { value: '#2a4a7a', deep: '#192e4f' },
  { value: '#7a4a1f', deep: '#522f11' }
];

function handleBgUpload(e: Event) {
  const file = (e.target as HTMLInputElement).files?.[0];
  if (file) {
    const reader = new FileReader();
    reader.onload = (ev) => {
      bgImageUrl.value = ev.target?.result as string;
    };
    reader.readAsDataURL(file);
  }
}

function clearBg() {
  bgImageUrl.value = '';
}

function applyTweaks() {
  const root = document.documentElement;
  const w = paperWarmth.value / 100;
  // Warmth is a micro-adjustment overlay on top of the skin's base --paper.
  // We write a --warmth variable that style.css can blend, NOT --paper directly.
  root.style.setProperty('--warmth', String(w));

  root.style.setProperty('--glass-blur', `${glassBlur.value}px`);
  root.style.setProperty('--grain', String(grainAmount.value / 100));
  root.style.setProperty('--accent', accent.value);

  const accentObj = accentsList.find(a => a.value === accent.value);
  if (accentObj) {
    root.style.setProperty('--accent-deep', accentObj.deep);
  }

  if (isCompact.value) {
    root.classList.add('compact');
  } else {
    root.classList.remove('compact');
  }

  if (lyricAlign.value === 'left') {
    root.classList.add('lyric-left');
  } else {
    root.classList.remove('lyric-left');
  }

  if (bgImageUrl.value) {
    root.style.setProperty('--custom-bg', `url("${bgImageUrl.value}")`);
    root.style.setProperty('--custom-bg-dim', String(bgDim.value / 100));
    root.classList.add('has-custom-bg');
  } else {
    root.style.setProperty('--custom-bg', 'none');
    root.style.setProperty('--custom-bg-dim', '0');
    root.classList.remove('has-custom-bg');
  }

  if (fontFamily.value === 'cute') {
    root.style.setProperty('--font-serif', 'var(--font-cute)');
  } else {
    root.style.setProperty('--font-serif',
      '"Noto Serif SC", "EB Garamond", "Songti SC", "STSong", "Times New Roman", Georgia, "Microsoft YaHei", serif');
  }
}

watch([paperWarmth, glassBlur, grainAmount, accent, isCompact, lyricAlign, bgImageUrl, bgDim, fontFamily], () => {
  localStorage.setItem('tweak_warmth', String(paperWarmth.value));
  localStorage.setItem('tweak_blur', String(glassBlur.value));
  localStorage.setItem('tweak_grain', String(grainAmount.value));
  localStorage.setItem('tweak_accent', accent.value);
  localStorage.setItem('tweak_compact', String(isCompact.value));
  localStorage.setItem('tweak_lyric_align', lyricAlign.value);
  try {
    localStorage.setItem('tweak_bg_img', bgImageUrl.value);
  } catch (e) {
    console.warn("Background image too large to save in localStorage");
  }
  localStorage.setItem('tweak_bg_dim', String(bgDim.value));
  localStorage.setItem('tweak_font', fontFamily.value);
  applyTweaks();
});

onMounted(() => {
  applyTweaks();
});

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
          <label>紧凑列表 Compact List</label>
          <input type="checkbox" v-model="isCompact" />
        </div>

        <div class="tweak-row">
          <label>歌词对齐 Lyric Align</label>
          <select v-model="lyricAlign" class="tweak-select">
            <option value="center">居中 Center</option>
            <option value="left">靠左 Left</option>
          </select>
        </div>

        <div class="tweak-row">
          <label>字体 Font</label>
          <select v-model="fontFamily" class="tweak-select">
            <option value="serif">衬线 Serif</option>
            <option value="cute">可爱 Cute</option>
          </select>
        </div>

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

      <!-- Background Customization -->
      <div class="drawer-section">
        <h4>自定义背景 <span class="en">Background</span></h4>
        
        <div class="tweak-row">
          <label>图片 Image</label>
          <div style="display: flex; gap: 8px; align-items: center;">
            <input type="file" accept="image/*" @change="handleBgUpload" style="max-width: 140px; font-size: 12px;"/>
            <button v-if="bgImageUrl" @click="clearBg" class="clear-btn">清除</button>
          </div>
        </div>

        <div class="tweak-row" v-if="bgImageUrl">
          <label>背景暗度 Dimming <span>{{ bgDim }}%</span></label>
          <input type="range" v-model.number="bgDim" min="0" max="100" />
        </div>
      </div>
    </div>
  </aside>
</template>

<style scoped>
.tweak-row input[type="checkbox"] {
  accent-color: var(--accent);
  width: 16px;
  height: 16px;
  cursor: pointer;
}
.tweak-select {
  background: var(--paper-2);
  border: 1px solid var(--rule);
  border-radius: 4px;
  padding: 3px 8px;
  font-size: 12px;
  color: var(--ink-soft);
  cursor: pointer;
}
.clear-btn {
  background: var(--paper-2);
  border: 1px solid var(--rule);
  border-radius: 4px;
  padding: 2px 10px;
  font-size: 12px;
  color: var(--ink-soft);
  cursor: pointer;
  transition: background 0.15s, color 0.15s, border-color 0.15s;
}
.clear-btn:hover {
  background: var(--accent);
  color: #fff;
  border-color: var(--accent);
}
</style>
