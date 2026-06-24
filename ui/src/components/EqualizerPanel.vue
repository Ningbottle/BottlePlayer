<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import { invoke } from '@tauri-apps/api/core';
import { playerStore } from '../api/playerStore';

const props = defineProps<{ modelValue: boolean }>();
const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void }>();

const expanded = computed({
  get: () => props.modelValue,
  set: (v: boolean) => emit('update:modelValue', v),
});

const PRESETS: Record<string, number[]> = {
  'Flat':       [0, 0, 0, 0, 0],
  'Bass Boost': [6, 4, 0, 0, 0],
  'Vocal':      [0, 2, 4, 2, 0],
  'Rock':       [4, 2, -2, 2, 4],
};

const BAND_FREQS = ['60Hz', '230Hz', '910Hz', '3.6kHz', '14kHz'];

const bandGains = ref<number[]>([...playerStore.eqBands]);
watch(() => playerStore.eqBands, (v) => { bandGains.value = [...v]; });

const disabled = computed(() => playerStore.backend !== 'native');

async function onSliderInput() {
  playerStore.eqBands = [...bandGains.value];
  localStorage.setItem('player_eq_bands', JSON.stringify(bandGains.value));
  if (playerStore.backend === 'native') {
    try {
      await invoke('playback_set_eq_bands', { gains: bandGains.value });
    } catch (e) { console.warn('set_eq_bands failed', e); }
  }
}

async function toggleEnabled() {
  playerStore.eqEnabled = !playerStore.eqEnabled;
  localStorage.setItem('player_eq_enabled', String(playerStore.eqEnabled));
  if (playerStore.backend === 'native') {
    try {
      await invoke('playback_set_eq_enabled', { enabled: playerStore.eqEnabled });
    } catch (e) { console.warn('set_eq_enabled failed', e); }
  }
}

function applyPreset(name: string) {
  if (PRESETS[name]) {
    bandGains.value = [...PRESETS[name]];
    playerStore.activePreset = name;
    localStorage.setItem('player_eq_preset', name);
    onSliderInput();
  }
}

function formatGain(g: number) {
  return (g > 0 ? '+' : '') + g.toFixed(1) + 'dB';
}
</script>

<template>
  <div class="eq-panel" :class="{ expanded, disabled }">
    <button class="eq-toggle" @click="expanded = !expanded">
      <span class="eq-status">EQ {{ playerStore.eqEnabled ? 'ON' : 'OFF' }}</span>
      <span class="chevron">{{ expanded ? '▾' : '▸' }}</span>
    </button>
    <div v-if="expanded" class="eq-controls">
      <div class="eq-row">
        <label class="eq-enable">
          <input
            type="checkbox"
            :checked="playerStore.eqEnabled"
            :disabled="disabled"
            @change="toggleEnabled"
          />
          Enable Equalizer
        </label>
        <select
          class="eq-preset"
          :value="playerStore.activePreset"
          :disabled="disabled || !playerStore.eqEnabled"
          @change="applyPreset(($event.target as HTMLSelectElement).value)"
        >
          <option v-for="(_, name) in PRESETS" :key="name" :value="name">{{ name }}</option>
        </select>
      </div>
      <div class="eq-bands">
        <div v-for="(gain, i) in bandGains" :key="i" class="eq-band">
          <input
            type="range"
            min="-12"
            max="12"
            step="0.5"
            v-model.number="bandGains[i]"
            :disabled="disabled || !playerStore.eqEnabled"
            orient="vertical"
            class="band-slider"
            @input="onSliderInput"
          />
          <span class="band-freq">{{ BAND_FREQS[i] }}</span>
          <span class="band-gain">{{ formatGain(gain) }}</span>
        </div>
      </div>
      <p v-if="disabled" class="eq-hint">
        Native backend not available — EQ disabled. Using HTML5 fallback.
      </p>
    </div>
  </div>
</template>

<style scoped>
.eq-panel {
  border: 1px solid var(--border-color, #2a2a2a);
  border-radius: 8px;
  background: var(--panel-bg, #1a1a1a);
  overflow: hidden;
}

.eq-toggle {
  display: flex;
  align-items: center;
  justify-content: space-between;
  width: 100%;
  padding: 10px 14px;
  background: none;
  border: none;
  color: var(--text-primary, #e0e0e0);
  cursor: pointer;
  font-size: 13px;
  font-weight: 500;
}

.eq-toggle:hover {
  background: var(--hover-bg, rgba(255, 255, 255, 0.05));
}

.eq-status {
  letter-spacing: 0.5px;
}

.chevron {
  font-size: 14px;
  opacity: 0.6;
}

.eq-controls {
  padding: 0 14px 14px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.eq-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}

.eq-enable {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: var(--text-secondary, #999);
  cursor: pointer;
}

.eq-enable input[type="checkbox"] {
  accent-color: var(--accent-color, #1db954);
}

.eq-preset {
  padding: 4px 8px;
  border: 1px solid var(--border-color, #2a2a2a);
  border-radius: 4px;
  background: var(--input-bg, #222);
  color: var(--text-primary, #e0e0e0);
  font-size: 12px;
  cursor: pointer;
}

.eq-preset:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.eq-bands {
  display: flex;
  justify-content: space-around;
  gap: 8px;
  padding: 8px 0;
}

.eq-band {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
}

.band-slider {
  writing-mode: vertical-lr;
  direction: rtl;
  width: 24px;
  height: 100px;
  accent-color: var(--accent-color, #1db954);
  cursor: pointer;
}

.band-slider:disabled {
  opacity: 0.3;
  cursor: not-allowed;
}

.band-freq {
  font-size: 10px;
  color: var(--text-secondary, #777);
  white-space: nowrap;
}

.band-gain {
  font-size: 10px;
  color: var(--text-primary, #e0e0e0);
  font-variant-numeric: tabular-nums;
  min-width: 42px;
  text-align: center;
}

.eq-hint {
  font-size: 11px;
  color: var(--text-secondary, #777);
  margin: 0;
  text-align: center;
  padding: 4px 0;
}

.disabled {
  opacity: 0.7;
}
</style>
