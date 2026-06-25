<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import { playerStore, setWebAudioEqBand, setWebAudioEqEnabled, eqState } from '../api/playerStore';

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

function onSliderInput() {
  playerStore.eqBands = [...bandGains.value];
  localStorage.setItem('player_eq_bands', JSON.stringify(bandGains.value));
  bandGains.value.forEach((g, i) => setWebAudioEqBand(i, g));
}

function toggleEnabled() {
  playerStore.eqEnabled = !playerStore.eqEnabled;
  localStorage.setItem('player_eq_enabled', String(playerStore.eqEnabled));
  setWebAudioEqEnabled(playerStore.eqEnabled);
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
  <div class="eq-panel" :class="{ expanded }">
    <button class="eq-toggle" @click="expanded = !expanded">
      <span class="eq-status">EQ {{ playerStore.eqEnabled ? 'ON' : 'OFF' }}</span>
      <span class="chevron">{{ expanded ? '▾' : '▸' }}</span>
    </button>
    <div v-if="expanded" class="eq-controls">
      <p v-if="!eqState.available" class="eq-unavailable">
        当前音源不支持 EQ（酷狗 CDN 未发送 CORS 头，启用 EQ 会导致静音）。滑块暂不生效。
      </p>
      <div class="eq-row">
        <label class="eq-enable">
          <input
            type="checkbox"
            :checked="playerStore.eqEnabled"
            @change="toggleEnabled"
          />
          Enable Equalizer
        </label>
        <select
          class="eq-preset"
          :value="playerStore.activePreset"
          :disabled="!playerStore.eqEnabled"
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
            :disabled="!playerStore.eqEnabled || !eqState.available"
            orient="vertical"
            class="band-slider"
            @input="onSliderInput"
          />
          <span class="band-freq">{{ BAND_FREQS[i] }}</span>
          <span class="band-gain">{{ formatGain(gain) }}</span>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.eq-panel {
  border: 1px solid var(--rule);
  border-radius: 8px;
  background: var(--paper-2);
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
  color: var(--ink);
  cursor: pointer;
  font-family: var(--font-serif);
  font-size: 13px;
  font-weight: 600;
  letter-spacing: 0.04em;
  transition: background 0.15s;
}

.eq-toggle:hover {
  background: var(--rule-soft);
}

.eq-status {
  letter-spacing: 0.08em;
}

.chevron {
  font-size: 14px;
  color: var(--ink-mute);
}

.eq-controls {
  padding: 4px 14px 14px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.eq-unavailable {
  margin: 4px 0 0;
  padding: 8px 10px;
  border-left: 3px solid var(--accent);
  background: var(--rule-soft);
  color: var(--ink-soft);
  font-family: var(--font-serif);
  font-style: italic;
  font-size: 11px;
  line-height: 1.5;
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
  font-family: var(--font-serif);
  font-size: 12px;
  color: var(--ink-soft);
  cursor: pointer;
}

.eq-enable input[type="checkbox"] {
  accent-color: var(--accent);
  width: 15px;
  height: 15px;
  cursor: pointer;
}

.eq-preset {
  padding: 4px 8px;
  border: 1px solid var(--rule);
  border-radius: 4px;
  background: var(--paper);
  color: var(--ink-soft);
  font-family: var(--font-serif);
  font-size: 12px;
  cursor: pointer;
  transition: border-color 0.15s, color 0.15s;
}

.eq-preset:hover:not(:disabled) {
  border-color: var(--ink-mute);
  color: var(--ink);
}

.eq-preset:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.eq-bands {
  display: flex;
  justify-content: space-around;
  gap: 8px;
  padding: 8px 0 4px;
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
  accent-color: var(--accent);
  cursor: pointer;
}

.band-slider:disabled {
  opacity: 0.3;
  cursor: not-allowed;
}

.band-freq {
  font-family: var(--font-serif);
  font-style: italic;
  font-size: 10px;
  color: var(--ink-mute);
  white-space: nowrap;
}

.band-gain {
  font-family: var(--font-sans);
  font-size: 10px;
  color: var(--ink);
  font-variant-numeric: tabular-nums;
  min-width: 42px;
  text-align: center;
}
</style>
