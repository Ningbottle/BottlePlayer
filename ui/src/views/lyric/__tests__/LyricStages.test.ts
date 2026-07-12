import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { nextTick } from 'vue';

const gsapSetMock = vi.hoisted(() => vi.fn());
const gsapToMock = vi.hoisted(() => vi.fn((_: any, opts: any) => {
  if (opts?.onComplete) opts.onComplete();
}));
const gsapFromToMock = vi.hoisted(() => vi.fn((_el: any, _from: any, to: any) => {
  if (to?.onComplete) to.onComplete();
}));
const gsapKillTweensOfMock = vi.hoisted(() => vi.fn());

vi.mock('gsap', () => ({
  gsap: {
    set: gsapSetMock,
    to: gsapToMock,
    fromTo: gsapFromToMock,
    killTweensOf: gsapKillTweensOfMock,
  },
}));

const isReducedMotionMock = vi.hoisted(() => vi.fn(() => false));

vi.mock('../../../api/motion', () => ({
  isReducedMotion: isReducedMotionMock,
  animateElement: vi.fn(),
  animateStagger: vi.fn(() => ({ kill: () => {} })),
  startAmbientMotion: vi.fn(() => ({ kill: () => {} })),
}));

import AuroraLyricStage from '../AuroraLyricStage.vue';
import NewsprintLyricStage from '../NewsprintLyricStage.vue';
import type { LyricStageModel } from '../useLyricStage';
import {
  useLyricFocusStore,
  __resetLyricFocusForTest,
} from '../../../api/lyricFocusStore';

const SAMPLE_LINES = [
  { time: 0, text: 'First line' },
  { time: 5, text: 'Second line' },
  { time: 10, text: 'Third line' },
];

function createModel(overrides: Partial<LyricStageModel> = {}): LyricStageModel {
  return {
    loading: false,
    parsedLyrics: SAMPLE_LINES,
    activeIndex: 1,
    currentTrack: { FileHash: 'h1', SongName: 'Test Song', SingerName: 'Test Artist', Duration: 100, Image: 'http://img/' } as any,
    coverUrl: 'http://img/',
    autoFollowing: true,
    fullscreen: false,
    isPlaying: true,
    currentTime: 5,
    ...overrides,
  };
}

/** Stage chrome: root or cover single-element fromTo (no stagger). */
function stageRootFromToCalls() {
  return gsapFromToMock.mock.calls.filter((call: any[]) => {
    const el = call[0];
    const to = call[call.length - 1];
    if (to?.stagger != null) return false;
    return el?.getAttribute?.('data-test') === 'aurora-lyric-stage';
  });
}

function stageCoverFromToCalls() {
  return gsapFromToMock.mock.calls.filter((call: any[]) => {
    const el = call[0];
    const to = call[call.length - 1];
    if (to?.stagger != null) return false;
    return (
      el?.getAttribute?.('data-test') === 'lyric-cover' ||
      el?.classList?.contains?.('aurora-cover')
    );
  });
}

/** Line enter: fromTo with stagger (or multi-element target). */
function lineStaggerFromToCalls() {
  return gsapFromToMock.mock.calls.filter((call: any[]) => {
    const el = call[0];
    const to = call[call.length - 1];
    if (to?.stagger != null) return true;
    return Array.isArray(el) || (typeof NodeList !== 'undefined' && el instanceof NodeList);
  });
}

function extractEases(): string[] {
  const calls = gsapToMock.mock.calls
    .map((c: any[]) => {
      const opts = c[c.length - 1];
      return opts?.ease;
    });
  const fromToCalls = gsapFromToMock.mock.calls
    .map((c: any[]) => {
      const opts = c[c.length - 1];
      return opts?.ease;
    });
  return calls.concat(fromToCalls).filter(Boolean);
}

function clearGsapMocks() {
  gsapToMock.mockClear();
  gsapFromToMock.mockClear();
  gsapSetMock.mockClear();
  gsapKillTweensOfMock.mockClear();
  isReducedMotionMock.mockReset();
  isReducedMotionMock.mockReturnValue(false);
}

describe('Lyric stage structure differences', () => {
  beforeEach(() => {
    clearGsapMocks();
  });

  it('Aurora and Newsprint have different root element classes', () => {
    const model = createModel();
    const aurora = mount(AuroraLyricStage, { props: { model } });
    const newsprint = mount(NewsprintLyricStage, { props: { model } });

    expect(aurora.find('.aurora-lyric-stage').exists()).toBe(true);
    expect(newsprint.find('.np-lyric-stage').exists()).toBe(true);
    expect(aurora.find('.np-lyric-stage').exists()).toBe(false);
    expect(newsprint.find('.aurora-lyric-stage').exists()).toBe(false);
  });

  it('Newsprint has line numbers, Aurora does not', () => {
    const model = createModel();
    const aurora = mount(AuroraLyricStage, { props: { model } });
    const newsprint = mount(NewsprintLyricStage, { props: { model } });

    expect(newsprint.find('.np-line-num').exists()).toBe(true);
    expect(aurora.find('.np-line-num').exists()).toBe(false);
  });

  it('Aurora and Newsprint have different cover element classes', () => {
    const model = createModel();
    const aurora = mount(AuroraLyricStage, { props: { model } });
    const newsprint = mount(NewsprintLyricStage, { props: { model } });

    expect(aurora.find('.aurora-cover').exists()).toBe(true);
    expect(newsprint.find('.np-cover').exists()).toBe(true);
    expect(aurora.find('.np-cover').exists()).toBe(false);
    expect(newsprint.find('.aurora-cover').exists()).toBe(false);
  });
});

