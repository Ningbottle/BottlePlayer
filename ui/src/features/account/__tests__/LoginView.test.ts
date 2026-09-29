import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { nextTick } from 'vue';
import LoginView from '../LoginView.vue';

const mockApiGet = vi.fn();
vi.mock('../../../platform/tauri/nativeClient', () => ({
  apiGet: (...args: any[]) => mockApiGet(...args),
}));

// W4（批次 C 审查）：mock 的 userStore 必须是普通对象（vi.mock 工厂被提升，
// 不能引用本文件顶层变量），测试在 mount 之前写入状态、mount 时读取当前值；
// 同时补上 cancelClaim —— 此前 mock 缺失，取消按钮的点击处理器在该环境下
// 是 undefined。
// vi.mock 工厂在提升后立即解引用 userStore 对象 —— 必须用 vi.hoisted 让
// 它先于工厂初始化（mockApiGet 无需如此：工厂只捕获闭包，调用时才解引用）。
const mockUserStore = vi.hoisted(() => ({
  isLoggedIn: false,
  deviceReady: false,
  userId: '42',
  username: '听歌用户',
  avatar: '',
  vipLevel: 0,
  vipType: 0,
  isVip: false,
  vipStatus: 'unknown' as 'active' | 'expired' | 'unknown',
  vipEndDate: '',
  loading: false,
  claimMessage: '',
  claimStage: '' as '' | 'device' | 'ad' | 'fallback' | 'confirm',
  claimCancelRequested: false,
  vipClaimPending: false,
  musicPermission: 'unknown' as 'unknown' | 'active' | 'expired' | 'observed_non_music',
  claimLedger: {
    lastAcceptedAt: null as string | null,
    lastAcceptedRoute: null as string | null,
    lastAcceptedSummary: null as string | null,
    lastFailureAt: null as string | null,
    lastFailureCode: null as string | number | null,
    lastFailureMessage: null as string | null,
  },
  observedEntitlements: [] as Array<Record<string, unknown>>,
}));
const mockCancelClaim = vi.hoisted(() => vi.fn());

vi.mock('../userStore', () => ({
  userStore: mockUserStore,
  checkLoginStatus: vi.fn(),
  claimVip: vi.fn(),
  cancelClaim: mockCancelClaim,
  claimVipViaRoute: vi.fn(),
  claimDayVipConceptCandidate: vi.fn(),
  isDayConceptCandidateUiEnabled: true,
  logoutLocal: vi.fn(),
  VIP_CLAIM_ROUTES: [
    { id: 'day', label: '直接领取（1天）' },
    { id: 'listen', label: '听歌上报' },
    { id: 'ad', label: '广告上报' },
    { id: 'day-upgrade', label: '看广告升级' },
  ],
  refreshLiveVip: vi.fn(),
  startVipClock: vi.fn(),
  stopVipClock: vi.fn(),
}));

vi.mock('qrcode', () => ({
  default: { toDataURL: () => Promise.resolve('data:image/png;base64,xxx') },
}));

function resetMockUserStore() {
  Object.assign(mockUserStore, {
    isLoggedIn: false,
    deviceReady: false,
    userId: '42',
    username: '听歌用户',
    avatar: '',
    vipLevel: 0,
    vipType: 0,
    isVip: false,
    vipStatus: 'unknown',
    vipEndDate: '',
    loading: false,
    claimMessage: '',
    claimStage: '',
    claimCancelRequested: false,
    vipClaimPending: false,
    musicPermission: 'unknown',
    claimLedger: {
      lastAcceptedAt: null,
      lastAcceptedRoute: null,
      lastAcceptedSummary: null,
      lastFailureAt: null,
      lastFailureCode: null,
      lastFailureMessage: null,
    },
    observedEntitlements: [],
  });
}

