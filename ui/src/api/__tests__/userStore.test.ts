import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

const mockApiGet = vi.fn();
const mockApiPost = vi.fn();
vi.mock('../../platform/tauri/nativeClient', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../../platform/tauri/nativeClient')>();
  return {
    ...actual,
    apiGet: (...args: any[]) => mockApiGet(...args),
    apiPost: (...args: any[]) => mockApiPost(...args),
  };
});

import {
  checkLoginStatus,
  claimVip,
  ensureVipDeviceReady,
  formatVipClaimFailure,
  logoutLocal,
  userStore,
} from '../userStore';
import { recentPlayedStore } from '../../playback/data/recentPlayedStore';
import type { Track } from '../../shared/music/track';

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
  userStore.deviceReady = false;
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

const VALID_DFID = 'abcdefghijklmnopqrstuvwx';

function mockReadyDevice() {
  mockApiPost.mockResolvedValue({
    status: 1,
    data: { registered: true, dfid: VALID_DFID },
  });
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

  it('refreshes session after device registration without auto-claiming the daily VIP', async () => {
    let resolveGate!: (value: unknown) => void;
    const gate = new Promise<unknown>((resolve) => {
      resolveGate = resolve;
    });
    mockApiPost.mockImplementation((path: string) => {
      expect(path).toBe('/register/dev');
      return gate;
    });
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return { status: 1, data: { userid: 42, nickname: 'Bottle' } };
      }
      if (path === '/user/vip/detail') {
        return { status: 1, data: {} };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    const checking = checkLoginStatus();
    await vi.waitFor(() => expect(mockApiPost).toHaveBeenCalledWith('/register/dev'));
    await Promise.resolve();
    await Promise.resolve();
    expect(mockApiGet).not.toHaveBeenCalledWith('/youth/listen/song');

    resolveGate({ status: 1, data: { registered: true, dfid: VALID_DFID } });
    await checking;
    // 静默自动领取已移除：VIP 应由用户手动领取，登录刷新不得调用领取接口，
    // 否则 VIP 状态会永不过期（用户反馈）。
    expect(mockApiGet).not.toHaveBeenCalledWith('/youth/listen/song');
  });

  it('shows an upstream error code instead of guessing that the official app is required', async () => {
    userStore.isLoggedIn = true;
    userStore.userId = '42';
    mockReadyDevice();
    mockApiGet.mockResolvedValue({
      status: 0,
      error_code: 51002,
      error_msg: '',
    });

    await claimVip();

    expect(userStore.claimMessage).toContain('51002');
    expect(userStore.claimMessage).not.toContain('需要在酷狗官方 App 内领取');
  });

  it('treats the same-day 130012 response as an idempotent already-claimed result', () => {
    expect(formatVipClaimFailure({
      status: 0,
      error_code: 130012,
      error_msg: '',
    })).toBe('今天已经领过了');
  });

  it('clears device-local recent history on logout so it is not shown to the next account', () => {
    recentPlayedStore.recordRecentPlayed(mkTrack());
    expect(recentPlayedStore.entries.value).toHaveLength(1);

    logoutLocal();

    expect(recentPlayedStore.entries.value).toEqual([]);
  });
});

describe('ensureVipDeviceReady', () => {
  beforeEach(() => {
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    resetUserStore();
  });

  it('treats registered=true and a non-dash dfid as ready', async () => {
    mockReadyDevice();
    const result = await ensureVipDeviceReady();
    expect(result.ok).toBe(true);
    expect(userStore.deviceReady).toBe(true);
    expect(mockApiPost).toHaveBeenCalledWith('/register/dev');
  });

  it('rejects registered=false with dfid="-" as device_registration_failed', async () => {
    mockApiPost.mockResolvedValue({
      status: 1,
      data: { registered: false, dfid: '-' },
    });
    const result = await ensureVipDeviceReady();
    expect(result.ok).toBe(false);
    expect(result.error).toBe('device_registration_failed');
    expect(userStore.deviceReady).toBe(false);
  });

  it('rejects registered=true with dfid="-"', async () => {
    mockApiPost.mockResolvedValue({
      status: 1,
      data: { registered: true, dfid: '-' },
    });
    const result = await ensureVipDeviceReady();
    expect(result.ok).toBe(false);
    expect(result.error).toBe('device_registration_failed');
    expect(userStore.deviceReady).toBe(false);
  });

  it('keeps status=0 as a distinguishable device registration failure', async () => {
    mockApiPost.mockResolvedValue({
      status: 0,
      error_code: 'device_registration_failed',
      error: 'device registration failed',
    });
    const result = await ensureVipDeviceReady();
    expect(result.ok).toBe(false);
    expect(result.error).toBe('device_registration_failed');
  });

  it('keeps request_timeout distinguishable from device registration failure', async () => {
    mockApiPost.mockRejectedValue(new Error('request_timeout'));
    const result = await ensureVipDeviceReady();
    expect(result.ok).toBe(false);
    expect(result.error).toBe('request_timeout');
    expect(userStore.deviceReady).toBe(false);
  });

  it('keeps circuit_open distinguishable from device registration failure', async () => {
    mockApiPost.mockRejectedValue(new Error('circuit_open'));
    const result = await ensureVipDeviceReady();
    expect(result.ok).toBe(false);
    expect(result.error).toBe('circuit_open');
    expect(userStore.deviceReady).toBe(false);
  });
});

