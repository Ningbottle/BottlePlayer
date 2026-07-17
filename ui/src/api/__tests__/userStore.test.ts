import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

const mockApiGet = vi.fn();
const mockApiPost = vi.fn();
vi.mock('../backend', () => ({
  apiGet: (...args: any[]) => mockApiGet(...args),
  apiPost: (...args: any[]) => mockApiPost(...args),
}));

import { checkLoginStatus, logoutLocal, userStore } from '../userStore';
import { recentPlayedStore } from '../recentPlayedStore';
import type { Track } from '../normalizer';

function mkTrack(): Track {
  return {
    FileHash: 'recent-hash',
    SongName: 'Recent Song',
    SingerName: 'Recent Artist',
    Duration: 180,
  };
}

function resetUserStore() {
  userStore.isLoggedIn = false;
  userStore.userId = '';
  userStore.username = '未登录';
  userStore.avatar = '';
  userStore.vipLevel = 0;
  userStore.vipType = 0;
  userStore.isVip = false;
  userStore.vipEndDate = '';
  userStore.loading = false;
  userStore.claimMessage = '';
}

describe('userStore login refresh', () => {
  beforeEach(() => {
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    mockApiPost.mockResolvedValue({ status: 1 });
    resetUserStore();
    recentPlayedStore.reset();
  });

  // Restore spies even if an assertion throws mid-test, so a failure here
  // can't pollute later tests (e.g. a leaked console.warn spy).
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('keeps successful login state when VIP detail refresh fails', async () => {
    const warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return {
          status: 1,
          data: {
            userid: 42,
            nickname: 'Bottle',
            pic: 'http://img/avatar.png',
          },
        };
      }
      if (path === '/user/vip/detail') {
        throw new Error('vip detail timeout');
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await checkLoginStatus();

    expect(warnSpy).toHaveBeenCalledWith('VIP detail refresh failed; keeping login state', expect.any(Error));
    expect(userStore.isLoggedIn).toBe(true);
    expect(userStore.userId).toBe('42');
    expect(userStore.username).toBe('Bottle');
    expect(userStore.avatar).toBe('http://img/avatar.png');
    expect(userStore.isVip).toBe(false);
    expect(userStore.vipEndDate).toBe('');
    expect(userStore.loading).toBe(false);
  });

  it('clears device-local recent history on logout so it is not shown to the next account', () => {
    recentPlayedStore.recordRecentPlayed(mkTrack());
    expect(recentPlayedStore.entries.value).toHaveLength(1);

    logoutLocal();

    expect(recentPlayedStore.entries.value).toEqual([]);
  });
});