describe('LoginView QR poll', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    mockApiGet.mockReset();
    resetMockUserStore();
  });

  it('does not start a second poll while the first is still pending', async () => {
    mockApiGet
      .mockResolvedValueOnce({
        status: 1,
        data: { qrcode: 'key-123', qrcodeurl: 'http://test' },
      }) // generateQrCode
      .mockImplementationOnce(
        () => new Promise(resolve => setTimeout(resolve, 10_000))
      ); // first poll hangs

    const wrapper = mount(LoginView);
    await flushPromises();
    // initial 2s base delay before first poll
    await vi.advanceTimersByTimeAsync(2_000);
    expect(mockApiGet).toHaveBeenCalledTimes(2); // generate + first poll

    await vi.advanceTimersByTimeAsync(4_000);
    // should NOT have fired a third call (second poll) while first is pending
    expect(mockApiGet).toHaveBeenCalledTimes(2);
    wrapper.unmount();
  });

  it('does not revive polling when QR-key response arrives after unmount', async () => {
    let resolveKey!: (value: unknown) => void;
    mockApiGet.mockReturnValueOnce(new Promise(resolve => { resolveKey = resolve; }));
    const wrapper = mount(LoginView);
    expect(mockApiGet).toHaveBeenCalledTimes(1);
    wrapper.unmount();
    resolveKey({ status: 1, data: { qrcode: 'late-key', qrcodeurl: 'http://test' } });
    await flushPromises();
    await vi.advanceTimersByTimeAsync(20_000);
    expect(mockApiGet).toHaveBeenCalledTimes(1);
  });

  it('does not restart polling after an expired QR result', async () => {
    mockApiGet
      .mockResolvedValueOnce({ status: 1, data: { qrcode: 'expired-key', qrcodeurl: 'http://test' } })
      .mockResolvedValueOnce({ status: 1, data: { status: 3 } });
    const wrapper = mount(LoginView);
    try {
      await flushPromises();
      expect(wrapper.text()).toContain('二维码已过期');
      await vi.advanceTimersByTimeAsync(20_000);
      expect(mockApiGet).toHaveBeenCalledTimes(2);

      mockApiGet.mockResolvedValueOnce({
        status: 1,
        data: { qrcode: 'refreshed-key', qrcodeurl: 'http://test/refreshed' },
      }).mockResolvedValueOnce({ status: 1, data: { status: 0 } });
      const refresh = wrapper.get('[data-test="expired-qr-refresh"]');
      expect(refresh.element.tagName).toBe('BUTTON');
      expect(refresh.attributes('type')).toBe('button');
      expect(refresh.attributes('aria-label')).toContain('刷新二维码');
      await refresh.trigger('click');
      await flushPromises();
      expect(mockApiGet).toHaveBeenCalledTimes(4);
      expect(wrapper.find('[data-test="expired-qr-refresh"]').exists()).toBe(false);
    } finally {
      wrapper.unmount();
    }
  });

  it('does not navigate after unmount while the post-login status check is pending', async () => {
    const { checkLoginStatus } = await import('../userStore');
    let resolveCheck!: () => void;
    vi.mocked(checkLoginStatus).mockImplementationOnce(
      () => new Promise<void>(resolve => { resolveCheck = resolve; }),
    );
    mockApiGet
      .mockResolvedValueOnce({ status: 1, data: { qrcode: 'success-key', qrcodeurl: 'http://test' } })
      .mockResolvedValueOnce({ status: 1, data: { status: 4 } });
    const wrapper = mount(LoginView);
    await flushPromises();
    await vi.advanceTimersByTimeAsync(1000);
    expect(checkLoginStatus).toHaveBeenCalled();
    wrapper.unmount();
    mockUserStore.isLoggedIn = true;
    resolveCheck();
    await flushPromises();
    expect(wrapper.emitted('navigate')).toBeUndefined();
  });
});

