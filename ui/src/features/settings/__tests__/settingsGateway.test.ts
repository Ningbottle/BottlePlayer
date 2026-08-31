import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiGet = vi.fn();
const mockApiPost = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
  apiPost: (...args: unknown[]) => mockApiPost(...args),
}));

import {
  fetchDiagnosticsMemory,
  fetchDeviceSettings,
  saveDeviceSettings,
  resetDeviceSettings,
  probeSongUrl,
  claimYouthListenSong,
  claimYouthVipAd,
} from "../settingsGateway";

describe("settingsGateway contract", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("fetchDiagnosticsMemory calls /diagnostics/memory", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { text: "ok" } });
    const res = await fetchDiagnosticsMemory();
    expect(mockApiGet).toHaveBeenCalledWith("/diagnostics/memory");
    expect(res.status).toBe(1);
  });

  it("fetchDeviceSettings calls /settings/device", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { dfid: "d1" } });
    const res = await fetchDeviceSettings();
    expect(mockApiGet).toHaveBeenCalledWith("/settings/device");
    expect(res.data.dfid).toBe("d1");
  });

  it("saveDeviceSettings calls /settings/device via apiPost", async () => {
    mockApiPost.mockResolvedValueOnce({ status: 1, data: { dfid: "d1" }, updated: true });
    const res = await saveDeviceSettings({ dfid: "d1" });
    expect(mockApiPost).toHaveBeenCalledWith("/settings/device", undefined, { dfid: "d1" });
    expect(res.updated).toBe(true);
  });

  it("resetDeviceSettings calls /settings/device with clear flag", async () => {
    mockApiPost.mockResolvedValueOnce({ status: 1, data: { dfid: "" } });
    const res = await resetDeviceSettings();
    expect(mockApiPost).toHaveBeenCalledWith("/settings/device", undefined, { clear: "1" });
    expect(res.status).toBe(1);
  });

  it("probeSongUrl calls /song/url with params", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, url: "http://test/full/audio" });
    const res = await probeSongUrl({ hash: "abc" });
    expect(mockApiGet).toHaveBeenCalledWith("/song/url", { hash: "abc" });
    expect(res.url).toBe("http://test/full/audio");
  });

  it("claimYouthListenSong calls /youth/listen/song", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1 });
    const res = await claimYouthListenSong();
    expect(mockApiGet).toHaveBeenCalledWith("/youth/listen/song");
    expect(res.status).toBe(1);
  });

  it("claimYouthVipAd calls /youth/vip/ad", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1 });
    const res = await claimYouthVipAd();
    expect(mockApiGet).toHaveBeenCalledWith("/youth/vip/ad");
    expect(res.status).toBe(1);
  });
});
