import { describe, it, expect, vi, beforeEach } from 'vitest';
import { KeepAlive, defineComponent, h, nextTick, ref } from 'vue';
import { mount, flushPromises } from '@vue/test-utils';
import HomeView from '../../HomeView.vue';
import { __resetHomeFeedForTest } from '../../../api/homeFeedStore';
import {
  __resetHomeEnterSessionForTest,
  nextHomeEnterMode,
} from '../../../api/homeEnterSession';
import { __resetForTest as __resetThemeForTest, useThemeStore } from '../../../api/themeStore';

const mockApiGet = vi.fn();
vi.mock('../../../api/backend', () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

vi.mock('../../../api/playerStore', () => ({
  playTrack: vi.fn(),
  playAll: vi.fn(),
  playPersonalFm: vi.fn(),
  clearQueue: vi.fn(),
  playerStore: {
    currentTrack: null,
    isPlaying: false,
    currentTime: 0,
    duration: 0,
    volume: 0.7,
    queue: [],
    currentIndex: -1,
  },
}));

vi.mock('../../../api/motion', () => ({
  animateElement: vi.fn(() => ({ kill: () => {} })),
  animateStagger: vi.fn(() => ({ kill: () => {} })),
  startAmbientMotion: vi.fn(() => ({ kill: () => {} })),
  startVinylSpin: vi.fn(() => ({ kill: () => {}, setPlaying: () => {} })),
  isReducedMotion: vi.fn(() => true),
}));

function stubFeedOk() {
  mockApiGet.mockImplementation((path: string) => {
    if (path === '/everyday/recommend') {
      return Promise.resolve({
        status: 1,
        data: {
          data: {
            song_list: [
              { FileHash: 'daily-1', SongName: 'Daily', SingerName: 'Artist', Duration: 180 },
            ],
          },
        },
      });
    }
    return Promise.resolve({
      status: 1,
      data: {
        data: {
          info: [{ specialid: 1, specialname: 'PL', nickname: 'T', imgurl: '', playcount: 0 }],
        },
      },
    });
  });
}

describe('HomeView enterMode wiring (KeepAlive activations)', () => {
  beforeEach(() => {
    __resetHomeFeedForTest();
    __resetHomeEnterSessionForTest();
    __resetThemeForTest();
    useThemeStore().setSkin('aurora');
    mockApiGet.mockReset();
    stubFeedOk();
  });

  it('first activation is cold; second is return — never double cold', async () => {
    const current = ref<'home' | 'other'>('home');

    const RealHost = defineComponent({
      setup() {
        return () =>
          h(
            'div',
            { class: 'scroll' },
            h(KeepAlive, null, {
              default: () =>
                current.value === 'home'
                  ? h(HomeView, { key: 'home' })
                  : h('div', { key: 'other', 'data-test': 'other-view' }, 'other'),
            }),
          );
      },
    });

    const wrapper = mount(RealHost);
    await flushPromises();
    await nextTick();

    const aurora = wrapper.findComponent({ name: 'AuroraHome' });
    expect(aurora.exists()).toBe(true);
    expect(aurora.props('enterMode')).toBe('cold');
    const firstNonce = aurora.props('enterNonce') as number;
    expect(firstNonce).toBeGreaterThan(0);

    current.value = 'other';
    await nextTick();
    await flushPromises();

    current.value = 'home';
    await nextTick();
    await flushPromises();

    const aurora2 = wrapper.findComponent({ name: 'AuroraHome' });
    expect(aurora2.exists()).toBe(true);
    expect(aurora2.props('enterMode')).toBe('return');
    expect(aurora2.props('enterNonce')).toBeGreaterThan(firstNonce);

    // Session itself never re-issues cold after first activation
    expect(nextHomeEnterMode()).toBe('return');
  });

  it('does not advance home enter session when skin is newsprint', async () => {
    useThemeStore().setSkin('newsprint');
    const current = ref(true);

    const RealHost = defineComponent({
      setup() {
        return () =>
          h(KeepAlive, null, {
            default: () => (current.value ? h(HomeView, { key: 'home' }) : h('div', { key: 'x' })),
          });
      },
    });

    mount(RealHost);
    await flushPromises();
    await nextTick();

    // Session unused for newsprint — first next call is still cold
    expect(nextHomeEnterMode()).toBe('cold');
  });
});
