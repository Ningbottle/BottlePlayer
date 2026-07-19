import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import HomeView from '../HomeView.vue';
import { playAll, playPersonalFm, playTrack, playerStore } from '../../api/playerStore';
import { __resetHomeFeedForTest } from '../../api/homeFeedStore';

const mockApiGet = vi.fn();
vi.mock('../../api/backend', () => ({
  apiGet: (...args: any[]) => mockApiGet(...args),
}));

vi.mock('../../api/playerStore', () => ({
  playTrack: vi.fn(),
  playAll: vi.fn(),
  playPersonalFm: vi.fn(),
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

describe('HomeView sections', () => {
  beforeEach(() => {
    __resetHomeFeedForTest();
    vi.clearAllMocks();
    mockApiGet.mockReset();
    playerStore.currentTrack = null;
    playerStore.isPlaying = false;
    playerStore.queue = [];
    playerStore.currentIndex = -1;
  });

  it('renders a section even when another section fails', async () => {
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/everyday/recommend') {
        return Promise.reject(new Error('network down'));
      }
      return Promise.resolve({
        status: 1,
        data: {
          data: {
            info: [
              { specialid: 1, specialname: 'Test PL', nickname: 'Tester', imgurl: '', playcount: 0 },
            ],
          },
        },
      });
    });

    const wrapper = mount(HomeView);
    await flushPromises();

    expect(wrapper.text()).toContain('加载失败');
    expect(wrapper.text()).toContain('Test PL');
  });

  it('labels everyday recommendations clearly and plays them via playPersonalFm (continuous reco session)', async () => {
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/everyday/recommend') {
        return Promise.resolve({
          status: 1,
          data: {
            data: {
              song_list: [
                { FileHash: 'daily-1', SongName: '不只是场梦', SingerName: '李玖哲', Duration: 212 },
                { FileHash: 'daily-2', SongName: '无聊', SingerName: '林俊杰', Duration: 251 },
              ],
            },
          },
        });
      }

      return Promise.resolve({
        status: 1,
        data: {
          data: {
            info: [
              { specialid: 1, specialname: 'Test PL', nickname: 'Tester', imgurl: '', playcount: 0 },
            ],
          },
        },
      });
    });

    const wrapper = mount(HomeView);
    await flushPromises();

    expect(wrapper.text()).toContain('每日推荐');
    expect(wrapper.text()).not.toContain('每周飙升');

    await wrapper.get('button.play-cta').trigger('click');

    // Daily seeds a personalFm session so the queue can append /personal/fm
    // as the listener advances (not a finite list-loop of the snapshot).
    expect(playPersonalFm).toHaveBeenCalledWith(
      expect.arrayContaining([
        expect.objectContaining({ FileHash: 'daily-1', SongName: '不只是场梦' }),
        expect.objectContaining({ FileHash: 'daily-2', SongName: '无聊' }),
      ]),
      0,
    );
    expect(playAll).not.toHaveBeenCalled();
  });

  it('plays a daily-rail row via playPersonalFm (continuous reco, not a 5-track loop)', async () => {
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/everyday/recommend') {
        return Promise.resolve({
          status: 1,
          data: {
            data: {
              song_list: [
                { FileHash: 'daily-1', SongName: '不只是场梦', SingerName: '李玖哲', Duration: 212 },
                { FileHash: 'daily-2', SongName: '无聊', SingerName: '李荣浩', Duration: 253 },
              ],
            },
          },
        });
      }
      return Promise.resolve({ status: 1, data: { data: { info: [] } } });
    });
    // Playback queue is separate; rail must not play this as a single-track path.
    playerStore.queue = [
      { FileHash: 'queued-extra', SongName: '队列追加', SingerName: '测试', Duration: 180 },
    ];

    const wrapper = mount(HomeView);
    await flushPromises();

    await wrapper.get('[data-test="queue-track-daily-1"]').trigger('click');

    expect(playPersonalFm).toHaveBeenCalledWith(
      expect.arrayContaining([
        expect.objectContaining({ FileHash: 'daily-1' }),
        expect.objectContaining({ FileHash: 'daily-2' }),
      ]),
      0,
    );
    expect(playAll).not.toHaveBeenCalled();
    expect(playTrack).not.toHaveBeenCalled();
  });

  it('refreshing daily recommendations does not touch the playback queue or invoke playback', async () => {
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
          data: { info: [{ specialid: 1, specialname: 'PL', nickname: 'T', imgurl: '', playcount: 0 }] },
        },
      });
    });

    const queueBefore = playerStore.queue;
    const wrapper = mount(HomeView);
    await flushPromises();

    // Trigger a refresh of the daily snapshot.
    await wrapper.get('[data-test="refresh"]').trigger('click');
    await flushPromises();

    // Daily Picks are a refreshable snapshot: refreshing must not mutate the
    // current playback queue nor start any playback.
    expect(playerStore.queue).toBe(queueBefore);
    expect(playAll).not.toHaveBeenCalled();
    expect(playPersonalFm).not.toHaveBeenCalled();
    expect(playTrack).not.toHaveBeenCalled();
  });

  it('does not request the home feed again after remounting', async () => {
    mockApiGet.mockImplementation((path: string) => {
      if (path === '/everyday/recommend') {
        return Promise.resolve({
          status: 1,
          data: { data: { song_list: [{ FileHash: 'daily-1', SongName: 'Daily', SingerName: 'Artist', Duration: 180 }] } },
        });
      }

      return Promise.resolve({
        status: 1,
        data: { data: { info: [{ specialid: 1, specialname: 'Test PL', nickname: 'Tester', imgurl: '', playcount: 0 }] } },
      });
    });

    const first = mount(HomeView);
    await flushPromises();
    first.unmount();

    mount(HomeView);
    await flushPromises();

    expect(mockApiGet).toHaveBeenCalledTimes(3);
  });
});