describe('Lyric stage shared data', () => {
  it('both show the same current line as active', () => {
    const model = createModel({ activeIndex: 1 });
    const aurora = mount(AuroraLyricStage, { props: { model } });
    const newsprint = mount(NewsprintLyricStage, { props: { model } });

    const auroraLines = aurora.findAll('[data-test^="lyric-line-"]');
    const newsprintLines = newsprint.findAll('[data-test^="lyric-line-"]');

    expect(auroraLines[1].classes()).toContain('active');
    expect(newsprintLines[1].classes()).toContain('active');
    expect(auroraLines[0].classes()).not.toContain('active');
    expect(newsprintLines[0].classes()).not.toContain('active');
  });

  it('both render the same lyrics', () => {
    const model = createModel();
    const aurora = mount(AuroraLyricStage, { props: { model } });
    const newsprint = mount(NewsprintLyricStage, { props: { model } });

    expect(aurora.text()).toContain('First line');
    expect(aurora.text()).toContain('Second line');
    expect(aurora.text()).toContain('Third line');
    expect(newsprint.text()).toContain('First line');
    expect(newsprint.text()).toContain('Second line');
    expect(newsprint.text()).toContain('Third line');
  });

  it('both show the same song info', () => {
    const model = createModel();
    const aurora = mount(AuroraLyricStage, { props: { model } });
    const newsprint = mount(NewsprintLyricStage, { props: { model } });

    expect(aurora.text()).toContain('Test Song');
    expect(aurora.text()).toContain('Test Artist');
    expect(newsprint.text()).toContain('Test Song');
    expect(newsprint.text()).toContain('Test Artist');
  });

  it('both show the same auto-follow state', () => {
    const model = createModel({ autoFollowing: false });
    const aurora = mount(AuroraLyricStage, { props: { model } });
    const newsprint = mount(NewsprintLyricStage, { props: { model } });

    const auroraScroll = aurora.find('.lyric-scroll');
    const newsprintScroll = newsprint.find('.lyric-scroll');

    expect(auroraScroll.classes()).toContain('paused');
    expect(newsprintScroll.classes()).toContain('paused');
  });
});

describe('Lyric stage motion profiles', () => {
  beforeEach(() => {
    clearGsapMocks();
  });

  it('Aurora uses cinematic power3.out rise-in for entrance', () => {
    const model = createModel();
    mount(AuroraLyricStage, { props: { model } });

    const eases = extractEases();
    // Film-like rise from below uses power3.out (not elastic/bounce).
    expect(eases.some((e: string) => e.includes('power3') || e.includes('power2'))).toBe(true);
    expect(eases.some((e: string) => e.includes('elastic'))).toBe(false);
  });

  it('Newsprint uses power3.out for entrance', () => {
    const model = createModel();
    mount(NewsprintLyricStage, { props: { model } });

    const eases = extractEases();
    expect(eases.some((e: string) => e.includes('power3'))).toBe(true);
  });

  it('Newsprint does not use elastic ease', () => {
    const model = createModel();
    mount(NewsprintLyricStage, { props: { model } });

    const eases = extractEases();
    expect(eases.every((e: string) => !e.includes('elastic'))).toBe(true);
  });
});

