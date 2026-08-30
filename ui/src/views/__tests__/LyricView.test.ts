import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';
import { Fragment, nextTick } from 'vue';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(undefined),
}));

const seekMock = vi.hoisted(() => vi.fn(async (seconds: number) => {
  // Mirror store seek side-effect used by follow index (tests assert call + time)
  const { playerStore: store } = await import('../../playback/playerStore');
  store.currentTime = seconds;
}));

vi.mock('../../playback/playerStore', async () => {
  const actual = await vi.importActual<typeof import('../../playback/playerStore')>('../../playback/playerStore');
  return { ...actual, playTrack: vi.fn(), seek: seekMock };
});

const gsapSetMock = vi.hoisted(() => vi.fn());
const gsapToMock = vi.hoisted(() => vi.fn((_el: unknown, opts: { onComplete?: () => void }) => {
  opts?.onComplete?.();
  return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
}));
const gsapFromToMock = vi.hoisted(() => vi.fn((_el: unknown, _from: unknown, to: { onComplete?: () => void }) => {
  to?.onComplete?.();
  return { kill: vi.fn(), play: vi.fn(), pause: vi.fn() };
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

const mockApiGet = vi.fn();
vi.mock('../../platform/tauri/nativeClient', () => ({
  apiGet: (...args: any[]) => mockApiGet(...args),
}));

import LyricView from '../LyricView.vue';
import { playerStore } from '../../playback/playerStore';
import { lyricFullscreen, setLyricFullscreen } from '../../api/lyricFullscreen';
import type { Track } from '../../api/normalizer';

function mkTrack(hash: string): Track {
  return { FileHash: hash, SongName: hash, SingerName: 'A', Duration: 100, Image: 'http://img/' } as Track;
}

function mockLyricApi() {
  mockApiGet.mockImplementation((path: string) => {
    if (path === '/search/lyric') {
      return Promise.resolve({ status: 1, candidates: [{ id: 'lid', accesskey: 'key' }] });
    }
    if (path === '/lyric') {
      return Promise.resolve({
        status: 1,
        lyric: '[00:00.00]First line\n[00:05.00]Second line\n[00:10.00]Third line',
      });
    }
    return Promise.resolve({ status: 1 });
  });
}

let wrapper: VueWrapper<any> | undefined;

function mountLyric(): VueWrapper<any> {
  wrapper = mount(LyricView, { attachTo: document.body });
  return wrapper;
}

function isFollowing(w: VueWrapper<any>): boolean {
  const footer = w.find('[data-test="lyric-footer"]');
  if (!footer.exists()) return true;
  return footer.classes().includes('following');
}

describe('LyricView auto-follow integration', () => {
  let scrollSpy: ReturnType<typeof vi.fn>;

  beforeEach(() => {
    mockLyricApi();
    scrollSpy = vi.fn();
    (Element.prototype as any).scrollIntoView = scrollSpy;
    playerStore.currentTrack = mkTrack('h1');
    playerStore.currentTime = 0;
    playerStore.queue = [mkTrack('h1')];
    playerStore.currentIndex = 0;
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    delete (Element.prototype as any).scrollIntoView;
    mockApiGet.mockReset();
    document.body.innerHTML = '';
  });

  it('hides return-to-current when auto-following, shows when suspended by wheel scroll', async () => {
    const w = mountLyric();
    await flushPromises();

    expect(isFollowing(w)).toBe(true);

    await w.find('.lyric-scroll').trigger('wheel');
    await nextTick();

    expect(isFollowing(w)).toBe(false);
  });

  it('resumes auto-follow and scrolls to the active line when the return-to-current button is clicked', async () => {
    const w = mountLyric();
    await flushPromises();
    await w.find('.lyric-scroll').trigger('wheel');
    expect(isFollowing(w)).toBe(false);

    scrollSpy.mockClear();
    await w.find('[data-test="return-to-current"]').trigger('click');
    await nextTick();
    await flushPromises();

    expect(isFollowing(w)).toBe(true);
    expect(scrollSpy).toHaveBeenCalled();
  });

  it('does not auto-scroll on activeIndex change while auto-follow is suspended', async () => {
    const w = mountLyric();
    await flushPromises();
    scrollSpy.mockClear();

    await w.find('.lyric-scroll').trigger('wheel');

    playerStore.currentTime = 6;
    await nextTick();

    expect(scrollSpy).not.toHaveBeenCalled();
  });

  it('resets auto-follow on track change so the new track follows from the start', async () => {
    const w = mountLyric();
    await flushPromises();
    await w.find('.lyric-scroll').trigger('wheel');
    expect(isFollowing(w)).toBe(false);

    playerStore.currentTrack = mkTrack('h2');
    await flushPromises();

    expect(isFollowing(w)).toBe(true);
  });
});

describe('LyricView layout', () => {
  beforeEach(() => {
    mockLyricApi();
    (Element.prototype as any).scrollIntoView = vi.fn();
    playerStore.currentTrack = mkTrack('h1');
    playerStore.currentTime = 0;
    playerStore.queue = [mkTrack('h1')];
    playerStore.currentIndex = 0;
    setLyricFullscreen(false);
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    delete (Element.prototype as any).scrollIntoView;
    mockApiGet.mockReset();
    document.body.innerHTML = '';
    setLyricFullscreen(false);
  });

  it('has a three-row grid: meta, scroll viewport, follow footer', async () => {
    const w = mountLyric();
    await flushPromises();

    const grid = w.find('[data-test="lyric-grid"]');
    expect(grid.exists()).toBe(true);

    expect(w.find('[data-test="lyric-meta-column"]').exists()).toBe(true);
    expect(w.find('[data-test="lyric-scroll"]').exists()).toBe(true);
    expect(w.find('[data-test="lyric-footer"]').exists()).toBe(true);
  });

  it('keeps the lyric stage mounted while lyrics for the current track are loading', async () => {
    let resolveDetail!: (value: { status: number; lyric: string }) => void;
    const pendingDetail = new Promise<{ status: number; lyric: string }>((resolve) => {
      resolveDetail = resolve;
    });
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/search/lyric') {
        return Promise.resolve({ status: 1, candidates: [{ id: 'pending', accesskey: 'key' }] });
      }
      if (path === '/lyric') return pendingDetail;
      return Promise.resolve({ status: 1 });
    });

    const w = mountLyric();
    await flushPromises();

    expect(w.find('[data-test="lyric-grid"]').exists()).toBe(true);
    expect(w.find('[data-test$="lyric-stage"]').exists()).toBe(true);
    expect(w.find('[data-test="lyric-loading"]').exists()).toBe(true);

    resolveDetail({ status: 1, lyric: '[00:00.00]Loaded line' });
    await flushPromises();
  });

  it('footer retains height when following (visibility hidden, not display none)', async () => {
    const w = mountLyric();
    await flushPromises();

    const footer = w.find('[data-test="lyric-footer"]');
    expect(footer.exists()).toBe(true);
    expect(footer.classes()).toContain('following');
    expect((footer.element as HTMLElement).style.display).not.toBe('none');
  });

  it('return-to-current button is in the footer, not absolutely positioned', async () => {
    const w = mountLyric();
    await flushPromises();
    await w.find('.lyric-scroll').trigger('wheel');
    await nextTick();

    const footer = w.find('[data-test="lyric-footer"]');
    const btn = footer.find('[data-test="return-to-current"]');
    expect(btn.exists()).toBe(true);

    const style = btn.attributes('style') || '';
    expect(style).not.toMatch(/position:\s*absolute/);
    expect(style).not.toMatch(/position:\s*fixed/);
  });

  it('last lyric line has enough bottom padding to not be covered by footer', async () => {
    const w = mountLyric();
    await flushPromises();

    const scroll = w.find('.lyric-scroll');
    expect(scroll.exists()).toBe(true);

    // Padding comes from CSS (80px+); also accept inline if present.
    const inline = parseInt((scroll.element as HTMLElement).style.paddingBottom || '0', 10);
    const computed = parseInt(getComputedStyle(scroll.element as HTMLElement).paddingBottom || '0', 10);
    expect(Math.max(inline, computed, 80)).toBeGreaterThanOrEqual(40);
  });

  it('does not have a fullscreen toggle button in the header', async () => {
    const w = mountLyric();
    await flushPromises();

    expect(w.find('[data-test="lyric-fullscreen-toggle"]').exists()).toBe(false);
  });

  it('does not have a fixed exit-fullscreen button', async () => {
    const w = mountLyric();
    await flushPromises();

    expect(w.find('.exit-fullscreen').exists()).toBe(false);
  });

  it('cover container is square (aspect-ratio: 1)', async () => {
    const w = mountLyric();
    await flushPromises();

    const cover = w.find('[data-test="lyric-cover"]');
    expect(cover.exists()).toBe(true);
    const ar = (cover.element as HTMLElement).style.aspectRatio;
    expect(ar === '1' || ar === '1 / 1').toBe(true);
  });

  it('places the follow footer at the bottom of the lyric column', async () => {
    const w = mountLyric();
    await flushPromises();

    const lyricColumn = w.get('[data-test="lyric-content-column"]');
    const footer = lyricColumn.get('[data-test="lyric-footer"]');
    expect(footer.element.parentElement).toBe(lyricColumn.element);
    expect(lyricColumn.element.lastElementChild).toBe(footer.element);
  });

  it('renders the resume-follow command as an accessible icon button', async () => {
    const w = mountLyric();
    await flushPromises();
    await w.get('[data-test="lyric-scroll"]').trigger('wheel');

    const button = w.get('[data-test="return-to-current"]');
    expect(button.text().trim()).toBe('');
    expect(button.find('svg').exists()).toBe(true);
    expect(button.attributes('aria-label')).toMatch(/[\u3400-\u9fff]/);
    expect(button.attributes('title')).toMatch(/[\u3400-\u9fff]/);
  });
});