describe('LoginView VIP 三态展示（Stage 5a 前端约束）', () => {
  beforeEach(() => {
    vi.useRealTimers();
    mockApiGet.mockReset();
    resetMockUserStore();
    mockUserStore.isLoggedIn = true;
  });

  it('active → 显示剩余时间（不得出现 已过期/未知/未开通）', () => {
    mockUserStore.isVip = true;
    mockUserStore.vipStatus = 'active';
    const wrapper = mount(LoginView);
    const text = wrapper.text();
    expect(text).toContain('无期限');
    expect(text).not.toContain('会员已过期');
    expect(text).not.toContain('权益状态未知');
    expect(text).not.toContain('未开通');
    wrapper.unmount();
  });

  it('expired → 明确显示 会员已过期（不得回退为 未开通）', () => {
    mockUserStore.isVip = false;
    mockUserStore.vipStatus = 'expired';
    const wrapper = mount(LoginView);
    const text = wrapper.text();
    expect(text).toContain('会员已过期');
    expect(text).not.toContain('未开通');
    expect(text).not.toContain('权益状态未知');
    wrapper.unmount();
  });

  it('unknown → 中性 权益状态未知（不得声称 已过期/未开通）', () => {
    mockUserStore.isVip = false;
    mockUserStore.vipStatus = 'unknown';
    const wrapper = mount(LoginView);
    const text = wrapper.text();
    expect(text).toContain('权益状态未知');
    expect(text).not.toContain('会员已过期');
    expect(text).not.toContain('未开通');
    wrapper.unmount();
  });

  it('observed_non_music → 显示已观察到权益 · 音乐适用性待确认，而非音乐会员或未开通', () => {
    mockUserStore.isLoggedIn = true;
    mockUserStore.isVip = false;
    mockUserStore.vipStatus = 'unknown';
    mockUserStore.musicPermission = 'observed_non_music';
    const wrapper = mount(LoginView);
    const text = wrapper.text();
    expect(text).toContain('已观察到权益');
    expect(text).toContain('音乐适用性待确认');
    expect(text).not.toContain('非音乐');
    expect(text).not.toContain('VIP · Lv.');
    wrapper.unmount();
  });

  it('claim ledger lines show acceptance and later failure separately', () => {
    mockUserStore.isLoggedIn = true;
    mockUserStore.isVip = false;
    mockUserStore.musicPermission = 'observed_non_music';
    mockUserStore.claimLedger.lastAcceptedAt = '2026-09-18T16:51:19+08:00';
    mockUserStore.claimLedger.lastAcceptedRoute = 'day-concept';
    mockUserStore.claimLedger.lastFailureAt = '2026-09-18T16:51:23+08:00';
    mockUserStore.claimLedger.lastFailureCode = 131001;
    mockUserStore.claimLedger.lastFailureMessage = 'x';
    const wrapper = mount(LoginView);
    expect(wrapper.find('[data-test="claim-ledger-accepted"]').exists()).toBe(true);
    expect(wrapper.find('[data-test="claim-ledger-failure"]').exists()).toBe(true);
    expect(wrapper.text()).toContain('不抹掉此前受理');
    wrapper.unmount();
  });

  it('renders the Debug day Concept candidate button when candidate UI is enabled', async () => {
    const { claimDayVipConceptCandidate } = await import('../userStore');
    mockUserStore.isLoggedIn = true;
    const wrapper = mount(LoginView);
    const btn = wrapper.find('[data-test="claim-day-concept-candidate"]');
    expect(btn.exists()).toBe(true);
    expect(btn.text()).toContain('day Concept 候选');
    await btn.trigger('click');
    expect(claimDayVipConceptCandidate).toHaveBeenCalledTimes(1);
    wrapper.unmount();
  });

  it('cached active VIP crossing the end time updates copy, badge, and claim CTA together', async () => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-09-15T20:16:08'));
    mockUserStore.isVip = true;
    mockUserStore.vipStatus = 'active';
    mockUserStore.vipLevel = 1;
    mockUserStore.vipEndDate = '2026-09-15 20:19:08';
    const wrapper = mount(LoginView);
    try {
      expect(wrapper.find('.vip-remaining').text()).toBe('剩 3 分钟');
      expect(wrapper.find('.avatar-badge').exists()).toBe(true);
      expect(wrapper.find('.avatar-large').classes()).toContain('is-vip');
      expect(wrapper.find('[data-test="claim-vip"]').text()).toContain('续领今日 VIP');

      await vi.advanceTimersByTimeAsync(181_000);
      await nextTick();

      expect(wrapper.find('.vip-remaining').text()).toBe('会员已过期');
      expect(wrapper.find('.vip-remaining').text()).not.toContain('即将到期');
      expect(wrapper.find('.avatar-badge').exists()).toBe(false);
      expect(wrapper.find('.avatar-large').classes()).not.toContain('is-vip');
      expect(wrapper.find('[data-test="claim-vip"]').text()).toBe('领取每日免费 VIP');
    } finally {
      wrapper.unmount();
      vi.useRealTimers();
    }
  });
});

describe('LoginView 取消按钮接线（Stage 5c）', () => {
  beforeEach(() => {
    vi.useRealTimers();
    mockApiGet.mockReset();
    resetMockUserStore();
    mockUserStore.isLoggedIn = true;
  });

  it('领取进行中 → 取消按钮可见并可触发 cancelClaim', async () => {
    mockUserStore.loading = true;
    mockUserStore.claimStage = 'ad';
    const wrapper = mount(LoginView);
    const cancelBtn = wrapper.find('[data-test="claim-cancel"]');
    expect(cancelBtn.exists()).toBe(true);
    await cancelBtn.trigger('click');
    expect(mockCancelClaim).toHaveBeenCalledTimes(1);
    wrapper.unmount();
  });

  it('空闲时 → 取消按钮隐藏', () => {
    mockUserStore.loading = false;
    mockUserStore.claimStage = '';
    const wrapper = mount(LoginView);
    expect(wrapper.find('[data-test="claim-cancel"]').exists()).toBe(false);
    wrapper.unmount();
  });
});
