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
  claimDayVipConceptCandidate,
  ensureVipDeviceReady,
  formatVipClaimFailure,
  logoutLocal,
  refreshLiveVip,
  startVipClock,
  __resetVipClockForTests,
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
  __resetVipClockForTests();
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
  userStore.claimLedger = {
    lastAcceptedAt: null,
    lastAcceptedRoute: null,
    lastAcceptedSummary: null,
    lastFailureAt: null,
    lastFailureCode: null,
    lastFailureMessage: null,
  };
  userStore.observedEntitlements = [];
  userStore.musicPermission = 'unknown';
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

  it('describes empty 51002 as an unspecified upstream rejection, not an SDK requirement', () => {
    const message = formatVipClaimFailure({
      status: 0,
      error_code: 51002,
      error_msg: '',
    });
    expect(message).toBe('领取失败：上游拒绝，原因未明确（错误码 51002）');
    expect(message).not.toContain('官方 App');
    expect(message).not.toContain('SDK');
    expect(message).not.toContain('广告');
  });

  it('preserves an explicit upstream rejection message when error code is 51002', () => {
    const message = formatVipClaimFailure({
      status: 0,
      error_code: 51002,
      error_msg: 'upstream-reason-fixture',
    });
    expect(message).toContain('upstream-reason-fixture');
    expect(message).toContain('51002');
    expect(message).not.toContain('原因未明确');
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

describe('checkLoginStatus ownership — late responses must not write', () => {
  let effects: ReturnType<typeof makeFakeEffects>;

  function deferred<T>() {
    let resolve!: (value: T) => void;
    const promise = new Promise<T>((done) => { resolve = done; });
    return { promise, resolve };
  }

  const detailOf = (id: string) => ({
    status: 1,
    data: { userid: id, nickname: id, pic: `http://img/${id}.png` },
  });
  const readyDevice = {
    status: 1,
    data: { registered: true, dfid: VALID_DFID },
  };

  beforeEach(() => {
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    mockApiPost.mockResolvedValue(readyDevice);
    resetUserStore();
    __resetAccountEffectsForTests();
    effects = makeFakeEffects();
    configureAccountEffects(effects);
  });

  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('does not restore an account when user detail arrives after logoutLocal', async () => {
    const reply = deferred<ReturnType<typeof detailOf>>();
    mockApiGet.mockImplementation((path: string) => {
      expect(path).toBe('/user/detail');
      return reply.promise;
    });

    const pending = checkLoginStatus();
    logoutLocal();
    reply.resolve(detailOf('old'));
    await pending;

    expect(userStore.isLoggedIn).toBe(false);
    expect(userStore.userId).toBe('');
    expect(mockApiPost).not.toHaveBeenCalled();
  });

  it('does not restore login when device registration finishes after logoutLocal', async () => {
    const registerReply = deferred<typeof readyDevice>();
    const entered = deferred<void>();
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') return detailOf('old');
      throw new Error(`unexpected path: ${path}`);
    });
    mockApiPost.mockImplementation((path: string) => {
      expect(path).toBe('/register/dev');
      entered.resolve();
      return registerReply.promise;
    });

    const pending = checkLoginStatus();
    await entered.promise;
    logoutLocal();
    registerReply.resolve(readyDevice);
    await pending;

    expect(userStore.isLoggedIn).toBe(false);
    expect(userStore.deviceReady).toBe(false);
    const vipCalls = mockApiGet.mock.calls.filter((c) => c[0] === '/user/vip/detail');
    expect(vipCalls).toHaveLength(0);
  });

  it('does not overwrite a newer login check with an older response', async () => {
    const olderReply = deferred<ReturnType<typeof detailOf>>();
    mockApiGet
      .mockReturnValueOnce(olderReply.promise)
      .mockResolvedValueOnce(detailOf('new'))
      .mockResolvedValue({ status: 1, data: { is_vip: 0, busi_vip: [] } });

    const older = checkLoginStatus();
    await checkLoginStatus();
    expect(userStore.userId).toBe('new');
    expect(userStore.isLoggedIn).toBe(true);

    olderReply.resolve(detailOf('old'));
    await older;

    expect(userStore.userId).toBe('new');
    expect(userStore.username).toBe('new');
  });

  it('does not let an older check failure clear a newer login (invalid detail after ownership loss)', async () => {
    const errSpy = vi.spyOn(console, 'error').mockImplementation(() => {});
    const olderReply = deferred<any>();
    mockApiGet
      .mockReturnValueOnce(olderReply.promise)
      .mockResolvedValueOnce(detailOf('fresh'))
      .mockResolvedValue({ status: 1, data: { is_vip: 0, busi_vip: [] } });

    const older = checkLoginStatus();
    await checkLoginStatus();
    expect(userStore.isLoggedIn).toBe(true);

    olderReply.resolve({ status: 0, error_code: 'session_expired' });
    await older;

    expect(userStore.isLoggedIn).toBe(true);
    expect(userStore.userId).toBe('fresh');
    expect(effects.onAccountCleared).not.toHaveBeenCalled();
    errSpy.mockRestore();
  });

  it('does not let an older check finally clear a newer check loading flag', async () => {
    const olderReply = deferred<ReturnType<typeof detailOf>>();
    const newerGate = deferred<ReturnType<typeof detailOf>>();
    mockApiGet
      .mockReturnValueOnce(olderReply.promise)
      .mockReturnValueOnce(newerGate.promise)
      .mockResolvedValue({ status: 1, data: { is_vip: 0, busi_vip: [] } });

    const older = checkLoginStatus();
    const newer = checkLoginStatus();
    expect(userStore.loading).toBe(true);

    olderReply.resolve(detailOf('old'));
    await older;
    // Newer check still in flight — loading must stay true.
    expect(userStore.loading).toBe(true);
    expect(userStore.userId).toBe('');

    newerGate.resolve(detailOf('new'));
    await newer;
    expect(userStore.loading).toBe(false);
    expect(userStore.userId).toBe('new');
  });

  it('does not let an older VIP response overwrite a newer account entitlement', async () => {
    const olderVip = deferred<any>();
    let vipCall = 0;
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        vipCall += 1;
        return vipCall === 1 ? detailOf('old') : detailOf('new');
      }
      if (path === '/user/vip/detail') {
        if (vipCall === 1) return olderVip.promise;
        return {
          status: 1,
          data: {
            is_vip: 1,
            vip_type: 1,
            vip_end_time: '2027-01-01 00:00:00',
            nickname: 'new',
          },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });
    mockApiPost.mockResolvedValue(readyDevice);

    const older = checkLoginStatus();
    // Let older finish identity+device and park on VIP detail.
    await vi.waitFor(() => expect(vipCall).toBe(1));
    await Promise.resolve();
    await Promise.resolve();

    await checkLoginStatus();
    expect(userStore.userId).toBe('new');
    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2027-01-01 00:00:00');

    olderVip.resolve({
      status: 1,
      data: {
        is_vip: 1,
        vip_type: 2,
        vip_end_time: '2026-01-01 00:00:00',
        nickname: 'old',
      },
    });
    await older;

    expect(userStore.userId).toBe('new');
    expect(userStore.username).toBe('new');
    expect(userStore.vipType).toBe(1);
    expect(userStore.vipEndDate).toBe('2027-01-01 00:00:00');
  });

  it('allows a cold-start login check to land while isLoggedIn is still false', async () => {
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') return detailOf('cold');
      if (path === '/user/vip/detail') {
        return { status: 1, data: { is_vip: 0, busi_vip: [] } };
      }
      throw new Error(`unexpected path: ${path}`);
    });
    expect(userStore.isLoggedIn).toBe(false);

    await checkLoginStatus();

    expect(userStore.isLoggedIn).toBe(true);
    expect(userStore.userId).toBe('cold');
  });
});