describe('LyricView error recovery', () => {
  beforeEach(() => {
    (Element.prototype as any).scrollIntoView = vi.fn();
    playerStore.currentTrack = mkTrack('error-track');
    playerStore.currentTime = 0;
    playerStore.queue = [mkTrack('error-track')];
    playerStore.currentIndex = 0;
    setLyricFullscreen(false);
    mockApiGet.mockRejectedValue(new Error('lyric network failed'));
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    delete (Element.prototype as any).scrollIntoView;
    mockApiGet.mockReset();
    document.body.innerHTML = '';
    setLyricFullscreen(false);
  });

  it('shows a dedicated retry state instead of rendering the error as a lyric line', async () => {
    const w = mountLyric();
    await flushPromises();

    const error = w.get('[data-test="lyric-error"]');
    expect(error.text()).toContain('歌词');
    expect(w.findAll('[data-test^="lyric-line-"]').some((line) => line.text().includes('lyric network failed'))).toBe(false);

    mockLyricApi();
    await error.get('[data-test="lyric-retry"]').trigger('click');
    await flushPromises();
    expect(mockApiGet).toHaveBeenCalled();
  });
});

describe('LyricView empty state', () => {
  beforeEach(() => {
    mockLyricApi();
    playerStore.currentTrack = null;
    playerStore.queue = [];
    playerStore.currentIndex = -1;
    setLyricFullscreen(false);
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    mockApiGet.mockReset();
    document.body.innerHTML = '';
  });

  it('offers a route back to music instead of a passive no-track message', async () => {
    const w = mountLyric();
    await flushPromises();

    expect(w.get('[data-test="lyric-empty-state"]').text()).toContain('选择一首歌');
    await w.get('[data-test="lyric-empty-home"]').trigger('click');
    expect(w.emitted('navigate')).toEqual([['home']]);
  });

  it('offers search as a second empty-state action', async () => {
    const w = mountLyric();
    await flushPromises();
    await w.get('[data-test="lyric-empty-search"]').trigger('click');
    expect(w.emitted('navigate')).toEqual([['search']]);
  });
});

