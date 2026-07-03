import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import HomeView from '../HomeView.vue';
import { playPersonalFm } from '../../api/playerStore';

const mockApiGet = vi.fn();
vi.mock('../../api/backend', () => ({
  apiGet: (...args: any[]) => mockApiGet(...args),
}));

vi.mock('../../api/playerStore', () => ({
  playTrack: vi.fn(),
  playAll: vi.fn(),
  playPersonalFm: vi.fn(),
}));

describe('HomeView sections', () => {
  beforeEach(() => {
    mockApiGet.mockReset();
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

  it('labels everyday recommendations clearly and plays them from the hero CTA', async () => {
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

    expect(playPersonalFm).toHaveBeenCalledWith(
      expect.arrayContaining([
        expect.objectContaining({ FileHash: 'daily-1', SongName: '不只是场梦' }),
        expect.objectContaining({ FileHash: 'daily-2', SongName: '无聊' }),
      ]),
      0,
    );
  });
});
