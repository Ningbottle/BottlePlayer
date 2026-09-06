import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

const mockApiGet = vi.fn();
const mockApiPost = vi.fn();
vi.mock('../../../platform/tauri/nativeClient', async (importOriginal) => {
  const actual = await importOriginal<typeof import('../../../platform/tauri/nativeClient')>();
  return {
    ...actual,
    apiGet: (...args: any[]) => mockApiGet(...args),
    apiPost: (...args: any[]) => mockApiPost(...args),
  };
});

import {
  cancelClaim,
  checkLoginStatus,
  claimVip,
  claimVipViaRoute,
  ensureVipDeviceReady,
  formatVipClaimFailure,
  logoutLocal,
  userStore,
} from '../userStore';
import {
  configureAccountEffects,
  __resetAccountEffectsForTests,
  type AccountEffects,
} from '../accountEffects';

function makeFakeEffects() {
  return {
    onAccountReady: vi.fn(),
    onAccountCleared: vi.fn(),
    onLocalLogout: vi.fn(),
  } satisfies AccountEffects & Record<string, ReturnType<typeof vi.fn>>;
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
  userStore.vipStatus = 'unknown';
  userStore.vipEndDate = '';
  userStore.loading = false;
  userStore.claimMessage = '';
  userStore.vipClaimPending = false;
  userStore.claimStage = '';
  userStore.claimCancelRequested = false;
}

const VALID_DFID = 'abcdefghijklmnopqrstuvwx';

function mockReadyDevice() {
  mockApiPost.mockResolvedValue({
    status: 1,
    data: { registered: true, dfid: VALID_DFID },
  });
}