describe('checkLoginStatus VIP — session/entitlement generation guard', () => {
  function deferred<T>() {
    let resolve!: (value: T) => void;
    const promise = new Promise<T>((done) => { resolve = done; });
    return { promise, resolve };
  }

  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-09-18T12:00:00'));
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    mockApiPost.mockResolvedValue({
      status: 1,
      data: { registered: true, dfid: VALID_DFID },
    });
    resetUserStore();
    __resetAccountEffectsForTests();
    configureAccountEffects(makeFakeEffects());
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: '42',
      username: 'fixture',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-18 12:00:07',
      vipType: 1,
      vipLevel: 1,
      deviceReady: true,
      loading: false,
    });
  });

  afterEach(() => {
    __resetVipClockForTests();
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  // Migrated from ui/audit/login-entitlement-order-20260918.probe.ts
  it('does not overwrite a newer same-account expiry confirmation with a delayed login VIP response', async () => {
    const olderVip = deferred<any>();
    let signalStarted!: () => void;
    const started = new Promise<void>((resolve) => { signalStarted = resolve; });
    let vipCalls = 0;

    mockApiGet.mockImplementation((path: string) => {
      if (path === '/user/detail') {
        return Promise.resolve({ status: 1, data: { userid: '42', nickname: 'fixture' } });
      }
      if (path === '/user/vip/detail') {
        vipCalls += 1;
        if (vipCalls === 1) {
          signalStarted();
          return olderVip.promise;
        }
        return Promise.resolve({
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_type: 1, vip_end_time: '2026-09-19 12:00:07' },
        });
      }
      throw new Error(`unexpected path: ${path}`);
    });

    const pendingLogin = checkLoginStatus();
    await started;
    vi.setSystemTime(new Date('2026-09-18T12:00:08'));
    await refreshLiveVip();
    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-09-19 12:00:07');

    olderVip.resolve({
      status: 1,
      authoritative: true,
      data: { is_vip: 1, vip_type: 1, vip_end_time: '2026-09-18 12:00:07' },
    });
    await pendingLogin;

    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-09-19 12:00:07');
  });

  it('still applies an authoritative no-entitlement downgrade when no concurrent update occurs', async () => {
    Object.assign(userStore, {
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-12-31 23:59:59',
      vipType: 1,
    });

    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return { status: 1, data: { userid: '42', nickname: 'fixture' } };
      }
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 0, busi_vip: [] },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await checkLoginStatus();

    expect(userStore.isLoggedIn).toBe(true);
    expect(userStore.isVip).toBe(false);
    expect(userStore.vipEndDate).toBe('');
    expect(userStore.vipStatus).toBe('unknown');
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

  it('does not announce activation from expired cached VIP when authority reports no entitlement', async () => {
    // T1 / F1: 过期缓存 isVip=true + 到期日已过，权威明确无权益 → 不得“已激活”。
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-09-14T12:00:00+08:00'));
    try {
      Object.assign(userStore, {
        isLoggedIn: true,
        userId: 'audit-only',
        deviceReady: true,
        isVip: true,
        vipStatus: 'active',
        vipEndDate: '2026-09-05 16:58:51',
        loading: false,
        claimCancelRequested: false,
        vipClaimPending: false,
      });
      mockApiGet.mockImplementation(async (path: string) => {
        if (path === '/youth/day/vip') return { status: 1, data: '' };
        if (path === '/user/vip/detail') {
          return {
            status: 1,
            authoritative: true,
            data: { is_vip: 0, vip_type: 0, busi_vip: [] },
          };
        }
        throw new Error(`unexpected path: ${path}`);
      });

      await claimVipViaRoute('day');

      expect(userStore.claimMessage).not.toContain('已激活每日 VIP');
      expect(userStore.claimMessage).toContain('未生效');
      expect(userStore.isVip).toBe(false);
      expect(userStore.vipClaimPending).toBe(false);
    } finally {
      vi.useRealTimers();
    }
  });

  it('keeps not-yet-expired prior VIP display but does not claim activation when authority says no', async () => {
    // 旧权益未过期、本次权威无新增权益：展示可暂留，文案不得“已激活”。
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-09-14T12:00:00+08:00'));
    try {
      Object.assign(userStore, {
        isLoggedIn: true,
        userId: 'u-prior',
        deviceReady: true,
        isVip: true,
        vipStatus: 'active',
        vipEndDate: '2026-12-31 23:59:59',
        loading: false,
        claimCancelRequested: false,
        vipClaimPending: false,
      });
      mockApiGet.mockImplementation(async (path: string) => {
        if (path === '/youth/day/vip') return { status: 1, data: '' };
        if (path === '/user/vip/detail') {
          return {
            status: 1,
            authoritative: true,
            data: { is_vip: 0, vip_type: 0, busi_vip: [] },
          };
        }
        throw new Error(`unexpected path: ${path}`);
      });

      await claimVipViaRoute('day');

      expect(userStore.isVip).toBe(true);
      expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
      expect(userStore.claimMessage).toContain('未生效');
      expect(userStore.claimMessage).not.toContain('已激活每日 VIP');
    } finally {
      vi.useRealTimers();
    }
  });

  it('announces activation when no prior VIP and authority confirms entitlement', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'u-fresh',
      deviceReady: true,
      isVip: false,
      vipStatus: 'unknown',
      vipEndDate: '',
    });
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/day/vip') return { status: 1, data: '' };
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 1, vip_type: 1, vip_end_time: '2026-12-31 23:59:59' },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    await claimVipViaRoute('day');

    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
    expect(userStore.claimMessage).toContain('已激活每日 VIP');
  });

  it('late confirm after account switch does not pollute the new session', async () => {
    let releaseDetail: () => void = () => {};
    const detailGate = new Promise<void>((resolve) => {
      releaseDetail = resolve;
    });
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/day/vip') return { status: 1, data: '' };
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

    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'user-a',
      deviceReady: true,
      isVip: false,
    });
    const run = claimVipViaRoute('day');
    await vi.waitFor(() => expect(userStore.claimStage).toBe('confirm'));

    // Switch account while confirm is in flight.
    logoutLocal();
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'user-b',
      deviceReady: true,
      isVip: false,
      vipEndDate: '',
      vipStatus: 'unknown',
      claimMessage: '',
    });
    releaseDetail();
    await run;

    expect(userStore.userId).toBe('user-b');
    expect(userStore.isVip).toBe(false);
    expect(userStore.vipEndDate).toBe('');
    expect(userStore.claimMessage).not.toContain('已激活每日 VIP');
  });

  it('does not continue the old account ad loop after logout, account switch, and a new claim resets cancellation', async () => {
    // 迁移自 ui/audit/claim-generation.probe.ts（审阅 P1 回归）：旧领取卡在
    // 第一轮广告上报 → 登出换号 → 新领取把 claimCancelRequested 复位 →
    // 旧循环不得再发起第二轮上报，也不得写任何 claim 状态。
    let releaseAd: (value: unknown) => void = () => {};
    const adGate = new Promise((resolve) => {
      releaseAd = resolve;
    });
    let adCalls = 0;
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/youth/vip/ad') {
        adCalls += 1;
        if (adCalls === 1) return adGate;
        return { status: 1, data: '' };
      }
      if (path === '/youth/day/vip') return { status: 1, data: '' };
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: { is_vip: 0, vip_type: 0, busi_vip: [] },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });

    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'old-account',
      deviceReady: true,
      isVip: false,
    });
    const oldClaim = claimVip({ adLoopMax: 2, adIntervalMs: 0, recheckAttempts: 0 });
    await vi.waitFor(() => expect(userStore.claimStage).toBe('ad'));

    // 换号：登出（取消标志 + generation 递增 + 会话拆除）后以新账号登录。
    logoutLocal();
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'new-account',
      deviceReady: true,
      isVip: false,
      vipEndDate: '',
      vipStatus: 'unknown',
      claimMessage: '',
    });

    // 新领取复位 claimCancelRequested —— 旧行为下旧循环由此“复活”再打一轮。
    await claimVipViaRoute('day');
    expect(userStore.claimMessage).toContain('未生效');

    releaseAd({ status: 1, data: '' });
    await oldClaim;

    expect(adCalls).toBe(1); // 旧行为：2 次（换号后仍发出第二轮上报）
    expect(userStore.claimMessage).toContain('未生效');
    expect(userStore.claimMessage).not.toContain('已取消领取');
    expect(userStore.isVip).toBe(false);
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
    // Upstream message is preserved when present (fixture text is not a server claim).
    expect(userStore.claimMessage).toContain('131001');
    expect(userStore.claimMessage).toContain('ad token invalid');
    expect(userStore.isVip).toBe(false);
  });

  describe('claim ledger — 受理与拒绝分离', () => {
    const tvipOnlyVip = {
      status: 1,
      authoritative: true,
      data: {
        is_vip: 0,
        vip_type: 0,
        busi_vip: [
          { product_type: 'tvip', is_vip: 1, vip_end_time: '2026-09-19 16:51:19' },
          { product_type: 'svip', is_vip: 0, vip_end_time: '2026-09-15 20:19:08' },
        ],
      },
    };

    beforeEach(() => {
      resetUserStore();
      userStore.isLoggedIn = true;
      userStore.userId = '42';
      mockReadyDevice();
    });

    it('status=1 then confirm sees only active tvip → 音乐权限待确认，不说权益未生效', async () => {
      // Pin clock to just after the live claim so fixture tvip end is still active.
      vi.useFakeTimers();
      vi.setSystemTime(new Date('2026-09-18T16:52:00+08:00'));
      mockApiGet.mockImplementation(async (path: string) => {
        if (path === '/youth/day/vip') return { status: 1, data: {} };
        if (path === '/user/vip/detail') return tvipOnlyVip;
        throw new Error(`unexpected path: ${path}`);
      });

      await claimDayVipConceptCandidate();

      expect(userStore.claimLedger.lastAcceptedAt).toBeTruthy();
      expect(userStore.claimLedger.lastAcceptedRoute).toBe('day-concept');
      expect(userStore.musicPermission).toBe('observed_non_music');
      expect(userStore.isVip).toBe(false);
      expect(userStore.claimMessage).toContain('领取已受理');
      expect(userStore.claimMessage).toContain('tvip');
      expect(userStore.claimMessage).toContain('音乐适用性待确认');
      expect(userStore.claimMessage).not.toContain('非音乐');
      expect(userStore.claimMessage).not.toContain('权益未生效（权威接口未见 VIP）');
      vi.useRealTimers();
    });

    it('later 131001 after acceptance does not erase claimLedger acceptance', async () => {
      vi.useFakeTimers();
      vi.setSystemTime(new Date('2026-09-18T16:52:00+08:00'));
      mockApiGet.mockImplementation(async (path: string) => {
        if (path === '/youth/day/vip') return { status: 1, data: {} };
        if (path === '/user/vip/detail') return tvipOnlyVip;
        throw new Error(`unexpected path: ${path}`);
      });
      await claimDayVipConceptCandidate();
      const acceptedAt = userStore.claimLedger.lastAcceptedAt;
      expect(acceptedAt).toBeTruthy();

      mockApiGet.mockReset();
      mockApiPost.mockImplementation(() =>
        Promise.resolve({ status: 1, data: { registered: true, dfid: VALID_DFID } }),
      );
      mockApiGet.mockResolvedValue({
        status: 0,
        error_code: 131001,
        error_msg: '',
      });

      await claimDayVipConceptCandidate();

      expect(userStore.claimMessage).toContain('131001');
      expect(userStore.claimMessage).not.toContain('已领取成功');
      expect(userStore.claimMessage).toContain('此前受理记录仍保留');
      expect(userStore.claimLedger.lastAcceptedAt).toBe(acceptedAt);
      expect(userStore.claimLedger.lastFailureCode).toBe(131001);
      vi.useRealTimers();
    });

    it('formatVipClaimFailure: empty 131001 does not invent 已领取 or token ad-hoc diagnosis', () => {
      const msg = formatVipClaimFailure({ status: 0, error_code: 131001, error_msg: '' });
      expect(msg).toContain('131001');
      expect(msg).toContain('原因未明确');
      expect(msg).not.toContain('已领取成功');
      expect(msg).not.toContain('ad token invalid');
      expect(msg).not.toContain('广告');
    });

    it('logoutLocal clears claim ledger (session isolation)', async () => {
      mockApiGet.mockImplementation(async (path: string) => {
        if (path === '/youth/day/vip') return { status: 1, data: {} };
        if (path === '/user/vip/detail') return tvipOnlyVip;
        throw new Error(`unexpected path: ${path}`);
      });
      await claimDayVipConceptCandidate();
      expect(userStore.claimLedger.lastAcceptedAt).toBeTruthy();

      logoutLocal();

      expect(userStore.claimLedger.lastAcceptedAt).toBeNull();
      expect(userStore.claimLedger.lastFailureAt).toBeNull();
      expect(userStore.observedEntitlements).toEqual([]);
      expect(userStore.musicPermission).toBe('unknown');
    });

    it('account switch after logout does not reuse prior acceptance', async () => {
      mockApiGet.mockImplementation(async (path: string) => {
        if (path === '/youth/day/vip') return { status: 1, data: {} };
        if (path === '/user/vip/detail') return tvipOnlyVip;
        throw new Error(`unexpected path: ${path}`);
      });
      await claimDayVipConceptCandidate();
      logoutLocal();

      userStore.isLoggedIn = true;
      userStore.userId = 'other-user';
      mockApiGet.mockReset();
      mockApiGet.mockResolvedValue({ status: 0, error_code: 51002, error_msg: '' });
      await claimDayVipConceptCandidate();

      expect(userStore.claimLedger.lastAcceptedAt).toBeNull();
      expect(userStore.claimMessage).not.toContain('此前受理记录仍保留');
    });

    it('expired tvip after confirm window: musicPermission not observed_non_music when not active', async () => {
      vi.useFakeTimers();
      vi.setSystemTime(new Date('2026-09-20T00:00:00'));
      mockApiGet.mockImplementation(async (path: string) => {
        if (path === '/youth/day/vip') return { status: 1, data: {} };
        if (path === '/user/vip/detail') {
          return {
            status: 1,
            data: {
              is_vip: 0,
              busi_vip: [{ product_type: 'tvip', is_vip: 1, vip_end_time: '2026-09-19 16:51:19' }],
            },
          };
        }
        throw new Error(`unexpected path: ${path}`);
      });

      await claimDayVipConceptCandidate();

      expect(userStore.isVip).toBe(false);
      expect(userStore.observedEntitlements[0]?.active).toBe(false);
      expect(userStore.musicPermission).not.toBe('observed_non_music');
      expect(userStore.claimLedger.lastAcceptedAt).toBeTruthy();
      expect(userStore.claimMessage).not.toContain('权益未生效（权威接口未见 VIP）');
      vi.useRealTimers();
    });
  });

  describe('claimDayVipConceptCandidate — single-channel Debug experiment', () => {
    beforeEach(() => {
      userStore.isLoggedIn = true;
      userStore.userId = '42';
      mockReadyDevice();
    });

    it('calls only /youth/day/vip with profile=concept and never auto-fallback routes', async () => {
      mockApiGet.mockImplementation(async (path: string, query?: any) => {
        if (path === '/youth/day/vip') {
          expect(query).toEqual({ profile: 'concept' });
          return { status: 0, error_code: 51002, error_msg: '' };
        }
        throw new Error(`unexpected path: ${path}`);
      });

      await claimDayVipConceptCandidate();

      expect(mockApiGet).toHaveBeenCalledTimes(1);
      expect(mockApiGet).toHaveBeenCalledWith('/youth/day/vip', { profile: 'concept' });
      const paths = mockApiGet.mock.calls.map((c) => c[0]);
      expect(paths).not.toContain('/youth/listen/song');
      expect(paths).not.toContain('/youth/day/vip/upgrade');
      expect(paths).not.toContain('/youth/vip/ad');
      expect(userStore.claimMessage).toContain('day Concept 候选');
      expect(userStore.claimMessage).toContain('51002');
    });

    it('surfaces Release candidate_profile_unavailable without treating it as a claim attempt', async () => {
      mockApiGet.mockResolvedValue({
        status: 0,
        error: 'candidate_profile_unavailable',
        error_msg: 'day Concept 候选仅 Debug 构建可用；未发起上游领取',
        upstream_called: false,
      });

      await claimDayVipConceptCandidate();

      expect(mockApiGet).toHaveBeenCalledTimes(1);
      expect(mockApiGet).toHaveBeenCalledWith('/youth/day/vip', { profile: 'concept' });
      expect(userStore.claimMessage).toContain('不可用');
      expect(userStore.claimMessage).toContain('未发起上游领取');
      expect(userStore.claimMessage).not.toContain('51002');
    });

    it('keeps session ownership and VIP confirm path on accepted candidate response', async () => {
      mockApiGet.mockImplementation(async (path: string) => {
        if (path === '/youth/day/vip') {
          return { status: 1, data: {} };
        }
        if (path === '/user/vip/detail') {
          return {
            status: 1,
            data: {
              is_vip: 1,
              vip_type: 1,
              vip_end_time: '2026-12-31 23:59:59',
            },
          };
        }
        throw new Error(`unexpected path: ${path}`);
      });

      await claimDayVipConceptCandidate();

      expect(userStore.isVip).toBe(true);
      expect(userStore.vipEndDate).toBe('2026-12-31 23:59:59');
      const paths = mockApiGet.mock.calls.map((c) => c[0]);
      expect(paths.filter((p) => p === '/youth/day/vip')).toHaveLength(1);
      expect(paths).toContain('/user/vip/detail');
    });
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

describe('refreshLiveVip — clock-derived expiry without extending stale rights', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-09-15T20:20:00'));
    mockApiGet.mockReset();
    mockApiPost.mockReset();
    resetUserStore();
    __resetAccountEffectsForTests();
    configureAccountEffects(makeFakeEffects());
  });

  afterEach(() => {
    __resetVipClockForTests();
    vi.useRealTimers();
  });

  it('turns a cached active VIP into expired when the known end time has passed', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: '42',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    mockApiGet.mockRejectedValue(new Error('vip detail timeout'));

    await refreshLiveVip(Date.parse('2026-09-15T20:19:09'));

    expect(userStore.isVip).toBe(false);
    expect(userStore.vipStatus).toBe('expired');
    expect(userStore.vipEndDate).toBe('2026-09-15 20:19:08');
  });

  // Migrated from ui/audit/observed-entitlement-expiry-20260919.probe.ts
  it('expires a previously observed tvip when the clock crosses its deadline without a new network snapshot', async () => {
    vi.setSystemTime(new Date('2026-09-19T16:51:12'));
    mockApiGet.mockImplementation(async (path: string) => {
      if (path === '/user/detail') {
        return { status: 1, data: { userid: '42', nickname: 'fixture' } };
      }
      if (path === '/user/vip/detail') {
        return {
          status: 1,
          authoritative: true,
          data: {
            is_vip: 0,
            vip_type: 0,
            busi_vip: [{ product_type: 'tvip', is_vip: 1, vip_end_time: '2026-09-19 16:51:19' }],
          },
        };
      }
      throw new Error(`unexpected path: ${path}`);
    });
    mockApiPost.mockResolvedValue({
      status: 1,
      data: { registered: true, dfid: VALID_DFID },
    });
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: '42',
      deviceReady: true,
      isVip: false,
      vipStatus: 'unknown',
      vipEndDate: '',
      observedEntitlements: [],
      musicPermission: 'unknown',
    });
    await checkLoginStatus();
    expect(userStore.observedEntitlements[0]?.active).toBe(true);
    expect(userStore.musicPermission).toBe('observed_non_music');
    const acceptedBefore = userStore.claimLedger.lastAcceptedAt;

    // Offline/time-derived expiry must work even when the next query fails.
    mockApiGet.mockRejectedValue(new Error('offline'));
    vi.setSystemTime(new Date('2026-09-19T16:51:20'));
    await refreshLiveVip();

    expect(userStore.observedEntitlements[0]?.active).toBe(false);
    expect(userStore.musicPermission).not.toBe('observed_non_music');
    expect(userStore.isVip).toBe(false);
    // Historical observation row is retained (not wiped).
    expect(userStore.observedEntitlements[0]?.productType).toBe('tvip');
    expect(userStore.observedEntitlements[0]?.vipEndDate).toBe('2026-09-19 16:51:19');
    // Claim ledger is independent of observation clock expiry.
    expect(userStore.claimLedger.lastAcceptedAt).toBe(acceptedBefore);
  });

  it('expires only the observation that crossed its deadline when multiple busi_vip deadlines differ', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: '42',
      isVip: false,
      vipStatus: 'unknown',
      vipEndDate: '',
      observedEntitlements: [
        {
          productType: 'tvip',
          isVip: true,
          vipEndDate: '2026-09-19 16:51:19',
          active: true,
          unlocksMusic: false,
        },
        {
          productType: 'musicpack',
          isVip: true,
          vipEndDate: '2026-09-20 00:00:00',
          active: true,
          unlocksMusic: true,
        },
      ],
      musicPermission: 'active',
    });
    mockApiGet.mockRejectedValue(new Error('offline'));

    await refreshLiveVip(Date.parse('2026-09-19T16:52:00'));

    const tvip = userStore.observedEntitlements.find((e) => e.productType === 'tvip');
    const pack = userStore.observedEntitlements.find((e) => e.productType === 'musicpack');
    expect(tvip?.active).toBe(false);
    expect(pack?.active).toBe(true);
    // Active music-class observation implies music permission active even if
    // store.isVip was not re-applied in this fixture.
    expect(userStore.musicPermission).toBe('active');
  });

  it('requests one entitlement recheck after local expiry and does not restore VIP on failure', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: '42',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    mockApiGet.mockRejectedValue(new Error('vip detail timeout'));

    await refreshLiveVip(Date.parse('2026-09-15T20:20:00'));
    await refreshLiveVip(Date.parse('2026-09-15T20:21:00'));

    expect(mockApiGet).toHaveBeenCalledTimes(1);
    expect(mockApiGet).toHaveBeenCalledWith('/user/vip/detail');
    expect(userStore.isVip).toBe(false);
    expect(userStore.vipStatus).toBe('expired');
  });

  it('does not extend known-expired rights when the recheck is non-authoritative', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: '42',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    mockApiGet.mockResolvedValue({
      status: 0,
      authoritative: false,
      data: null,
    });

    await refreshLiveVip(Date.parse('2026-09-15T20:20:00'));

    expect(userStore.isVip).toBe(false);
    expect(userStore.vipStatus).toBe('expired');
  });

  it('accepts a later deadline after a successful recheck', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: '42',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    mockApiGet.mockResolvedValue({
      status: 1,
      authoritative: true,
      data: { is_vip: 1, vip_type: 1, vip_end_time: '2026-09-15 23:19:08' },
    });

    await refreshLiveVip(Date.parse('2026-09-15T20:20:00'));

    expect(userStore.isVip).toBe(true);
    expect(userStore.vipStatus).toBe('active');
    expect(userStore.vipEndDate).toBe('2026-09-15 23:19:08');
  });

  it('recomputes validity when returning to the foreground after crossing expiry', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: '42',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    mockApiGet.mockRejectedValue(new Error('offline'));
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-09-15T20:16:08'));
    startVipClock();
    vi.setSystemTime(new Date('2026-09-15T20:20:00'));
    Object.defineProperty(document, 'hidden', { configurable: true, value: false });
    document.dispatchEvent(new Event('visibilitychange'));
    await Promise.resolve();
    await Promise.resolve();

    expect(userStore.isVip).toBe(false);
    expect(userStore.vipStatus).toBe('expired');
  });

  it('discards an expiry recheck response after logout', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'account-a',
      username: 'A',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    let finish!: (value: unknown) => void;
    mockApiGet.mockImplementation(
      () => new Promise((resolve) => {
        finish = resolve;
      }),
    );

    const pending = refreshLiveVip(Date.parse('2026-09-15T20:20:00'));
    logoutLocal();
    finish({
      status: 1,
      data: {
        is_vip: 1,
        vip_type: 1,
        vip_end_time: '2026-09-16 20:19:08',
        nickname: 'old-account',
      },
    });
    await pending;

    expect(userStore.isLoggedIn).toBe(false);
    expect(userStore.isVip).toBe(false);
    expect(userStore.username).toBe('未登录');
  });

  it('does not replace newer same-account entitlement with a delayed expiry recheck', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'account-a',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    let finish!: (value: unknown) => void;
    mockApiGet.mockImplementation(
      () => new Promise((resolve) => {
        finish = resolve;
      }),
    );

    const pending = refreshLiveVip(Date.parse('2026-09-15T20:20:00'));
    Object.assign(userStore, {
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-16 20:19:08',
      vipType: 1,
    });
    finish({
      status: 1,
      data: {
        is_vip: 0,
        vip_type: 0,
        busi_vip: [{ product_type: 'svip', is_vip: 0, vip_end_time: '2026-09-15 20:19:08' }],
      },
    });
    await pending;

    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-09-16 20:19:08');
  });

  it('discards an expiry recheck after switching accounts without waiting for logout to settle the old response', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'account-a',
      username: 'A',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    let finish!: (value: unknown) => void;
    mockApiGet.mockImplementation(
      () => new Promise((resolve) => {
        finish = resolve;
      }),
    );

    const pending = refreshLiveVip(Date.parse('2026-09-15T20:20:00'));
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'account-b',
      username: 'B',
      isVip: false,
      vipStatus: 'unknown',
      vipEndDate: '',
    });
    finish({
      status: 1,
      data: {
        is_vip: 1,
        vip_type: 1,
        vip_end_time: '2026-09-16 20:19:08',
        nickname: 'old-account',
      },
    });
    await pending;

    expect(userStore.userId).toBe('account-b');
    expect(userStore.username).toBe('B');
    expect(userStore.isVip).toBe(false);
  });

  it('discards an expiry recheck after the same account logs out and back in', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'account-a',
      username: 'A',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    let finish!: (value: unknown) => void;
    mockApiGet.mockImplementation(
      () => new Promise((resolve) => {
        finish = resolve;
      }),
    );

    const pending = refreshLiveVip(Date.parse('2026-09-15T20:20:00'));
    logoutLocal();
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'account-a',
      username: 'A-relogin',
      isVip: false,
      vipStatus: 'unknown',
      vipEndDate: '2026-09-15 20:19:08',
    });
    finish({
      status: 1,
      data: {
        is_vip: 1,
        vip_type: 1,
        vip_end_time: '2026-09-16 20:19:08',
        nickname: 'stale-session',
      },
    });
    await pending;

    expect(userStore.isLoggedIn).toBe(true);
    expect(userStore.userId).toBe('account-a');
    expect(userStore.isVip).toBe(false);
    expect(userStore.username).toBe('A-relogin');
  });

  it('does not let an old recheck finally clear a newer recheck in flight', async () => {
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'account-a',
      username: 'A',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    const resolvers: Array<(value: unknown) => void> = [];
    mockApiGet.mockImplementation(
      () => new Promise((resolve) => {
        resolvers.push(resolve);
      }),
    );

    const pendingOld = refreshLiveVip(Date.parse('2026-09-15T20:20:00'));
    logoutLocal();
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: 'account-b',
      username: 'B',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    const pendingNew = refreshLiveVip(Date.parse('2026-09-15T20:20:00'));
    expect(resolvers).toHaveLength(2);

    resolvers[0]({
      status: 1,
      data: {
        is_vip: 1,
        vip_type: 1,
        vip_end_time: '2026-09-16 20:19:08',
        nickname: 'old-account',
      },
    });
    await pendingOld;

    expect(userStore.userId).toBe('account-b');
    expect(userStore.username).toBe('B');

    resolvers[1]({
      status: 1,
      authoritative: true,
      data: { is_vip: 1, vip_type: 1, vip_end_time: '2026-09-15 23:19:08' },
    });
    await pendingNew;

    expect(userStore.userId).toBe('account-b');
    expect(userStore.isVip).toBe(true);
    expect(userStore.vipEndDate).toBe('2026-09-15 23:19:08');
    expect(userStore.username).toBe('B');
  });

  it('switches local VIP off at the known deadline without waiting for the 30s clock beat', async () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-09-15T20:19:01'));
    Object.assign(userStore, {
      isLoggedIn: true,
      userId: '42',
      isVip: true,
      vipStatus: 'active',
      vipEndDate: '2026-09-15 20:19:08',
    });
    mockApiGet.mockRejectedValue(new Error('offline'));
    startVipClock();

    await vi.advanceTimersByTimeAsync(7_000);

    expect(userStore.isVip).toBe(false);
    expect(userStore.vipStatus).toBe('expired');
  });
});
