import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiGet = vi.fn();
const mockApiPost = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
  apiPost: (...args: unknown[]) => mockApiPost(...args),
}));

import {
  registerDevice,
  fetchUserDetail,
  fetchVipDetail,
  claimDailyVipSong,
  fetchQrKey,
  checkQrStatus,
  logoutAuth,
} from "../accountGateway";

describe("accountGateway contract", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("registerDevice calls /register/dev via apiPost", async () => {
    mockApiPost.mockResolvedValueOnce({ status: 1, data: { registered: true, dfid: "test-dfid" } });
    const res = await registerDevice();
    expect(mockApiPost).toHaveBeenCalledWith("/register/dev");
    expect(res).toEqual({ status: 1, data: { registered: true, dfid: "test-dfid" } });
  });

  it("fetchUserDetail calls /user/detail via apiGet", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { userid: "123", username: "tester" } });
    const res = await fetchUserDetail();
    expect(mockApiGet).toHaveBeenCalledWith("/user/detail");
    expect(res.data?.userid).toBe("123");
  });

  it("fetchVipDetail calls /user/vip/detail via apiGet", async () => {
    mockApiGet.mockResolvedValueOnce({ is_vip: 1 });
    const res = await fetchVipDetail();
    expect(mockApiGet).toHaveBeenCalledWith("/user/vip/detail");
    expect(res).toEqual({ is_vip: 1 });
  });

  it("claimDailyVipSong calls /youth/listen/song via apiGet", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: "" });
    const res = await claimDailyVipSong();
    expect(mockApiGet).toHaveBeenCalledWith("/youth/listen/song");
    expect(res.status).toBe(1);
  });

  it("fetchQrKey calls /login/qr/key via apiGet", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { qrcode: "k123" } });
    const res = await fetchQrKey();
    expect(mockApiGet).toHaveBeenCalledWith("/login/qr/key");
    expect(res.data?.qrcode).toBe("k123");
  });

  it("checkQrStatus calls /login/qr/check with key param", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { status: 1 } });
    const res = await checkQrStatus("k123");
    expect(mockApiGet).toHaveBeenCalledWith("/login/qr/check", { key: "k123" });
    expect(res.data?.status).toBe(1);
  });

  it("logoutAuth calls /auth/logout via apiPost", async () => {
    mockApiPost.mockResolvedValueOnce({ status: 1 });
    const res = await logoutAuth();
    expect(mockApiPost).toHaveBeenCalledWith("/auth/logout");
    expect(res.status).toBe(1);
  });
});
