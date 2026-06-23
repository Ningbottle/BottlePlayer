import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import HomeView from '../HomeView.vue';

const mockApiGet = vi.fn();
vi.mock('../../api/backend', () => ({
  apiGet: (...args: any[]) => mockApiGet(...args),
}));

vi.mock('../../api/playerStore', () => ({
  playTrack: vi.fn(),
  playAll: vi.fn(),
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
});
