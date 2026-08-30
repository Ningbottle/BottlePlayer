import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { defineComponent, effectScope, h, nextTick, reactive } from 'vue';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';

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
  startVinylSpin: vi.fn(() => ({ kill: vi.fn(), setPlaying: vi.fn(), burst: vi.fn() })),
}));

const playTrackMock = vi.hoisted(() => vi.fn());
const lyricApiGetMock = vi.hoisted(() => vi.fn());
const mockPlayerStoreState = vi.hoisted(() => ({
  queue: [
    { FileHash: 'q1', SongName: 'Queue One', SingerName: 'A', Duration: 100, Image: '' },
    { FileHash: 'q2', SongName: 'Queue Two', SingerName: 'B', Duration: 120, Image: '' },
  ],
  currentTrack: null as any,
  currentTime: 0,
  duration: 0,
  isPlaying: false,
}));
const queueTracks = mockPlayerStoreState.queue;
const mockPlayerStoreHolder = vi.hoisted(() => ({ value: null as any }));
vi.mock('../../../api/playerStore', () => ({
  playerStore: (() => {
    mockPlayerStoreHolder.value = reactive(mockPlayerStoreState);
    return mockPlayerStoreHolder.value;
  })(),
  playTrack: playTrackMock,
  seek: vi.fn(),
}));
vi.mock('../../../platform/tauri/nativeClient', () => ({
  apiGet: (...args: unknown[]) => lyricApiGetMock(...args),
}));

import AuroraLyricStage from '../AuroraLyricStage.vue';
import AuroraPlaylistShelf from '../AuroraPlaylistShelf.vue';
import NewsprintLyricStage from '../NewsprintLyricStage.vue';
import type { LyricStageCommands, LyricStageModel } from '../useLyricStage';
import { useLyricStage } from '../useLyricStage';
import {
  useLyricFocusStore,
  __resetLyricFocusForTest,
} from '../../../api/lyricFocusStore';

const newsprintStageSource = readFileSync(resolve(__dirname, '../NewsprintLyricStage.vue'), 'utf8');

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
    duration: 100,
    error: null,
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