describe('claimVip snapshot overlay', () => {
  beforeEach(() => {
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    resetUserStore();
    userStore.isLoggedIn = true;
    userStore.userId = '42';
    mockReadyDevice();
  });

  it('does not send listen when device registration is not ready', async () => {
    mockApiPost.mockResolvedValue({
      status: 1,
      data: { registered: false, dfid: '-' },
    });
    await claimVip();
    expect(mockApiGet).not.toHaveBeenCalledWith('/youth/listen/song');
    expect(userStore.claimMessage).toContain('设备注册失败');
    expect(userStore.isVip).toBe(false);
  });

  it('keeps optimistic VIP when listen succeeds and detail is non-authoritative', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/listen/song') {
        return { status: 1, data: '' };
      }
      if (path === '/user/vip/detail') {
        return {
          status: 0,
          authoritative: false,
          error_code: 51002,
          error: 'activity rejected',
          data: null,
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVip();

    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('');
    expect(userStore.claimMessage).toContain('领取上报成功');
    expect(userStore.claimMessage).toContain('会员状态查询失败');
  });

  it('does not immediately downgrade optimistic VIP when authoritative detail is still false', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/listen/song') {
        return { status: 1, data: '' };
      }
      if (path === '/user/vip/detail') {
        return { status: 1, authoritative: true, data: { is_vip: 0, vip_type: 0 } };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVip();

    expect(userStore.isVip).toBe(true);
    expect(userStore.claimMessage).toContain('权益状态同步中');
  });

  it('applies level and expiry when authoritative detail confirms VIP', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/listen/song') {
        return { status: 1, data: '' };
      }
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_type: 1, vip_end_time: '2026-12-31 23:59:59' },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVip();

    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
    expect(userStore.claimMessage).toContain('已激活每日 VIP');
  });

  it('lets an explicit login refresh downgrade after a delayed authoritative false', async () => {
    userStore.isVip = true;
    userStore.vipEndDate = '';
    mockReadyDevice();
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return { status: 1, data: { userid: 42, nickname: 'Bottle' } };
      }
      if (path === '/user/vip/detail') {
        return { status: 1, authoritative: true, data: { is_vip: 0, vip_type: 0 } };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await checkLoginStatus();

    expect(userStore.isLoggedIn).toBe(true);
    expect(userStore.isVip).toBe(false);
  });

  it('does not treat 130012 as VIP unless authoritative detail confirms it', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/listen/song') {
        return { status: 0, error_code: 130012, error_msg: '' };
      }
      if (path === '/user/vip/detail') {
        return { status: 0, authoritative: false, data: null };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVip();

    expect(userStore.claimMessage).toBe('今天已经领过了');
    expect(userStore.isVip).toBe(false);
  });

  it('updates VIP from 130012 only when authoritative detail is VIP', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/listen/song') {
        return { status: 0, error_code: 130012, error_msg: '' };
      }
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_end_time: '2026-12-31 23:59:59' },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVip();

    expect(userStore.claimMessage).toBe('今天已经领过了');
    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
  });

  it('does not classify timeout as 51002', async () => {
    mockApiGet.mockRejectedValue(new Error('request_timeout'));
    await claimVip();
    expect(userStore.claimMessage).toContain('超时');
    expect(userStore.claimMessage).not.toContain('51002');
    expect(userStore.isVip).toBe(false);
  });

  it('does not classify circuit_open as 51002', async () => {
    mockApiGet.mockRejectedValue(new Error('circuit_open'));
    await claimVip();
    expect(userStore.claimMessage).toContain('繁忙');
    expect(userStore.claimMessage).not.toContain('51002');
    expect(userStore.isVip).toBe(false);
  });

  it('keeps prior VIP when login refresh gets a non-authoritative detail failure', async () => {
    userStore.isVip = true;
    userStore.vipEndDate = '2026-12-31 23:59:59';
    mockReadyDevice();
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return { status: 1, data: { userid: 42, nickname: 'Bottle' } };
      }
      if (path === '/user/vip/detail') {
        return { status: 0, authoritative: false, data: null, error_code: 'native_vip_detail_failed' };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await checkLoginStatus();

    expect(userStore.isLoggedIn).toBe(true);
    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
  });
});
