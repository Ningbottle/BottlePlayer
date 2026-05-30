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
    </div>
  </aside>
</template>

<style scoped>
/* Scoped tweaks styling */
</style>
