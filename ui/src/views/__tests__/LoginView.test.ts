import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import LoginView from '../LoginView.vue';

const mockApiGet = vi.fn();
vi.mock('../../platform/tauri/nativeClient', () => ({
  apiGet: (...args: any[]) => mockApiGet(...args),
}));

vi.mock('../../api/userStore', () => ({
  userStore: {},
  checkLoginStatus: vi.fn(),
  claimVip: vi.fn(),
  logoutLocal: vi.fn(),
}));

vi.mock('qrcode', () => ({
  default: { toDataURL: () => Promise.resolve('data:image/png;base64,xxx') },
}));

describe('LoginView QR poll', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    mockApiGet.mockReset();
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

    mount(LoginView);
    await flushPromises();
    // initial 2s base delay before first poll
    await vi.advanceTimersByTimeAsync(2_000);
    expect(mockApiGet).toHaveBeenCalledTimes(2); // generate + first poll

    await vi.advanceTimersByTimeAsync(4_000);
    // should NOT have fired a third call (second poll) while first is pending
    expect(mockApiGet).toHaveBeenCalledTimes(2);
  });
});
