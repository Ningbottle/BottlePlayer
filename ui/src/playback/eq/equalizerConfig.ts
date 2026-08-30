export const EQ_MIN_GAIN_DB = -6;
export const EQ_MAX_GAIN_DB = 6;

export const EQ_BANDS = [
  { frequency: 31, label: '31', display: '31Hz', tone: '超低频' },
  { frequency: 62, label: '62', display: '62Hz', tone: '低频' },
  { frequency: 125, label: '125', display: '125Hz', tone: '厚度' },
  { frequency: 250, label: '250', display: '250Hz', tone: '温暖' },
  { frequency: 500, label: '500', display: '500Hz', tone: '中低频' },
  { frequency: 1000, label: '1K', display: '1K', tone: '人声' },
  { frequency: 2000, label: '2K', display: '2K', tone: '清晰度' },
  { frequency: 4000, label: '4K', display: '4K', tone: '存在感' },
  { frequency: 8000, label: '8K', display: '8K', tone: '明亮' },
  { frequency: 16000, label: '16K', display: '16K', tone: '空气感' },
] as const;

export const FLAT_EQ_BANDS = EQ_BANDS.map(() => 0);

export const EQ_PRESETS: Record<string, number[]> = {
  Flat: FLAT_EQ_BANDS,
  'Bass Boost': [4, 5, 4, 1, 0, 0, 0, 1, 1, 0],
  Vocal: [-1, -1, 0, 1, 2, 3, 3, 2, 1, 0],
  Rock: [3, 4, 3, 1, -1, -1, 1, 3, 4, 3],
  'Harman Kardon': [2, 3, 2, 0, -1, 0, 1, 2, 2, 1],
  '125Hz Test': [0, 0, 6, 0, 0, 0, 0, 0, 0, 0],
};

export const EQ_PRESET_LABELS: Record<string, string> = {
  Flat: '中性 Flat',
  'Bass Boost': '低频增强 Bass Boost',
  Vocal: '人声 Vocal',
  Rock: '摇滚 Rock',
  'Harman Kardon': '哈曼卡顿 Harman Kardon',
  '125Hz Test': '125Hz 测试 125Hz Test',
};

export function clampEqGain(gain: number): number {
  if (!Number.isFinite(gain)) return 0;
  return Math.max(EQ_MIN_GAIN_DB, Math.min(EQ_MAX_GAIN_DB, gain));
}

export function normalizeEqBands(input: unknown): number[] {
  const values = Array.isArray(input) ? input : [];
  return EQ_BANDS.map((_, index) => clampEqGain(Number(values[index] ?? 0)));
}