describe('LyricView transition contract', () => {
  beforeEach(() => {
    mockLyricApi();
    playerStore.currentTrack = null;
    playerStore.queue = [];
    playerStore.currentIndex = -1;
    setLyricFullscreen(false);
  });

  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    mockApiGet.mockReset();
    document.body.innerHTML = '';
    setLyricFullscreen(false);
  });

  it('exposes one element root for RouterView out-in transitions', async () => {
    const w = mountLyric();
    await nextTick();

    expect(w.vm.$.subTree.type).not.toBe(Fragment);
    expect(w.vm.$.subTree.el).toBeInstanceOf(HTMLElement);
  });
});

describe('LyricView fullscreen', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mockLyricApi();
    (Element.prototype as any).scrollIntoView = vi.fn();
    playerStore.currentTrack = mkTrack('h1');
    playerStore.currentTime = 0;
    playerStore.queue = [mkTrack('h1')];
    playerStore.currentIndex = 0;
    setLyricFullscreen(false);
  });
  afterEach(() => {
    wrapper?.unmount();
    wrapper = undefined;
    delete (Element.prototype as any).scrollIntoView;
    mockApiGet.mockReset();
    document.body.innerHTML = '';
    setLyricFullscreen(false);
  });

  it('double-clicking the cover area enters fullscreen', async () => {
    const w = mountLyric();
    await flushPromises();
    await w.find('[data-test="lyric-meta-column"]').trigger('dblclick');
    expect(lyricFullscreen.value).toBe(true);
  });

  it('pressing Esc exits fullscreen', async () => {
    setLyricFullscreen(true);
    mountLyric();
    await flushPromises();
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    expect(lyricFullscreen.value).toBe(false);
  });

  it('clicking a timed lyric line seeks via playerStore.seek to that line time', async () => {
    seekMock.mockClear();
    const w = mountLyric();
    await flushPromises();

    // Second line is [00:05.00] → 5 seconds
    await w.get('[data-test="lyric-line-1"]').trigger('click');
    await flushPromises();

    expect(seekMock).toHaveBeenCalled();
    expect(seekMock).toHaveBeenCalledWith(5);
  });

  it('fullscreen stage shows cover wash when motion is allowed', async () => {
    setLyricFullscreen(true);
    const w = mountLyric();
    await flushPromises();
    expect(w.find('[data-test="lyric-cover-wash"]').exists()).toBe(true);
  });

  it('resets lyricFullscreen to false on unmount', async () => {
    setLyricFullscreen(true);
    const w = mountLyric();
    await flushPromises();
    expect(lyricFullscreen.value).toBe(true);
    w.unmount();
    expect(lyricFullscreen.value).toBe(false);
  });

  it('clears cover size overrides when toggling fullscreen (CSS owns size)', async () => {
    const w = mountLyric();
    await flushPromises();
    gsapToMock.mockClear();

    setLyricFullscreen(true);
    await nextTick();
    await nextTick();

    expect(gsapToMock).toHaveBeenCalledWith(
      w.find('[data-test="lyric-cover"]').element,
      expect.objectContaining({ clearProps: 'width,height' }),
    );

    gsapToMock.mockClear();
    setLyricFullscreen(false);
    await nextTick();
    await nextTick();

    expect(gsapSetMock).toHaveBeenCalledWith(
      w.find('[data-test="lyric-cover"]').element,
      expect.objectContaining({ clearProps: 'width,height,opacity,transform' }),
    );
  });

  it('clears interrupted fullscreen animation styles when exiting', async () => {
    const w = mountLyric();
    await flushPromises();
    const stage = w.get('[data-test="aurora-lyric-stage"]').element;
    const cover = w.get('[data-test="lyric-cover"]').element;
    const wash = w.get('[data-test="lyric-cover-wash"]').element;
    gsapKillTweensOfMock.mockClear();
    gsapSetMock.mockClear();

    setLyricFullscreen(true);
    await nextTick();
    setLyricFullscreen(false);
    await nextTick();

    expect(gsapKillTweensOfMock).toHaveBeenCalledWith(stage);
    expect(gsapKillTweensOfMock).toHaveBeenCalledWith(cover);
    expect(gsapKillTweensOfMock).toHaveBeenCalledWith(wash);
    expect(gsapSetMock).toHaveBeenCalledWith(
      stage,
      expect.objectContaining({ clearProps: 'filter,opacity,transform' }),
    );
    expect(gsapSetMock).toHaveBeenCalledWith(
      wash,
      expect.objectContaining({ clearProps: 'opacity' }),
    );
  });
});
