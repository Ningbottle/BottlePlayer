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

const isDarkMode = ref(localStorage.getItem('tweak_dark') === 'true');
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

  if (isDarkMode.value) {
    root.classList.add('dark');
    // VS Code Dark+ style palette with clear depth layers
    root.style.setProperty('--paper', '#1e1e1e');
    root.style.setProperty('--paper-2', '#252526');
    root.style.setProperty('--paper-edge', '#3c3c3c');
    root.style.setProperty('--ink', '#d4d4d4');
    root.style.setProperty('--ink-soft', '#aaaaaa');
    root.style.setProperty('--ink-mute', '#999999');
    root.style.setProperty('--ink-faint', '#808080');
    root.style.setProperty('--rule', 'rgba(255, 255, 255, 0.12)');
    root.style.setProperty('--rule-soft', 'rgba(255, 255, 255, 0.06)');
    // Dark shadows instead of warm brown
    root.style.setProperty('--glass-shadow',
      '0 30px 60px -20px rgba(0,0,0,0.55),'
      + ' 0 10px 24px -8px rgba(0,0,0,0.4),'
      + ' inset 0 1px 0 rgba(255,255,255,0.06),'
      + ' inset 0 0 0 1px rgba(255,255,255,0.04)');
    // Glass tint: cold dark gray instead of warm paper
    root.style.setProperty('--glass-tint', 'rgba(30, 30, 30, 0.75)');
    root.style.setProperty('--glass-tint-2', 'rgba(40, 40, 40, 0.82)');
    root.style.setProperty('--glass-edge', 'rgba(255, 255, 255, 0.1)');
  } else {
    root.classList.remove('dark');
    const w = paperWarmth.value / 100;
    const paper = `hsl(${42 - w * 4} ${20 + w * 22}% ${91 - w * 2}%)`;
    const paper2 = `hsl(${42 - w * 4} ${22 + w * 22}% ${86 - w * 2}%)`;
    const paperEdge = `hsl(${42 - w * 4} ${20 + w * 18}% ${80 - w * 2}%)`;

    root.style.setProperty('--paper', paper);
    root.style.setProperty('--paper-2', paper2);
    root.style.setProperty('--paper-edge', paperEdge);
    root.style.setProperty('--ink', '#221b12');
    root.style.setProperty('--ink-soft', '#4a3f2f');
    root.style.setProperty('--ink-mute', '#847460');
    root.style.setProperty('--ink-faint', '#b5a98e');
    root.style.setProperty('--rule', 'rgba(34, 27, 18, 0.14)');
    root.style.setProperty('--rule-soft', 'rgba(34, 27, 18, 0.07)');
    // Warm shadows for light mode
    root.style.setProperty('--glass-shadow',
      '0 30px 60px -20px rgba(40,28,12,0.28),'
      + ' 0 10px 24px -8px rgba(40,28,12,0.18),'
      + ' inset 0 1px 0 rgba(255,252,243,0.9),'
      + ' inset 0 0 0 1px rgba(255,252,243,0.5)');
    root.style.setProperty('--glass-tint', 'rgba(248, 243, 230, 0.46)');
    root.style.setProperty('--glass-tint-2', 'rgba(248, 243, 230, 0.62)');
    root.style.setProperty('--glass-edge', 'rgba(255, 252, 243, 0.85)');
  }

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

  // Font family
  if (fontFamily.value === 'cute') {
    root.style.setProperty('--font-serif', 'var(--font-cute)');
  } else {
    root.style.setProperty('--font-serif',
      '"Noto Serif SC", "EB Garamond", "Songti SC", "STSong", "Times New Roman", Georgia, "Microsoft YaHei", serif');
  }
}

watch([paperWarmth, glassBlur, grainAmount, accent, isDarkMode, isCompact, lyricAlign, bgImageUrl, bgDim, fontFamily], () => {
  localStorage.setItem('tweak_warmth', String(paperWarmth.value));
  localStorage.setItem('tweak_blur', String(glassBlur.value));
  localStorage.setItem('tweak_grain', String(grainAmount.value));
  localStorage.setItem('tweak_accent', accent.value);
  localStorage.setItem('tweak_dark', String(isDarkMode.value));
  localStorage.setItem('tweak_compact', String(isCompact.value));
  localStorage.setItem('tweak_lyric_align', lyricAlign.value);
  
  // To avoid exhausting localStorage quota, we could restrict saving huge background images,
  // but for a desktop electron/tauri app it is usually fine up to 5MB.
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
          <label>深色模式 Dark Mode</label>
          <input type="checkbox" v-model="isDarkMode" />
        </div>

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
          <label>纸张暖度 Paper warmth <span v-if="isDarkMode">(禁用)</span><span v-else>{{ paperWarmth }}</span></label>
          <input type="range" v-model.number="paperWarmth" min="0" max="100" :disabled="isDarkMode" />
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