function deferred<T>() {
  let resolve!: (value: T) => void;
  let reject!: (reason?: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

describe('Lyric cover fallback contract', () => {
  it.each([
    ['Aurora', AuroraLyricStage, 'phosphor'],
    ['Newsprint', NewsprintLyricStage, 'lucide'],
  ])('%s keeps real cover images and uses its icon family for an empty cover', (_skin, Stage, iconFamily) => {
    const withCover = mount(Stage, {
      props: { model: createModel({ coverUrl: 'http://img/cover.jpg' }) },
    });
    expect(withCover.get('[data-test="lyric-cover"]').get('img').attributes('src')).toBe('http://img/cover.jpg');
    withCover.unmount();

    const withoutCover = mount(Stage, {
      props: { model: createModel({ coverUrl: '' }) },
    });
    const cover = withoutCover.get('[data-test="lyric-cover"]');
    expect(cover.find('img').exists()).toBe(false);
    const placeholder = cover.get('[data-test="lyric-cover-placeholder"]');
    expect(placeholder.attributes('data-icon-family')).toBe(iconFamily);
    expect(placeholder.attributes('aria-hidden')).toBe('true');
    withoutCover.unmount();
  });
});

describe('Lyric request race boundaries', () => {
  it('keeps B lyrics/loading and ignores A follow work after A resolves late', async () => {
    vi.useFakeTimers();
    const aDetail = deferred<{ status: number; lyric: string }>();
    const bDetail = deferred<{ status: number; lyric: string }>();
    lyricApiGetMock.mockReset();
    lyricApiGetMock.mockImplementation((path: string, query: { hash?: string; id?: string }) => {
      if (path === '/search/lyric') {
        return Promise.resolve({
          status: 1,
          candidates: [{ id: query.hash, accesskey: 'key' }],
        });
      }
      if (query.id === 'a') return aDetail.promise;
      if (query.id === 'b') return bDetail.promise;
      throw new Error(`unexpected lyric id ${query.id}`);
    });

    mockPlayerStoreHolder.value.currentTrack = {
      FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 100, Image: '',
    };
    const Harness = defineComponent({
      setup() {
        return useLyricStage();
      },
      render() {
        return h('div', this.model.parsedLyrics.map((_, index) =>
          h('div', { id: `lyric-line-${index}` }),
        ));
      },
    });
    const wrapper = mount(Harness);
    const scrollIntoView = vi.fn();
    const getElementById = vi.spyOn(document, 'getElementById').mockImplementation(() => ({
      scrollIntoView,
    } as any));
    try {
      await flushPromises();

      mockPlayerStoreHolder.value.currentTrack = {
        FileHash: 'b', SongName: 'B', SingerName: 'Artist', Duration: 100, Image: '',
      };
      await nextTick();
      await flushPromises();

      aDetail.resolve({ status: 1, lyric: '[00:00.00]A line\n[00:05.00]A second' });
      await flushPromises();

      expect.soft(wrapper.vm.model.parsedLyrics.map((line: { text: string }) => line.text)).not.toContain('A line');
      expect.soft(wrapper.vm.model.currentTrack?.FileHash).toBe('b');
      expect.soft(wrapper.vm.model.loading).toBe(true);

      bDetail.resolve({ status: 1, lyric: '[00:00.00]B line' });
      await flushPromises();
      await nextTick();
      scrollIntoView.mockClear();

      vi.advanceTimersByTime(480);
      await nextTick();
      await flushPromises();
      expect.soft(wrapper.vm.model.parsedLyrics.map((line: { text: string }) => line.text)).toEqual(['B line']);
      expect.soft(wrapper.vm.model.loading).toBe(false);
      expect.soft(scrollIntoView).toHaveBeenCalledTimes(1);
    } finally {
      getElementById.mockRestore();
      wrapper.unmount();
      mockPlayerStoreHolder.value.currentTrack = null;
      vi.useRealTimers();
    }
  });

  it('does not let an old lyric follow timer scroll after switching to B', async () => {
    vi.useFakeTimers();
    const aDetail = deferred<{ status: number; lyric: string }>();
    const bDetail = deferred<{ status: number; lyric: string }>();
    lyricApiGetMock.mockReset();
    lyricApiGetMock.mockImplementation((path: string, query: { hash?: string; id?: string }) => {
      if (path === '/search/lyric') {
        return Promise.resolve({ status: 1, candidates: [{ id: query.hash, accesskey: 'key' }] });
      }
      return query.id === 'a' ? aDetail.promise : bDetail.promise;
    });
    mockPlayerStoreHolder.value.currentTrack = {
      FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 100, Image: '',
    };
    const Harness = defineComponent({
      setup() { return useLyricStage(); },
      render() { return h('div'); },
    });
    const wrapper = mount(Harness);
    const scrollIntoView = vi.fn();
    const getElementById = vi.spyOn(document, 'getElementById').mockImplementation(() => ({ scrollIntoView } as any));
    try {
      await flushPromises();
      aDetail.resolve({ status: 1, lyric: '[00:00.00]A line' });
      await flushPromises();
      scrollIntoView.mockClear();
      vi.advanceTimersByTime(200);

      mockPlayerStoreHolder.value.currentTrack = {
        FileHash: 'b', SongName: 'B', SingerName: 'Artist', Duration: 100, Image: '',
      };
      await nextTick();
      await flushPromises();
      bDetail.resolve({ status: 1, lyric: '[00:00.00]B line' });
      await flushPromises();
      await nextTick();
      await flushPromises();
      const baselineScrolls = scrollIntoView.mock.calls.length;

      vi.advanceTimersByTime(279);
      await nextTick();
      await flushPromises();
      expect.soft(scrollIntoView).toHaveBeenCalledTimes(baselineScrolls);

      vi.advanceTimersByTime(1);
      await nextTick();
      await flushPromises();
      expect.soft(scrollIntoView).toHaveBeenCalledTimes(baselineScrolls);

      vi.advanceTimersByTime(200);
      await nextTick();
      await flushPromises();

      expect.soft(wrapper.vm.model.parsedLyrics.map((line: { text: string }) => line.text)).toEqual(['B line']);
      expect.soft(scrollIntoView).toHaveBeenCalledTimes(baselineScrolls + 1);
    } finally {
      getElementById.mockRestore();
      wrapper.unmount();
      mockPlayerStoreHolder.value.currentTrack = null;
      vi.useRealTimers();
    }
  });
});

describe('Lyric resource stage adapter', () => {
  it('does not schedule immediate-load follow before mount after its scope is disposed', async () => {
    vi.useFakeTimers();
    const detail = deferred<{ status: number; lyric: string }>();
    lyricApiGetMock.mockReset();
    lyricApiGetMock.mockImplementation((path: string) => {
      if (path === '/search/lyric') {
        return Promise.resolve({ status: 1, candidates: [{ id: 'a', accesskey: 'key' }] });
      }
      return detail.promise;
    });
    mockPlayerStoreHolder.value.currentTrack = {
      FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 100, Image: '',
    };
    const getElementById = vi.spyOn(document, 'getElementById');
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});
    const scope = effectScope();
    try {
      scope.run(() => useLyricStage());
      await flushPromises();
      scope.stop();

      detail.resolve({ status: 1, lyric: '[00:00.00]Late line' });
      await flushPromises();
      await nextTick();
      vi.runAllTimers();
      await nextTick();

      expect(getElementById).not.toHaveBeenCalled();
    } finally {
      scope.stop();
      warn.mockRestore();
      getElementById.mockRestore();
      mockPlayerStoreHolder.value.currentTrack = null;
      vi.useRealTimers();
    }
  });

  it('exposes a load error without fabricating a lyric line and retries the current track', async () => {
    lyricApiGetMock.mockReset();
    lyricApiGetMock
      .mockRejectedValueOnce(new Error('lyrics unavailable'))
      .mockResolvedValueOnce({ status: 1, candidates: [{ id: 'b', accesskey: 'key' }] })
      .mockResolvedValueOnce({ status: 1, lyric: '[00:00.00]Recovered line' });
    mockPlayerStoreHolder.value.currentTrack = {
      FileHash: 'b', SongName: 'B', SingerName: 'Artist', Duration: 100, Image: '',
    };
    const Harness = defineComponent({
      setup() {
        return useLyricStage();
      },
      render() {
        return h('div');
      },
    });
    const wrapper = mount(Harness);
    try {
      await flushPromises();

      expect.soft(wrapper.vm.model.parsedLyrics).toEqual([]);
      expect.soft(wrapper.vm.model.error).toEqual(expect.objectContaining({ message: 'lyrics unavailable' }));
      const retryLyrics = wrapper.vm.commands.retryLyrics;
      expect.soft(retryLyrics).toEqual(expect.any(Function));

      await retryLyrics();
      await flushPromises();

      expect.soft(wrapper.vm.model.parsedLyrics).toEqual([{ time: 0, text: 'Recovered line' }]);
      expect.soft(wrapper.vm.model.error).toBeNull();
    } finally {
      wrapper.unmount();
      mockPlayerStoreHolder.value.currentTrack = null;
    }
  });

  it('clears delayed entry follow before manual seek and re-follow', async () => {
    vi.useFakeTimers();
    lyricApiGetMock.mockReset();
    lyricApiGetMock.mockImplementation((path: string) => {
      if (path === '/search/lyric') {
        return Promise.resolve({ status: 1, candidates: [{ id: 'a', accesskey: 'key' }] });
      }
      return Promise.resolve({ status: 1, lyric: '[00:00.00]A line' });
    });
    mockPlayerStoreHolder.value.currentTrack = {
      FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 100, Image: '',
    };
    const Harness = defineComponent({
      setup() { return useLyricStage(); },
      render() { return h('div'); },
    });
    const wrapper = mount(Harness);
    const scrollIntoView = vi.fn();
    const getElementById = vi.spyOn(document, 'getElementById').mockImplementation(() => ({ scrollIntoView } as any));
    try {
      await flushPromises();
      await nextTick();
      scrollIntoView.mockClear();

      wrapper.vm.commands.seekToLine(0);
      wrapper.vm.commands.resumeFollow();
      await nextTick();
      const immediateScrolls = scrollIntoView.mock.calls.length;

      vi.advanceTimersByTime(480);
      await nextTick();
      await flushPromises();

      expect(scrollIntoView).toHaveBeenCalledTimes(immediateScrolls);
    } finally {
      getElementById.mockRestore();
      wrapper.unmount();
      mockPlayerStoreHolder.value.currentTrack = null;
      vi.useRealTimers();
    }
  });

  it('does not run delayed entry follow after the stage unmounts', async () => {
    vi.useFakeTimers();
    lyricApiGetMock.mockReset();
    lyricApiGetMock.mockImplementation((path: string) => {
      if (path === '/search/lyric') {
        return Promise.resolve({ status: 1, candidates: [{ id: 'a', accesskey: 'key' }] });
      }
      return Promise.resolve({ status: 1, lyric: '[00:00.00]A line' });
    });
    mockPlayerStoreHolder.value.currentTrack = {
      FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 100, Image: '',
    };
    const Harness = defineComponent({
      setup() { return useLyricStage(); },
      render() { return h('div'); },
    });
    const wrapper = mount(Harness);
    const scrollIntoView = vi.fn();
    const getElementById = vi.spyOn(document, 'getElementById').mockImplementation(() => ({ scrollIntoView } as any));
    try {
      await flushPromises();
      await nextTick();
      scrollIntoView.mockClear();

      wrapper.unmount();
      vi.advanceTimersByTime(480);
      await nextTick();
      await flushPromises();

      expect(scrollIntoView).not.toHaveBeenCalled();
    } finally {
      getElementById.mockRestore();
      mockPlayerStoreHolder.value.currentTrack = null;
      vi.useRealTimers();
    }
  });

  it.each<{
    name: string;
    queueScroll: (commands: LyricStageCommands) => void;
  }>([
    { name: 'resumeFollow', queueScroll: (commands) => commands.resumeFollow() },
    { name: 'seekToLine', queueScroll: (commands) => commands.seekToLine(0) },
  ])('does not access lyric DOM from a queued $name scroll after unmount', async ({ queueScroll }) => {
    lyricApiGetMock.mockReset();
    lyricApiGetMock.mockImplementation((path: string) => {
      if (path === '/search/lyric') {
        return Promise.resolve({ status: 1, candidates: [{ id: 'a', accesskey: 'key' }] });
      }
      return Promise.resolve({ status: 1, lyric: '[00:00.00]A line' });
    });
    mockPlayerStoreHolder.value.currentTrack = {
      FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 100, Image: '',
    };
    const Harness = defineComponent({
      setup() { return useLyricStage(); },
      render() { return h('div'); },
    });
    const wrapper = mount(Harness);
    try {
      await flushPromises();
      await nextTick();
      const getElementById = vi.spyOn(document, 'getElementById');
      try {
        queueScroll(wrapper.vm.commands);
        wrapper.unmount();
        await nextTick();

        expect(getElementById).not.toHaveBeenCalled();
      } finally {
        getElementById.mockRestore();
      }
    } finally {
      wrapper.unmount();
      mockPlayerStoreHolder.value.currentTrack = null;
    }
  });

  it('clears a delayed entry follow when its timer handle is zero', async () => {
    lyricApiGetMock.mockReset();
    lyricApiGetMock.mockImplementation((path: string) => {
      if (path === '/search/lyric') {
        return Promise.resolve({ status: 1, candidates: [{ id: 'a', accesskey: 'key' }] });
      }
      return Promise.resolve({ status: 1, lyric: '[00:00.00]A line' });
    });
    mockPlayerStoreHolder.value.currentTrack = {
      FileHash: 'a', SongName: 'A', SingerName: 'Artist', Duration: 100, Image: '',
    };
    const setTimeoutSpy = vi.spyOn(window, 'setTimeout')
      .mockReturnValue(0 as unknown as ReturnType<typeof window.setTimeout>);
    const clearTimeoutSpy = vi.spyOn(globalThis, 'clearTimeout').mockImplementation(() => {});
    const Harness = defineComponent({
      setup() { return useLyricStage(); },
      render() { return h('div'); },
    });
    const wrapper = mount(Harness);
    try {
      await flushPromises();
      await nextTick();
      expect(setTimeoutSpy).toHaveBeenCalledWith(expect.any(Function), 480);

      wrapper.vm.commands.resumeFollow();

      expect(clearTimeoutSpy).toHaveBeenCalledWith(0);
    } finally {
      wrapper.unmount();
      clearTimeoutSpy.mockRestore();
      setTimeoutSpy.mockRestore();
      mockPlayerStoreHolder.value.currentTrack = null;
    }
  });
});

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

  it('neither Aurora nor Newsprint shows line numbers', () => {
    const model = createModel();
    const aurora = mount(AuroraLyricStage, { props: { model } });
    const newsprint = mount(NewsprintLyricStage, { props: { model } });

    expect(aurora.find('.np-line-num').exists()).toBe(false);
    expect(newsprint.find('.np-line-num').exists()).toBe(false);
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

  it('both stages surface song metadata in their independent meta columns', () => {
    const model = createModel();
    const aurora = mount(AuroraLyricStage, { props: { model } });
    const newsprint = mount(NewsprintLyricStage, { props: { model } });

    expect(aurora.find('[data-test="lyric-cover"]').exists()).toBe(true);
    expect(aurora.get('[data-test="lyric-meta-column"]').text()).toContain('Test Song');
    expect(aurora.get('[data-test="lyric-meta-column"]').text()).toContain('Test Artist');
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

  it('Aurora uses bouncy back.out rise-in for entrance', () => {
    const model = createModel();
    mount(AuroraLyricStage, { props: { model } });

    const eases = extractEases();
    expect(eases.some((e: string) => e.includes('back.out'))).toBe(true);
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
    playTrackMock.mockClear();
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

  it('cover chrome has no text labels or action buttons', () => {
    const focus = useLyricFocusStore();
    focus.init();

    const wrapper = mount(AuroraLyricStage, {
      props: { model: createModel({ fullscreen: false }) },
    });

    expect(wrapper.find('[data-test="lyric-cover"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="lyric-meta-actions"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="lyric-focus-toggle"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="lyric-enter-fs"]').exists()).toBe(false);
    expect(wrapper.find('[data-test="lyric-shelf-open"]').exists()).toBe(false);
    expect(wrapper.find('.aurora-song-title').exists()).toBe(false);
    expect(wrapper.find('.aurora-artist').exists()).toBe(false);
  });

  it('fullscreen opens minimal playlist shelf only from cover click', async () => {
    const wrapper = mount(AuroraLyricStage, {
      props: {
        model: createModel({
          fullscreen: true,
          currentTrack: {
            FileHash: 'q1',
            SongName: 'Queue One',
            SingerName: 'A',
            Duration: 100,
            Image: '',
          } as any,
        }),
      },
      attachTo: document.body,
    });

    expect(document.querySelector('[data-test="aurora-playlist-shelf"]')).toBeNull();
    expect(wrapper.find('[data-test="lyric-shelf-open"]').exists()).toBe(false);

    await wrapper.get('[data-test="lyric-cover"]').trigger('click');
    await nextTick();
    expect(document.querySelector('[data-test="aurora-playlist-shelf"]')).toBeTruthy();
    expect(document.querySelector('[data-test="shelf-card-0"]')).toBeTruthy();
    // Minimal: no prev/next/close/play chrome text buttons
    expect(document.querySelector('[data-test="shelf-prev"]')).toBeNull();
    expect(document.querySelector('[data-test="shelf-next"]')).toBeNull();
    expect(document.querySelector('[data-test="shelf-close"]')).toBeNull();
    expect(document.querySelector('[data-test="shelf-play"]')).toBeNull();

    (document.querySelector('[data-test="shelf-backdrop"]') as HTMLElement).click();
    await nextTick();
    expect(document.querySelector('[data-test="aurora-playlist-shelf"]')).toBeNull();

    wrapper.unmount();
  });

  it('selects a shelf card through playTrack while keeping fullscreen mounted', async () => {
    const wrapper = mount(AuroraLyricStage, {
      props: {
        model: createModel({
          fullscreen: true,
          currentTrack: queueTracks[0] as any,
        }),
      },
      attachTo: document.body,
    });

    await wrapper.get('[data-test="lyric-cover"]').trigger('click');
    await nextTick();

    (document.querySelector('[data-test="shelf-card-1"]') as HTMLButtonElement).click();
    await nextTick();

    expect(playTrackMock).toHaveBeenCalledWith(queueTracks[1]);
    expect(document.querySelector('[data-test="aurora-playlist-shelf"]')).toBeNull();
    expect(wrapper.find('.aurora-lyric-fullscreen').exists()).toBe(true);

    wrapper.unmount();
  });

  it('Newsprint stage does not expose dual-mode lyric focus toggle', () => {
    const focus = useLyricFocusStore();
    focus.init();

    const wrapper = mount(NewsprintLyricStage, {
      props: { model: createModel({ fullscreen: false }) },
    });
    expect(wrapper.find('[data-test="lyric-focus-toggle"]').exists()).toBe(false);
  });

  it('fullscreen shows cover wash; reduced-motion and non-fs hide it', () => {
    isReducedMotionMock.mockReturnValue(false);
    const fs = mount(AuroraLyricStage, {
      props: {
        model: createModel({
          fullscreen: true,
          coverUrl: 'http://img/cover.jpg',
        }),
      },
    });
    expect(fs.find('[data-test="lyric-cover-wash"]').exists()).toBe(true);
    expect(fs.find('[data-test="lyric-cover-wash"]').attributes('style') || '').toContain(
      'http://img/cover.jpg',
    );
    const fsWashToCalls = gsapToMock.mock.calls.filter((c: any[]) => {
      const el = c[0];
      return el?.getAttribute?.('data-test') === 'lyric-cover-wash';
    });
    const fsWashSetCalls = gsapSetMock.mock.calls.filter((c: any[]) => {
      const el = c[0];
      return el?.getAttribute?.('data-test') === 'lyric-cover-wash';
    });
    expect(
      fsWashToCalls.some((c: any[]) => c[1]?.opacity === 0.9) ||
      fsWashSetCalls.some((c: any[]) => c[1]?.opacity === 0.9),
    ).toBe(true);

    clearGsapMocks();

    const nonFs = mount(AuroraLyricStage, {
      props: { model: createModel({ fullscreen: false, coverUrl: 'http://img/cover.jpg' }) },
    });
    expect(nonFs.find('[data-test="lyric-cover-wash"]').exists()).toBe(true);
    const nonFsWashCalls = gsapSetMock.mock.calls.filter((c: any[]) => {
      const el = c[0];
      return el?.getAttribute?.('data-test') === 'lyric-cover-wash';
    });
    expect(nonFsWashCalls.some((c: any[]) => c[1]?.opacity === 0)).toBe(true);

    clearGsapMocks();
    isReducedMotionMock.mockReturnValue(true);
    const reduced = mount(AuroraLyricStage, {
      props: {
        model: createModel({ fullscreen: true, coverUrl: 'http://img/cover.jpg' }),
      },
    });
    expect(reduced.find('[data-test="lyric-cover-wash"]').exists()).toBe(false);
    isReducedMotionMock.mockReturnValue(false);
  });

  it('fullscreen keeps lightweight playback controls available', () => {
    const fullscreen = mount(AuroraLyricStage, {
      props: { model: createModel({ fullscreen: true, duration: 240 }) },
    });
    const readability = fullscreen.find('[data-test="aurora-fs-readability"]');
    const controls = fullscreen.find('[data-test="aurora-fs-controls"]');
    const meta = fullscreen.get('[data-test="lyric-meta-column"]');
    const cover = fullscreen.get('[data-test="lyric-cover"]');

    expect(readability.exists()).toBe(true);
    expect(readability.attributes('data-contrast')).toBe('high');
    expect(controls.exists()).toBe(true);
    expect(controls.attributes('data-contrast')).toBe('high');
    expect(controls.attributes('data-visual-weight')).toBe('subtle');
    expect(controls.find('[data-test="aurora-fs-play"], [data-test="aurora-fs-pause"]').exists()).toBe(true);
    expect(controls.findComponent({ name: 'PlayerProgress' }).exists()).toBe(true);
    // Anchored under title/meta text in the left column (not under cover, not lyric column).
    expect(meta.element.contains(cover.element)).toBe(true);
    expect(meta.element.contains(controls.element)).toBe(true);
    const metaText = meta.find('.aurora-lyric-track-meta');
    expect(metaText.exists()).toBe(true);
    const exitRow = fullscreen.find('[data-test="aurora-fs-exit-row"]');
    expect(exitRow.exists()).toBe(true);
    expect(meta.element.contains(exitRow.element)).toBe(true);
    // Exit-fs only under meta (no window-minimize here).
    expect(exitRow.find('[data-test="fs-exit-fullscreen"]').exists()).toBe(true);
    expect(exitRow.find('[data-test="fs-minimize"]').exists()).toBe(false);
    // DOM order: cover → track-meta → transport → exit-fs
    const kids = Array.from(meta.element.children) as HTMLElement[];
    const coverIdx = kids.indexOf(cover.element as HTMLElement);
    const metaIdx = kids.indexOf(metaText.element as HTMLElement);
    const controlsIdx = kids.indexOf(controls.element as HTMLElement);
    const exitIdx = kids.indexOf(exitRow.element as HTMLElement);
    expect(coverIdx).toBeGreaterThanOrEqual(0);
    expect(metaIdx).toBeGreaterThan(coverIdx);
    expect(controlsIdx).toBeGreaterThan(metaIdx);
    expect(exitIdx).toBeGreaterThan(controlsIdx);
    expect(
      fullscreen.find('[data-test="lyric-content-column"] [data-test="aurora-fs-controls"]').exists(),
    ).toBe(false);
  });

  it('clicking a lyric line emits seek-line with that line timestamp', async () => {
    const wrapper = mount(AuroraLyricStage, {
      props: {
        model: createModel({
          parsedLyrics: [
            { time: 0, text: 'First line' },
            { time: 5, text: 'Second line' },
            { time: 12.5, text: 'Third line' },
          ],
        }),
      },
    });

    await wrapper.get('[data-test="lyric-line-1"]').trigger('click');
    expect(wrapper.emitted('seek-line')).toBeTruthy();
    expect(wrapper.emitted('seek-line')![0]).toEqual([5]);

    await wrapper.get('[data-test="lyric-line-2"]').trigger('click');
    expect(wrapper.emitted('seek-line')![1]).toEqual([12.5]);
  });

  it('lyric-scroll stays flex-fill without thin scrollbar chrome', () => {
    const wrapper = mount(AuroraLyricStage, {
      props: { model: createModel({ fullscreen: true }) },
    });
    const scroll = wrapper.find('[data-test="lyric-scroll"]');
    expect(scroll.exists()).toBe(true);
    expect(scroll.classes()).not.toContain('with-scrollbar');
  });
});

describe('Aurora playlist shelf selection', () => {
  it('shows a clear Chinese empty state for an empty queue', () => {
    const wrapper = mount(AuroraPlaylistShelf, {
      props: { open: true, tracks: [], activeHash: null },
      attachTo: document.body,
    });

    try {
      const empty = document.querySelector('[data-test="shelf-empty"]');
      expect(empty?.textContent).toContain('播放队列为空');
      expect(empty?.getAttribute('role')).toBe('status');
      expect(empty?.getAttribute('aria-label')).toContain('播放队列为空');
    } finally {
      wrapper.unmount();
    }
  });

  it('follows the active queue item when the shelf is not being interacted with', async () => {
    const wrapper = mount(AuroraPlaylistShelf, {
      props: {
        open: true,
        tracks: queueTracks as any,
        activeHash: queueTracks[0].FileHash,
      },
      attachTo: document.body,
    });

    try {
      await wrapper.setProps({ activeHash: queueTracks[1].FileHash });
      await nextTick();

      expect(document.querySelector('[data-test="shelf-card-1"]')?.classList.contains('is-focus')).toBe(true);
    } finally {
      wrapper.unmount();
    }
  });

  it('keeps an active track beyond the first 32 items inside the visible shelf window', async () => {
    const longQueue = Array.from({ length: 40 }, (_, index) => ({
      FileHash: `long-${index}`,
      SongName: `Long Queue ${index}`,
      SingerName: 'Artist',
      Duration: 180,
      Image: '',
    }));
    const activeTrack = longQueue[35];
    const wrapper = mount(AuroraPlaylistShelf, {
      props: {
        open: true,
        tracks: longQueue as any,
        activeHash: activeTrack.FileHash,
      },
      attachTo: document.body,
    });

    try {
      await nextTick();
      const cards = Array.from(document.querySelectorAll('.shelf-card'));
      const activeCard = document.querySelector('.shelf-card.is-active');

      expect(cards).toHaveLength(32);
      expect(activeCard?.getAttribute('aria-label')).toBe(activeTrack.SongName);
      expect(activeCard?.classList.contains('is-focus')).toBe(true);
    } finally {
      wrapper.unmount();
    }
  });

  it('pauses active-item follow during hover and resumes when hover ends', async () => {
    const wrapper = mount(AuroraPlaylistShelf, {
      props: {
        open: true,
        tracks: queueTracks as any,
        activeHash: queueTracks[0].FileHash,
      },
      attachTo: document.body,
    });
    try {
      const stage = document.querySelector('[data-test="shelf-stage"]') as HTMLElement;

      stage.dispatchEvent(new MouseEvent('mouseenter', { bubbles: true }));
      await wrapper.setProps({ activeHash: queueTracks[1].FileHash });
      await nextTick();
      expect(document.querySelector('[data-test="shelf-card-0"]')?.classList.contains('is-focus')).toBe(true);

      stage.dispatchEvent(new MouseEvent('mouseleave', { bubbles: true }));
      await nextTick();
      expect(document.querySelector('[data-test="shelf-card-1"]')?.classList.contains('is-focus')).toBe(true);
    } finally {
      wrapper.unmount();
    }
  });

  it('keeps the dragged focus during active changes and restores active after pointerup', async () => {
    const wrapper = mount(AuroraPlaylistShelf, {
      props: {
        open: true,
        tracks: queueTracks as any,
        activeHash: queueTracks[0].FileHash,
      },
      attachTo: document.body,
    });

    try {
      const stage = document.querySelector('[data-test="shelf-stage"]') as HTMLElement;
      const pointerId = 8;

      stage.dispatchEvent(new PointerEvent('pointerdown', {
        bubbles: true,
        button: 0,
        clientX: 100,
        pointerId,
      }));
      await wrapper.setProps({ activeHash: queueTracks[1].FileHash });
      await nextTick();
      expect(document.querySelector('[data-test="shelf-card-0"]')?.classList.contains('is-focus')).toBe(true);

      stage.dispatchEvent(new PointerEvent('pointermove', {
        bubbles: true,
        clientX: 0,
        pointerId,
      }));
      await nextTick();
      expect(document.querySelector('[data-test="shelf-card-1"]')?.classList.contains('is-focus')).toBe(true);

      await wrapper.setProps({ activeHash: queueTracks[0].FileHash });
      await nextTick();
      expect(document.querySelector('[data-test="shelf-card-1"]')?.classList.contains('is-focus')).toBe(true);

      stage.dispatchEvent(new PointerEvent('pointerup', {
        bubbles: true,
        button: 0,
        clientX: 0,
        pointerId,
      }));
      await new Promise((resolve) => setTimeout(resolve, 0));
      await nextTick();
      expect(document.querySelector('[data-test="shelf-card-0"]')?.classList.contains('is-focus')).toBe(true);
    } finally {
      wrapper.unmount();
    }
  });

  it('does not select after a drag produces a synthetic card click', async () => {
    const wrapper = mount(AuroraPlaylistShelf, {
      props: {
        open: true,
        tracks: queueTracks as any,
        activeHash: queueTracks[0].FileHash,
      },
      attachTo: document.body,
    });

    const stage = document.querySelector('[data-test="shelf-stage"]') as HTMLElement;
    const pointerId = 7;
    stage.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, clientX: 20, pointerId }));
    stage.dispatchEvent(new PointerEvent('pointermove', { bubbles: true, clientX: 29, pointerId }));
    stage.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, button: 0, clientX: 29, pointerId }));
    const card = document.querySelector('[data-test="shelf-card-1"]') as HTMLButtonElement;
    card.click();
    await nextTick();

    expect(wrapper.emitted('select')).toBeUndefined();

    await new Promise((resolve) => setTimeout(resolve, 0));
    card.click();
    await nextTick();

    expect(wrapper.emitted('select')).toEqual([[queueTracks[1]]]);

    wrapper.unmount();
  });
});

