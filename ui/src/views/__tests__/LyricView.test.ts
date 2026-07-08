import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises, type VueWrapper } from '@vue/test-utils';
import { nextTick } from 'vue';

vi.mock('@tauri-apps/api/core', () => ({
  invoke: vi.fn().mockResolvedValue(undefined),
}));

vi.mock('../../api/playerStore', async () => {
  const actual = await vi.importActual<typeof import('../../api/playerStore')>('../../api/playerStore');
  return { ...actual, playTrack: vi.fn() };
});

const mockApiGet = vi.fn();
vi.mock('../../api/backend', () => ({
  apiGet: (...args: any[]) => mockApiGet(...args),
}));

import LyricView from '../LyricView.vue';
import { playerStore } from '../../api/playerStore';
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

  it('shows a return-to-current button when auto-follow is suspended by wheel scroll', async () => {
    const wrapper = mountLyric();
    await flushPromises(); // lyrics load

    // Initially auto-following 鈥?no return-to-current button.
    expect(wrapper.find('[data-test="return-to-current"]').exists()).toBe(false);

    // User scrolls up via wheel 鈫?suspends auto-follow.
    await wrapper.find('.lyric-scroll').trigger('wheel');
    await nextTick();

    expect(wrapper.find('[data-test="return-to-current"]').exists()).toBe(true);
  });

  it('resumes auto-follow and scrolls to the active line when the return-to-current button is clicked', async () => {
    const wrapper = mountLyric();
    await flushPromises();
    await wrapper.find('.lyric-scroll').trigger('wheel'); // suspend
    expect(wrapper.find('[data-test="return-to-current"]').exists()).toBe(true);

    scrollSpy.mockClear();
    await wrapper.find('[data-test="return-to-current"]').trigger('click');

    expect(wrapper.find('[data-test="return-to-current"]').exists()).toBe(false);
    expect(scrollSpy).toHaveBeenCalled();
  });

  it('does not auto-scroll on activeIndex change while auto-follow is suspended', async () => {
    const wrapper = mountLyric();
    await flushPromises(); // lyrics load, initial auto-follow scroll
    scrollSpy.mockClear();

    await wrapper.find('.lyric-scroll').trigger('wheel'); // suspend

    // playback advances 鈫?activeIndex changes; suspended 鈫?no scroll
    playerStore.currentTime = 6;
    await nextTick();

    expect(scrollSpy).not.toHaveBeenCalled();
  });

  it('resets auto-follow on track change so the new track follows from the start', async () => {
    const wrapper = mountLyric();
    await flushPromises();
    await wrapper.find('.lyric-scroll').trigger('wheel'); // suspend
    expect(wrapper.find('[data-test="return-to-current"]').exists()).toBe(true);

    playerStore.currentTrack = mkTrack('h2');
    await flushPromises(); // loadLyrics for new track + resetForTrack

    expect(wrapper.find('[data-test="return-to-current"]').exists()).toBe(false);
  });
});

describe('LyricView fullscreen', () => {
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

  it('has a fullscreen toggle button that sets lyricFullscreen to true', async () => {
    const w = mountLyric();
    await flushPromises();
    const btn = w.find('[data-test="lyric-fullscreen-toggle"]');
    await btn.trigger('click');
    expect(lyricFullscreen.value).toBe(true);
  });

  it('double-clicking the cover area enters fullscreen', async () => {
    const w = mountLyric();
    await flushPromises();
    await w.find('.lyric-meta').trigger('dblclick');
    expect(lyricFullscreen.value).toBe(true);
  });

  it('pressing Esc exits fullscreen', async () => {
    setLyricFullscreen(true);
    mountLyric();
    await flushPromises();
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    expect(lyricFullscreen.value).toBe(false);
  });
});
