import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
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
}));
const mockCancelClaim = vi.hoisted(() => vi.fn());

vi.mock('../userStore', () => ({
  userStore: mockUserStore,
  checkLoginStatus: vi.fn(),
  claimVip: vi.fn(),
  cancelClaim: mockCancelClaim,
  claimVipViaRoute: vi.fn(),
  logoutLocal: vi.fn(),
  VIP_CLAIM_ROUTES: [
    { id: 'day', label: '直接领取（1天）' },
    { id: 'listen', label: '听歌上报' },
    { id: 'ad', label: '广告上报' },
    { id: 'day-upgrade', label: '看广告升级' },
  ],
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