describe('userStore login refresh', () => {
  let effects: ReturnType<typeof makeFakeEffects>;

  beforeEach(() => {
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    mockApiPost.mockResolvedValue({ status: 1 });
    resetUserStore();
    __resetAccountEffectsForTests();
    effects = makeFakeEffects();
    configureAccountEffects(effects);
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

  it('notifies account ready exactly once after device registration succeeds', async () => {
    const warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    mockReadyDevice();
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return { status: 1, data: { userid: 42, nickname: 'Bottle' } };
      }
      if (path === '/user/vip/detail') {
        return { status: 1, data: {} };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await checkLoginStatus();

    expect(effects.onAccountReady).toHaveBeenCalledTimes(1);
    expect(effects.onAccountReady).toHaveBeenCalledWith('42');
    expect(warnSpy).not.toHaveBeenCalledWith('Device upgrade failed', expect.anything());
  });

  it('does not notify account ready when device registration fails', async () => {
    const warnSpy = vi.spyOn(console, 'warn').mockImplementation(() => {});
    mockApiPost.mockResolvedValue({ status: 1, data: { registered: false, dfid: '-' } });
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return { status: 1, data: { userid: 42, nickname: 'Bottle' } };
      }
      if (path === '/user/vip/detail') {
        return { status: 1, data: {} };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await checkLoginStatus();

    expect(effects.onAccountReady).not.toHaveBeenCalled();
    expect(userStore.isLoggedIn).toBe(true);
    warnSpy.mockRestore();
  });

  it('notifies account cleared when the session is invalid', async () => {
    mockApiGet.mockResolvedValue({ status: 0 });
    await checkLoginStatus();
    expect(effects.onAccountCleared).toHaveBeenCalledTimes(1);
    expect(effects.onAccountReady).not.toHaveBeenCalled();
  });

  it('notifies account cleared when the login check throws', async () => {
    const errSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
    mockApiGet.mockRejectedValue(new Error('offline'));
    await checkLoginStatus();
    expect(effects.onAccountCleared).toHaveBeenCalledTimes(1);
    errSpy.mockRestore();
  });

  it('claimVip still notifies ready only after registration succeeds', async () => {
    userStore.isLoggedIn = true;
    userStore.userId = '42';
    mockReadyDevice();
    mockApiGet.mockResolvedValue({ status: 1 });

    // recheckAttempts: 0 — this test pins the notify-ready ordering, not the
    // confirm/recheck flow (the mock has no authoritative detail data, so
    // the confirm would schedule rechecks).
    await claimVip({ adLoopMax: 1, adIntervalMs: 0, recheckAttempts: 0 });

    expect(effects.onAccountReady).toHaveBeenCalledTimes(1);
    expect(effects.onAccountReady).toHaveBeenCalledWith('42');
    expect(mockApiPost).toHaveBeenCalledWith('/register/dev');
  });

  it('logoutLocal orders cleared before localLogout', () => {
    logoutLocal();

    expect(effects.onAccountCleared).toHaveBeenCalledTimes(1);
    expect(effects.onLocalLogout).toHaveBeenCalledTimes(1);
    const clearedOrder = effects.onAccountCleared.mock.invocationCallOrder[0];
    const logoutOrder = effects.onLocalLogout.mock.invocationCallOrder[0];
    expect(clearedOrder).toBeLessThan(logoutOrder);
  });
});

describe('ensureVipDeviceReady', () => {
  beforeEach(() => {
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    resetUserStore();
    __resetAccountEffectsForTests();
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
    __resetAccountEffectsForTests();
    configureAccountEffects({ onAccountReady: vi.fn(), onAccountCleared: vi.fn(), onLocalLogout: vi.fn() });
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

  it('keeps prior state (never optimistic VIP) and reports 未确认 when detail is non-authoritative', async () => {
    // Stage 5b（计划 §5.2 例外改写）：领取受理 ≠ 权益有效。通道成功只进入
    // "待确认"；非权威查询失败保留 prior（这里 prior 是未登录 VIP 的
    // isVip=false），recheckAttempts=0 时立即转"未确认"——不再乐观置 true。
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad' || path === '/youth/day/vip') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
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

    await claimVip({ adLoopMax: 1, adIntervalMs: 0, recheckAttempts: 0 });

    expect(userStore.isVip).toBe(false);
    expect(userStore.vipClaimPending).toBe(false);
    expect(userStore.claimMessage).toContain('未确认');
  });

  it('reports 未生效 and never shows VIP when authoritative detail says no after claim', async () => {
    // Stage 5b（计划 §5.2 例外改写，替换旧的"乐观 VIP + 权益状态同步中"悬挂）：
    // 权威接口明确返回无权益 → isVip 必须为 false，消息如实说明"未生效"
    // （绝不保留乐观 true，也绝不说"已过期"——那是 5a expired 专属表述）。
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad' || path === '/youth/day/vip') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/listen/song') {
        return { status: 1, data: '' };
      }
      if (path === '/user/vip/detail') {
        return { status: 1, authoritative: true, data: { is_vip: 0, vip_type: 0 } };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVip({ adLoopMax: 1, adIntervalMs: 0, recheckAttempts: 0 });

    expect(userStore.isVip).toBe(false);
    expect(userStore.vipClaimPending).toBe(false);
    expect(userStore.claimMessage).toContain('未生效');
    expect(userStore.claimMessage).not.toContain('已过期');
    expect(userStore.claimMessage).not.toContain('权益状态同步中');
  });

  it('applies level and expiry when authoritative detail confirms VIP', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad' || path === '/youth/day/vip') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
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

    await claimVip({ adLoopMax: 1, adIntervalMs: 0 });

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

  it('bounded recheck promotes to 已激活 when a later authoritative confirm succeeds', async () => {
    // Stage 5b: 首次确认非权威 → 保持待确认 → 有限重查；重查拿到权威
    // 生效结论 → 升级为已激活（pending 清除，到期时间落库）。
    let detailCalls = 0;
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad' || path === '/youth/day/vip') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/listen/song') {
        return { status: 1, data: '' };
      }
      if (path === '/user/vip/detail') {
        detailCalls += 1;
        if (detailCalls === 1) {
          return { status: 0, authoritative: false, error_code: 51002, data: null };
        }
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_end_time: '2026-12-31 23:59:59' },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVip({ adLoopMax: 1, adIntervalMs: 0, recheckAttempts: 2, recheckIntervalMs: 5 });

    expect(detailCalls).toBe(2);
    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
    expect(userStore.vipClaimPending).toBe(false);
    expect(userStore.claimMessage).toContain('已激活每日 VIP');
  });

  it('a landed authoritative conclusion is reported even when cancel was requested during the await', async () => {
    // 批次 C 审查 W2：tryConfirmOnce 已把权威"生效"结论写入 store 后，
    // 取消标志不得把消息误报成"已取消领取"（否则角标已激活、消息已取消，
    // 自相矛盾）。判别：旧实现（结论前查取消）在此必红。
    let releaseDetail: () => void = () => {};
    const detailGate = new Promise<void>((resolve) => {
      releaseDetail = resolve;
    });
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad' || path === '/youth/day/vip') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/listen/song') {
        return { status: 1, data: '' };
      }
      if (path === '/user/vip/detail') {
        await detailGate;
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_end_time: '2026-12-31 23:59:59' },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    const run = claimVip({ adLoopMax: 1, adIntervalMs: 0, recheckAttempts: 0 });
    await vi.waitFor(() => expect(userStore.claimStage).toBe('confirm'));
    cancelClaim(); // 在确认 await 期间请求取消
    releaseDetail();
    await run;

    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
    expect(userStore.claimMessage).toContain('已激活每日 VIP');
    expect(userStore.claimMessage).not.toBe('已取消领取');
    expect(userStore.vipClaimPending).toBe(false);
  });

  it('logoutLocal stops an in-flight claim: no FFI after logout, store stays clean', async () => {
    // 批次 C 审查 W1：确认重查期间登出 —— 重查循环必须在下一边界停止，
    // 登出后不得再发起 fetchVipDetail、不得把 VIP 状态写回已登出的 store。
    let releaseDetail: () => void = () => {};
    const detailGate = new Promise<void>((resolve) => {
      releaseDetail = resolve;
    });
    let detailCalls = 0;
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad' || path === '/youth/day/vip') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/listen/song') {
        return { status: 1, data: '' };
      }
      if (path === '/user/vip/detail') {
        detailCalls += 1;
        await detailGate;
        return { status: 0, authoritative: false, error_code: 51002, data: null };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    const run = claimVip({ adLoopMax: 1, adIntervalMs: 0, recheckAttempts: 3, recheckIntervalMs: 50 });
    await vi.waitFor(() => expect(userStore.vipClaimPending).toBe(true));

    logoutLocal(); // W1：取消标志置位 + 会话拆除
    releaseDetail();
    await run;

    expect(detailCalls).toBe(1); // 登出后不再发起新的权威查询
    expect(userStore.isLoggedIn).toBe(false);
    expect(userStore.loading).toBe(false);
    expect(userStore.claimStage).toBe('');
    expect(userStore.vipClaimPending).toBe(false);
    expect(userStore.isVip).toBe(false);
  });

  it('recheck exhaustion reports 未确认 without marking VIP, with an upper-bound message', async () => {
    // Stage 5b: 重查耗尽 → 转"未确认"（pending=false），绝不标成有效。
    // W5（5c 完成标准）：确认中消息带整体最长等待上界（250ms×2 → 文案"1 秒"）。
    let detailCalls = 0;
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad' || path === '/youth/day/vip') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/listen/song') {
        return { status: 1, data: '' };
      }
      if (path === '/user/vip/detail') {
        detailCalls += 1;
        return { status: 0, authoritative: false, error_code: 51002, data: null };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    const run = claimVip({ adLoopMax: 1, adIntervalMs: 0, recheckAttempts: 2, recheckIntervalMs: 250 });
    await vi.waitFor(() => expect(userStore.claimMessage).toContain('最长约需'));
    await run;

    expect(detailCalls).toBe(3); // 首次确认 + 2 次重查
    expect(userStore.isVip).toBe(false);
    expect(userStore.vipClaimPending).toBe(false);
    expect(userStore.claimMessage).toContain('未确认');
  });

  it('cancelClaim during the ad countdown stops the next round and resets loading', async () => {
    // Stage 5c RED/GREEN: 取消在倒计时边界生效 —— 不再发起下一轮广告上报，
    // loading 复位，消息如实说明"已取消"。
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') {
        return { status: 1, data: '' };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    const run = claimVip({ adLoopMax: 3, adIntervalMs: 3000, recheckAttempts: 0 });
    await vi.waitFor(() => expect(userStore.claimMessage).toContain('秒后下一轮'));
    cancelClaim();
    await run;

    const adCalls = mockApiGet.mock.calls.filter((c: any[]) => c[0] === '/youth/vip/ad');
    expect(adCalls).toHaveLength(1);
    expect(userStore.loading).toBe(false);
    expect(userStore.claimStage).toBe('');
    expect(userStore.claimMessage).toBe('已取消领取');
  });

  it('exposes the claim stage while running and clears it when done', async () => {
    // 用未 resolve 的 ad gate 把流程钉在 'ad' 阶段（整个 claim 在毫秒级
    // 完成，mid-flight 状态只能用可控 gate 确定性地观察）。
    let releaseAd: () => void = () => {};
    const adGate = new Promise<void>((resolve) => {
      releaseAd = resolve;
    });
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') {
        await adGate;
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/day/vip') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/listen/song') {
        return { status: 0, error_code: 130012, error_msg: '' };
      }
      if (path === '/youth/day/vip/upgrade') {
        return { status: 0, error_code: 304001, error_msg: '' };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    const run = claimVip({ adLoopMax: 1, adIntervalMs: 0, recheckAttempts: 0 });
    await vi.waitFor(() => expect(userStore.claimStage).toBe('ad'));
    expect(userStore.loading).toBe(true);
    releaseAd();
    await run;
    expect(userStore.claimStage).toBe('');
    expect(userStore.loading).toBe(false);
  });

  it('stops the ad loop at max rounds and reports success once', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') {
        return { status: 1, data: '' };
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

    await claimVip({ adLoopMax: 2, adIntervalMs: 0 });

    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
    expect(userStore.claimMessage).toContain('已激活每日 VIP');
    // 2 ad rounds + one final vip detail sync
    expect(mockApiGet).toHaveBeenCalledWith('/youth/vip/ad');
    expect(mockApiGet.mock.calls.filter((c: any[]) => c[0] === '/youth/vip/ad')).toHaveLength(2);
    expect(mockApiGet.mock.calls.filter((c: any[]) => c[0] === '/user/vip/detail')).toHaveLength(1);
  });

  it('falls back to day-vip when the ad channel is rejected, and syncs on success', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/day/vip') {
        return { status: 1, data: '' };
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

    await claimVip({ adLoopMax: 1, adIntervalMs: 0 });

    expect(mockApiGet).toHaveBeenCalledWith('/youth/day/vip');
    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
    expect(userStore.claimMessage).toContain('已激活每日 VIP');
  });

  it('tries every channel in order and surfaces the last upstream code when all are rejected', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/day/vip') {
        return { status: 0, error_code: 51002, error_msg: '' };
      }
      if (path === '/youth/listen/song') {
        return { status: 0, error_code: 130012, error_msg: '' };
      }
      if (path === '/youth/day/vip/upgrade') {
        return { status: 0, error_code: 304001, error_msg: '' };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVip({ adLoopMax: 1, adIntervalMs: 0 });

    expect(mockApiGet).toHaveBeenCalledWith('/youth/vip/ad');
    expect(mockApiGet).toHaveBeenCalledWith('/youth/day/vip');
    expect(mockApiGet).toHaveBeenCalledWith('/youth/listen/song');
    expect(mockApiGet).toHaveBeenCalledWith('/youth/day/vip/upgrade');
    expect(userStore.claimMessage).toContain('304001');
    expect(userStore.isVip).toBe(false);
  });

  it('claimVipViaRoute success enters confirm and applies authoritative VIP with state reset', async () => {
    // 批次 C 审查 P1：route 成功路径此前零覆盖，状态机不对齐（取消标志/
    // claimStage 残留毒化后续调用）被漏网。锁定：成功 → 待确认 → 权威生效
    // → 已激活，且 finally 复位全部流程状态。
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/day/vip/upgrade') {
        return { status: 1, data: '' };
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

    await claimVipViaRoute('day-upgrade');

    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
    expect(userStore.vipClaimPending).toBe(false);
    expect(userStore.claimStage).toBe('');
    expect(userStore.claimCancelRequested).toBe(false);
    expect(userStore.loading).toBe(false);
    expect(userStore.claimMessage).toContain('已激活每日 VIP');
  });

  it('claimVipViaRoute resets a stale cancel flag between runs (no poison)', async () => {
    // 批次 C 审查 P1 的毒化场景：上一次运行的取消请求残留 → 下一次 route
    // 调用在通道完成后被误判"已取消"。开头复位后不得发生。
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/day/vip/upgrade') {
        return { status: 1, data: '' };
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

    cancelClaim(); // 模拟上一次运行遗留的取消请求
    await claimVipViaRoute('day-upgrade');

    expect(userStore.isVip).toBe(true);
    expect(userStore.claimMessage).toContain('已激活每日 VIP');
    expect(userStore.claimCancelRequested).toBe(false);
  });

  it('propagates vipStatus three states into the store via login refresh', async () => {
    // 批次 C 审查 P2：5a 三态必须有 store 层载体（applied 时透传）。
    userStore.isLoggedIn = true;
    userStore.userId = '42';
    mockReadyDevice();
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return { status: 1, data: { userid: 42, nickname: 'Bottle' } };
      }
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_end_time: '2025-01-01 00:00:00' },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await checkLoginStatus();
    expect(userStore.isVip).toBe(false);
    expect(userStore.vipStatus).toBe('expired');

    // 再来一次 unknown（日期非法）——确认三态都可透传。
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return { status: 1, data: { userid: 42, nickname: 'Bottle' } };
      }
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_end_time: 'not-a-date' },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });
    await checkLoginStatus();
    expect(userStore.isVip).toBe(false);
    expect(userStore.vipStatus).toBe('unknown');
  });

  it('claimVipViaRoute dispatches the experimental route and shows upstream failure verbatim', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/day/vip/upgrade') {
        return { status: 0, error_code: 131001, error_msg: 'ad token invalid' };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVipViaRoute('day-upgrade');

    expect(mockApiGet).toHaveBeenCalledWith('/youth/day/vip/upgrade');
    expect(userStore.claimMessage).toContain('131001');
    expect(userStore.claimMessage).toContain('ad token invalid');
    expect(userStore.isVip).toBe(false);
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
