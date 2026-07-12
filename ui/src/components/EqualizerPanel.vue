<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import { playerStore, setWebAudioEqBand, setWebAudioEqEnabled, eqState, retryEq } from '../api/playerStore';
import {
  EQ_BANDS,
  EQ_MAX_GAIN_DB,
  EQ_MIN_GAIN_DB,
  EQ_PRESET_LABELS,
  EQ_PRESETS,
  FLAT_EQ_BANDS,
  normalizeEqBands,
} from '../api/equalizerConfig';

const props = withDefaults(defineProps<{
  modelValue?: boolean;
  variant?: 'collapsible' | 'standalone';
  showPresets?: boolean;
}>(), {
  modelValue: false,
  variant: 'collapsible',
  showPresets: true,
});
const emit = defineEmits<{ (e: 'update:modelValue', v: boolean): void }>();

const expanded = computed({
  get: () => props.variant === 'standalone' || props.modelValue,
  set: (v: boolean) => {
    if (props.variant !== 'standalone') emit('update:modelValue', v);
  },
});

const bandGains = ref<number[]>(normalizeEqBands(playerStore.eqBands));
watch(() => playerStore.eqBands, (v) => { bandGains.value = normalizeEqBands(v); });

function onSliderInput() {
  bandGains.value = normalizeEqBands(bandGains.value);
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
  if (EQ_PRESETS[name]) {
    bandGains.value = [...EQ_PRESETS[name]];
    playerStore.activePreset = name;
    localStorage.setItem('player_eq_preset', name);
    onSliderInput();
  }
}

function resetEq() {
  bandGains.value = [...FLAT_EQ_BANDS];
  playerStore.activePreset = 'Flat';
  localStorage.setItem('player_eq_preset', 'Flat');
  onSliderInput();
}

function formatGain(g: number) {
  return (g > 0 ? '+' : '') + g.toFixed(1) + 'dB';
}
</script>

<template>
  <div class="eq-panel" :class="{ expanded, standalone: variant === 'standalone' }">
    <button v-if="variant !== 'standalone'" class="eq-toggle" @click="expanded = !expanded">
      <span class="eq-status">EQ {{ playerStore.eqEnabled ? 'ON' : 'OFF' }}</span>
      <span class="chevron">{{ expanded ? '▾' : '▸' }}</span>
    </button>
    <div v-if="expanded" class="eq-controls">
      <p v-if="!eqState.available" class="eq-unavailable">
        {{ eqState.reason }}
        <button
          type="button"
          data-test="eq-retry"
          class="eq-retry"
          :disabled="eqState.retryDisabled"
          @click="retryEq"
        >
          重试 EQ
        </button>
        <span v-if="eqState.retryDisabled" class="eq-retry-hint">请重启应用</span>
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
          v-if="showPresets"
          class="eq-preset"
          :value="playerStore.activePreset"
          :disabled="!playerStore.eqEnabled"
          @change="applyPreset(($event.target as HTMLSelectElement).value)"
        >
          <option v-for="(_, name) in EQ_PRESETS" :key="name" :value="name">
            {{ EQ_PRESET_LABELS[name] ?? name }}
          </option>
        </select>
        <button type="button" data-test="eq-reset" class="eq-reset" @click="resetEq">
          恢复默认
        </button>
      </div>
      <div class="eq-bands">
        <div v-for="(gain, i) in bandGains" :key="i" class="eq-band">
          <input
            type="range"
            :min="EQ_MIN_GAIN_DB"
            :max="EQ_MAX_GAIN_DB"
            step="0.5"
            v-model.number="bandGains[i]"
            :disabled="!playerStore.eqEnabled || !eqState.available"
            orient="vertical"
            class="band-slider"
            @input="onSliderInput"
          />
          <span class="band-freq">{{ EQ_BANDS[i].display }}</span>
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

.eq-panel.standalone {
  border: 0;
  border-radius: 0;
  background: transparent;
  overflow: visible;
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

.eq-panel.standalone .eq-controls {
  padding: 0;
  gap: 18px;
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
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 8px;
}

.eq-retry {
  border: 1px solid var(--rule);
  border-radius: 4px;
  background: var(--paper);
  color: var(--accent);
  font-family: var(--font-serif);
  font-size: 11px;
  font-style: normal;
  cursor: pointer;
  padding: 2px 8px;
}

.eq-retry:disabled {
  opacity: 0.4;
  cursor: not-allowed;
}

.eq-retry-hint {
  font-style: normal;
  color: var(--ink-mute);
}

.eq-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  flex-wrap: wrap;
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

.eq-reset {
  border: 0;
  background: transparent;
  color: var(--accent);
  font-family: var(--font-serif);
  font-size: 12px;
  cursor: pointer;
  padding: 4px 0;
}

.eq-reset:hover {
  color: var(--ink);
}

.eq-bands {
  display: flex;
  justify-content: space-between;
  align-items: flex-end;
  gap: clamp(4px, 1vw, 12px);
  padding: 8px 0 4px;
  min-height: 150px;
  min-width: 0;
  width: 100%;
  box-sizing: border-box;
}

.eq-panel.standalone .eq-bands {
  min-height: 200px;
  gap: clamp(4px, 1vw, 12px);
  padding: 8px 0 4px;
}

.eq-panel.standalone .eq-band {
  flex: 1 1 0;
  min-width: 36px;
  max-width: 80px;
}

.eq-panel.standalone .band-slider {
  width: 26px;
  height: 180px;
  flex: none;
}

.eq-panel.standalone .band-freq {
  font-size: 12px;
  color: var(--ink);
}

.eq-panel.standalone .band-gain {
  font-size: 10px;
  color: var(--ink-soft);
  min-width: 0;
}

.eq-band {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  min-width: 0;
}

@media (max-width: 1200px) {
  .eq-panel.standalone .eq-bands {
    min-height: 170px;
  }

  .eq-panel.standalone .band-slider {
    width: 22px;
    height: 150px;
  }

  .eq-panel.standalone .band-freq {
    font-size: 11px;
  }
}

@media (max-width: 900px) {
  .eq-panel.standalone .band-slider {
    height: 120px;
  }
}

.band-slider {
  writing-mode: vertical-lr;
  direction: rtl;
  width: 24px;
  height: 124px;
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
