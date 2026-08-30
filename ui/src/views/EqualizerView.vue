<script setup lang="ts">
import { ref } from 'vue';
import EqualizerPanel from '../components/EqualizerPanel.vue';
import { playerStore, setWebAudioEqBand, eqState } from '../playback/playerStore';
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
    <SkinPageHeader title="均衡器" kicker="SOUND ROOM · 调音室" subtitle="Equalizer">
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
          :active="playerStore.activePreset === name"
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
  gap: 16px;
  padding: 0 0 28px;
  min-height: 100%;
  box-sizing: border-box;
  width: 100%;
  max-width: 100%;
  min-width: 0;
}

.eq-health {
  max-width: min(360px, 42vw);
  padding: 10px 12px;
  border: 1px solid var(--border-subtle);
  color: var(--accent);
  background: var(--surface-1);
  font-size: 12px;
  line-height: 1.45;
  text-align: right;
  border-radius: 8px;
}

.eq-health.unavailable {
  color: var(--text-secondary);
}

.eq-console {
  padding: 14px clamp(12px, 1.5vw, 20px) 12px;
  border: 1px solid var(--border-subtle);
  background: var(--surface-1);
  border-radius: 10px;
  min-width: 0;
  overflow: visible;
  box-sizing: border-box;
}

.band-guide {
  display: grid;
  grid-template-columns: repeat(10, minmax(0, 1fr));
  gap: 4px 6px;
  margin-bottom: 10px;
  padding-bottom: 10px;
  border-bottom: 1px solid var(--border-subtle);
  min-width: 0;
}

.band-guide span {
  color: var(--text-secondary);
  font-size: clamp(10px, 0.85vw, 12px);
  line-height: 1.2;
  text-align: center;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.sound-effects {
  display: grid;
  grid-template-columns: minmax(180px, 0.65fr) minmax(0, 1.35fr);
  gap: 20px;
  align-items: start;
  padding-top: 12px;
  margin-bottom: 16px;
  border-top: 1px solid var(--border-subtle);
  min-width: 0;
}

.effects-head h2 {
  margin: 0;
  color: var(--text-primary);
  font-size: clamp(22px, 2.4vw, 28px);
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
  min-width: 0;
}

@media (max-width: 1200px) {
  .band-guide {
    grid-template-columns: repeat(5, minmax(0, 1fr));
    row-gap: 8px;
  }

  .band-guide span {
    white-space: normal;
  }
}

@media (max-width: 1040px) {
  .eq-view {
    padding: 0 0 24px;
  }

  .eq-health {
    max-width: 100%;
    text-align: left;
  }

  .sound-effects {
    grid-template-columns: 1fr;
  }

  .effect-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}

@media (max-width: 720px) {
  .band-guide {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .effect-grid {
    grid-template-columns: 1fr;
  }
}
</style>
