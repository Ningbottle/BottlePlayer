<script setup lang="ts">
import { ref } from 'vue';
import EqualizerPanel from '../components/EqualizerPanel.vue';
import { playerStore, setWebAudioEqBand, eqState } from '../api/playerStore';
import {
  EQ_BANDS,
  EQ_PRESET_LABELS,
  EQ_PRESETS,
  normalizeEqBands,
} from '../api/equalizerConfig';

const eqExpanded = ref(true);

function applyEffect(name: string) {
  const preset = EQ_PRESETS[name];
  if (!preset) return;

  const gains = normalizeEqBands(preset);
  playerStore.activePreset = name;
  playerStore.eqBands = gains;
  localStorage.setItem('player_eq_preset', name);
  localStorage.setItem('player_eq_bands', JSON.stringify(gains));
  gains.forEach((gain, index) => setWebAudioEqBand(index, gain));
}
</script>

<template>
  <div class="eq-view">
    <header class="eq-header">
      <div>
        <div class="kicker">SOUND ROOM · 调音室</div>
        <h1>均衡器 <span>Equalizer</span></h1>
      </div>
      <div class="eq-health" :class="{ unavailable: !eqState.available }">
        {{ eqState.available ? '本地音频处理已接入' : eqState.reason }}
      </div>
    </header>

    <section class="eq-console" aria-label="Equalizer controls">
      <div class="band-guide">
        <span v-for="band in EQ_BANDS" :key="band.frequency">
          {{ band.display }} · {{ band.tone }}
        </span>
      </div>
      <EqualizerPanel
        v-model="eqExpanded"
        variant="standalone"
        :show-presets="false"
      />
    </section>

    <section data-test="sound-effects" class="sound-effects">
      <div class="effects-head">
        <h2>音效预设 <span>Sound Effects</span></h2>
        <p>选择一个整体曲线，再用上方频段做细调。</p>
      </div>
      <div class="effect-grid">
        <button
          v-for="(_, name) in EQ_PRESETS"
          :key="name"
          type="button"
          class="effect-button"
          :class="{ active: playerStore.activePreset === name }"
          @click="applyEffect(name)"
        >
          {{ EQ_PRESET_LABELS[name] ?? name }}
        </button>
      </div>
    </section>
  </div>
</template>

<style scoped>
.eq-view {
  display: flex;
  flex-direction: column;
  gap: 18px;
  padding: 28px 48px 148px;
  min-height: 100%;
}

.eq-header {
  display: flex;
  justify-content: space-between;
  align-items: end;
  gap: 32px;
  padding-bottom: 10px;
}

.kicker {
  color: var(--ink-mute);
  font-family: var(--font-serif);
  font-style: italic;
  font-size: 14px;
  letter-spacing: 0.18em;
  text-transform: uppercase;
}

.eq-header h1 {
  margin: 8px 0 0;
  color: var(--ink);
  font-family: var(--font-serif);
  font-size: clamp(38px, 5vw, 68px);
  font-weight: 700;
  letter-spacing: 0;
  line-height: 0.95;
}

.eq-header h1 span {
  color: var(--ink-soft);
  font-style: italic;
  font-weight: 400;
}

.eq-health {
  max-width: 360px;
  padding: 10px 12px;
  border: 1px solid var(--rule);
  color: var(--accent);
  background: rgba(255, 252, 243, 0.38);
  font-family: var(--font-serif);
  font-size: 13px;
  line-height: 1.5;
  text-align: right;
}

.eq-health.unavailable {
  color: var(--ink-soft);
}

.eq-console {
  padding: 18px 22px 14px;
  border: 1px solid var(--rule);
  background: rgba(255, 252, 243, 0.26);
}

.band-guide {
  display: grid;
  grid-template-columns: repeat(10, minmax(0, 1fr));
  gap: 8px;
  margin-bottom: 12px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--rule);
}

.band-guide span {
  color: var(--ink-soft);
  font-family: var(--font-serif);
  font-size: 12px;
  line-height: 1.25;
  text-align: center;
}

.sound-effects {
  display: grid;
  grid-template-columns: minmax(220px, 0.7fr) 1.3fr;
  gap: 24px;
  align-items: start;
  padding-top: 14px;
  border-top: 1px solid var(--rule);
}

.effects-head h2 {
  margin: 0;
  color: var(--ink);
  font-family: var(--font-serif);
  font-size: 28px;
  letter-spacing: 0;
}

.effects-head h2 span {
  color: var(--ink-mute);
  font-style: italic;
  font-weight: 400;
}

.effects-head p {
  margin: 8px 0 0;
  color: var(--ink-soft);
  font-family: var(--font-serif);
  font-size: 14px;
}

.effect-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 10px;
}

.effect-button {
  min-height: 42px;
  padding: 8px 12px;
  border: 1px solid var(--rule);
  border-radius: 6px;
  background: rgba(255, 252, 243, 0.42);
  color: var(--ink-soft);
  font-family: var(--font-serif);
  font-size: 14px;
  text-align: left;
  cursor: pointer;
  transition: background 0.15s, border-color 0.15s, color 0.15s;
}

.effect-button:hover,
.effect-button.active {
  border-color: var(--accent);
  background: var(--accent);
  color: var(--paper);
}

@media (max-width: 1040px) {
  .eq-view {
    padding: 24px 28px 148px;
  }

  .band-guide {
    grid-template-columns: repeat(5, minmax(0, 1fr));
  }

  .sound-effects {
    grid-template-columns: 1fr;
  }
}
</style>