describe('Task 3 non-fullscreen two-column contract', () => {
  it.each([
    ['Aurora', AuroraLyricStage],
    ['Newsprint', NewsprintLyricStage],
  ])('%s exposes independent meta and lyric columns at the narrow-layout contract', (_skin, Stage) => {
    const wrapper = mount(Stage, {
      props: { model: createModel({ fullscreen: false }) },
    });

    wrapper.get('[data-test="lyric-meta-column"]');
    expect(wrapper.get('[data-test="lyric-content-column"]').attributes('data-layout')).toBe('two-column');
  });

  it.each([
    ['Aurora', AuroraLyricStage],
    ['Newsprint', NewsprintLyricStage],
  ])('%s shows track metadata and an icon-only fullscreen entry below the cover', async (_skin, Stage) => {
    const wrapper = mount(Stage, {
      props: {
        model: createModel({
          fullscreen: false,
          currentTrack: {
            FileHash: 'meta',
            SongName: '纸月亮',
            SingerName: '测试歌手',
            AlbumName: '夜航专辑',
            Duration: 180,
            Image: 'http://img/',
          } as any,
        }),
      },
    });

    const meta = wrapper.get('[data-test="lyric-meta-column"]');
    expect(meta.text()).toContain('纸月亮');
    expect(meta.text()).toContain('测试歌手');
    expect(meta.text()).toContain('夜航专辑');

    const fullscreen = meta.get('[data-test="lyric-enter-fullscreen"]');
    expect(fullscreen.text().trim()).toBe('');
    expect(fullscreen.find('svg').exists()).toBe(true);
    expect(fullscreen.attributes('aria-label')).toMatch(/[\u3400-\u9fff]/);
    expect(fullscreen.attributes('title')).toMatch(/[\u3400-\u9fff]/);
    await fullscreen.trigger('click');
    expect(wrapper.emitted('enter-fullscreen')).toHaveLength(1);
  });

  it('Aurora opens fullscreen on cover double-click but never opens the shelf on a normal-mode click', async () => {
    const wrapper = mount(AuroraLyricStage, {
      props: { model: createModel({ fullscreen: false }) },
    });

    await wrapper.get('[data-test="lyric-cover"]').trigger('click');
    expect(wrapper.getComponent(AuroraPlaylistShelf).props('open')).toBe(false);

    await wrapper.get('[data-test="lyric-cover"]').trigger('dblclick');
    expect(wrapper.emitted('enter-fullscreen')).toHaveLength(1);
  });

  it('makes the Aurora cover keyboard-operable only when fullscreen shelf access is active', async () => {
    const normal = mount(AuroraLyricStage, {
      props: { model: createModel({ fullscreen: false }) },
    });
    expect(normal.get('[data-test="lyric-cover"]').attributes('role')).toBeUndefined();
    expect(normal.get('[data-test="lyric-cover"]').attributes('tabindex')).toBeUndefined();

    const fullscreen = mount(AuroraLyricStage, {
      props: { model: createModel({ fullscreen: true }) },
    });
    const cover = fullscreen.get('[data-test="lyric-cover"]');
    expect(cover.attributes('role')).toBe('button');
    expect(cover.attributes('tabindex')).toBe('0');

    await cover.trigger('keydown', { key: 'Enter' });
    expect(fullscreen.getComponent(AuroraPlaylistShelf).props('open')).toBe(true);
  });
});