describe('Aurora lyric enter split (stage vs lines)', () => {
  beforeEach(() => {
    clearGsapMocks();
  });

  it('when lyrics go [] → N, only line stagger fires (no second stage root fromTo)', async () => {
    const wrapper = mount(AuroraLyricStage, {
      props: { model: createModel({ parsedLyrics: [] }) },
    });

    const rootCallsAfterMount = stageRootFromToCalls().length;
    const coverCallsAfterMount = stageCoverFromToCalls().length;
    expect(rootCallsAfterMount).toBeGreaterThanOrEqual(1);
    expect(coverCallsAfterMount).toBeGreaterThanOrEqual(1);
    expect(lineStaggerFromToCalls().length).toBe(0);

    clearGsapMocks();

    await wrapper.setProps({
      model: createModel({ parsedLyrics: SAMPLE_LINES }),
    });
    await nextTick();
    await flushPromises();

    expect(lineStaggerFromToCalls().length).toBe(1);
    expect(stageRootFromToCalls().length).toBe(0);
    expect(stageCoverFromToCalls().length).toBe(0);
  });

  it('when FileHash changes, stage re-runs and line enter runs again for the new hash', async () => {
    const wrapper = mount(AuroraLyricStage, {
      props: { model: createModel({ currentTrack: { FileHash: 'h1', SongName: 'A', SingerName: 'B', Duration: 100, Image: '' } as any }) },
    });
    await nextTick();
    await flushPromises();

    expect(stageRootFromToCalls().length).toBeGreaterThanOrEqual(1);
    expect(lineStaggerFromToCalls().length).toBe(1);

    clearGsapMocks();

    await wrapper.setProps({
      model: createModel({
        currentTrack: { FileHash: 'h2', SongName: 'C', SingerName: 'D', Duration: 100, Image: '' } as any,
        parsedLyrics: SAMPLE_LINES,
      }),
    });
    await nextTick();
    await flushPromises();

    expect(stageRootFromToCalls().length).toBeGreaterThanOrEqual(1);
    expect(stageCoverFromToCalls().length).toBeGreaterThanOrEqual(1);
    expect(lineStaggerFromToCalls().length).toBe(1);
  });

  it('line enter is not run twice for the same FileHash', async () => {
    const wrapper = mount(AuroraLyricStage, {
      props: { model: createModel({ parsedLyrics: SAMPLE_LINES }) },
    });
    await nextTick();
    await flushPromises();

    expect(lineStaggerFromToCalls().length).toBe(1);
    clearGsapMocks();

    // Lyrics clear then reload for the same hash — must not restage lines.
    await wrapper.setProps({
      model: createModel({ parsedLyrics: [] }),
    });
    await nextTick();
    await wrapper.setProps({
      model: createModel({ parsedLyrics: SAMPLE_LINES }),
    });
    await nextTick();
    await flushPromises();

    expect(lineStaggerFromToCalls().length).toBe(0);
    expect(stageRootFromToCalls().length).toBe(0);
  });

  it('reduced motion sets final styles only (no fromTo theater)', async () => {
    isReducedMotionMock.mockReturnValue(true);
    mount(AuroraLyricStage, { props: { model: createModel() } });
    await nextTick();
    await flushPromises();

    expect(gsapFromToMock).not.toHaveBeenCalled();
    expect(gsapSetMock).toHaveBeenCalled();
  });
});

describe('Aurora lyric focus modes', () => {
  beforeEach(() => {
    localStorage.clear();
    __resetLyricFocusForTest();
    clearGsapMocks();
  });


  it('roots data-lyric-focus from the focus store', () => {
    const focus = useLyricFocusStore();
    focus.init();
    focus.setMode('stage');

    const wrapper = mount(AuroraLyricStage, { props: { model: createModel() } });
    const root = wrapper.get('[data-test="aurora-lyric-stage"]');
    expect(root.attributes('data-lyric-focus')).toBe('stage');

    focus.setMode('readable');
    return wrapper.vm.$nextTick().then(() => {
      expect(wrapper.get('[data-test="aurora-lyric-stage"]').attributes('data-lyric-focus')).toBe(
        'readable',
      );
    });
  });

  it('page toggle flips mode when not fullscreen', async () => {
    const focus = useLyricFocusStore();
    focus.init();
    focus.setMode('readable');

    const wrapper = mount(AuroraLyricStage, {
      props: { model: createModel({ fullscreen: false }) },
    });
    const toggle = wrapper.get('[data-test="lyric-focus-toggle"]');
    expect(toggle.attributes('aria-pressed')).toBe('false');
    expect(toggle.attributes('aria-label')).toBe('切换为舞台渐隐');

    await toggle.trigger('click');
    expect(focus.mode.value).toBe('stage');
    expect(toggle.attributes('aria-pressed')).toBe('true');
    expect(toggle.attributes('aria-label')).toBe('切换为清晰可读');
    expect(wrapper.get('[data-test="aurora-lyric-stage"]').attributes('data-lyric-focus')).toBe(
      'stage',
    );
  });

  it('fullscreen has no lyric-focus-toggle in the DOM', () => {
    const focus = useLyricFocusStore();
    focus.init();

    const wrapper = mount(AuroraLyricStage, {
      props: { model: createModel({ fullscreen: true }) },
    });
    expect(wrapper.find('[data-test="lyric-focus-toggle"]').exists()).toBe(false);
  });

  it('Newsprint stage does not expose dual-mode lyric focus toggle', () => {
    const focus = useLyricFocusStore();
    focus.init();

    const wrapper = mount(NewsprintLyricStage, {
      props: { model: createModel({ fullscreen: false }) },
    });
    expect(wrapper.find('[data-test="lyric-focus-toggle"]').exists()).toBe(false);
  });
});
