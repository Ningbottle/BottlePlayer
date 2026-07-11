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
import SkinPageHeader from '../components/primitives/SkinPageHeader.vue';
import SkinButton from '../components/primitives/SkinButton.vue';

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
    <SkinPageHeader title="均衡器" kicker="SOUND ROOM · 调音室">
      <template #actions>
        <div class="eq-health" :class="{ unavailable: !eqState.available }">
          {{ eqState.available ? '本地音频处理已接入' : eqState.reason }}
        </div>
      </template>
    </SkinPageHeader>

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
        <SkinButton
          v-for="(_, name) in EQ_PRESETS"
          :key="name"
          variant="secondary"
          size="md"
          :class="{ 'effect-active': playerStore.activePreset === name }"
          @click="applyEffect(name)"
        >
          {{ EQ_PRESET_LABELS[name] ?? name }}
        </SkinButton>
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

.eq-health {
  max-width: 360px;
  padding: 10px 12px;
  border: 1px solid var(--border-subtle);
  color: var(--accent);
  background: var(--surface-1);
  font-size: 13px;
  line-height: 1.5;
  text-align: right;
}

.eq-health.unavailable {
  color: var(--text-secondary);
}

.eq-console {
  padding: 18px 22px 14px;
  border: 1px solid var(--border-subtle);
  background: var(--surface-1);
  border-radius: 10px;
}

.band-guide {
  display: grid;
  grid-template-columns: repeat(10, minmax(0, 1fr));
  gap: 8px;
  margin-bottom: 12px;
  padding-bottom: 12px;
  border-bottom: 1px solid var(--border-subtle);
}

.band-guide span {
  color: var(--text-secondary);
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
  border-top: 1px solid var(--border-subtle);
}

.effects-head h2 {
  margin: 0;
  color: var(--text-primary);
  font-size: 28px;
  letter-spacing: 0;
}

.effects-head h2 span {
  color: var(--text-muted);
  font-style: italic;
  font-weight: 400;
}

.effects-head p {
  margin: 8px 0 0;
  color: var(--text-secondary);
  font-size: 14px;
}

.effect-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 10px;
}

.effect-active {
  background: var(--accent) !important;
  color: var(--app-bg) !important;
  border-color: var(--accent) !important;
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