describe('Task 4 fullscreen controls', () => {
  it.each([
    ['Aurora', AuroraLyricStage, 'aurora-fs-controls'],
    ['Newsprint', NewsprintLyricStage, 'newsprint-fs-controls'],
  ])('%s exposes an intent-revealed fullscreen transport', async (_skin, Stage, testId) => {
    vi.useFakeTimers();
    const wrapper = mount(Stage, {
      props: { model: createModel({ fullscreen: true, duration: 240 }) },
    });

    const controls = wrapper.get(`[data-test="${testId}"]`);
    expect(controls.attributes('data-visible')).toBe('false');

    await wrapper.get('[data-test$="lyric-stage"]').trigger('pointermove');
    expect(controls.attributes('data-visible')).toBe('true');

    wrapper.unmount();
    vi.useRealTimers();
  });

  it('renders the Newsprint fullscreen transport as icon-only play and progress controls', () => {
    const wrapper = mount(NewsprintLyricStage, {
      props: { model: createModel({ fullscreen: true, duration: 240 }) },
    });
    const controls = wrapper.get('[data-test="newsprint-fs-controls"]');

    const playButton = controls.get('button');
    expect(playButton.text().trim()).toBe('');
    expect(playButton.find('svg').exists()).toBe(true);
    expect(controls.findComponent({ name: 'PlayerProgress' }).exists()).toBe(true);
  });

  it('anchors the Newsprint fullscreen transport in the lower-left metadata space', () => {
    expect(newsprintStageSource).toMatch(
      /\.np-lyric-fullscreen\s*\{[\s\S]*?position:\s*relative;/,
    );
    expect(newsprintStageSource).toMatch(
      /\.np-lyric-fullscreen \.np-fs-controls\s*\{[\s\S]*?position:\s*absolute;[\s\S]*?left:\s*48px;[\s\S]*?bottom:\s*28px;/,
    );
  });
});

describe('AuroraLyricStage keyboard navigation', () => {
  it('emits seek-line with the next line time on ArrowDown', async () => {
    const model = createModel({ activeIndex: 1 });
    const wrapper = mount(AuroraLyricStage, { props: { model } });
    const scroll = wrapper.find('[data-test="lyric-scroll"]');
    await scroll.trigger('keydown', { key: 'ArrowDown' });
    const events = wrapper.emitted('seek-line');
    expect(events).toBeTruthy();
    expect(events![0]).toEqual([model.parsedLyrics[2].time]);
  });

  it('emits seek-line with the previous line time on ArrowUp', async () => {
    const model = createModel({ activeIndex: 1 });
    const wrapper = mount(AuroraLyricStage, { props: { model } });
    const scroll = wrapper.find('[data-test="lyric-scroll"]');
    await scroll.trigger('keydown', { key: 'ArrowUp' });
    const events = wrapper.emitted('seek-line');
    expect(events).toBeTruthy();
    expect(events![0]).toEqual([model.parsedLyrics[0].time]);
  });

  it('does nothing at the last line on ArrowDown', async () => {
    const lastIndex = SAMPLE_LINES.length - 1;
    const model = createModel({ activeIndex: lastIndex });
    const wrapper = mount(AuroraLyricStage, { props: { model } });
    await wrapper.find('[data-test="lyric-scroll"]').trigger('keydown', { key: 'ArrowDown' });
    expect(wrapper.emitted('seek-line')).toBeFalsy();
  });
});
