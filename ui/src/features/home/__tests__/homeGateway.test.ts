import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiGet = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

import {
  fetchEverydayRecommend,
  fetchTopSong,
  fetchTopPlaylist,
} from "../homeGateway";

describe("homeGateway contract", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("fetchEverydayRecommend calls /everyday/recommend with pagesize", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { song_list: [] } });
    const res = await fetchEverydayRecommend(6);
    expect(mockApiGet).toHaveBeenCalledWith("/everyday/recommend", { pagesize: 6 });
    expect(res.status).toBe(1);
  });

  it("fetchTopSong calls /top/song with pagesize", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { info: [] } });
    const res = await fetchTopSong(6);
    expect(mockApiGet).toHaveBeenCalledWith("/top/song", { pagesize: 6 });
    expect(res.status).toBe(1);
  });

  it("fetchTopPlaylist calls /top/playlist with params", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { list: [] } });
    const res = await fetchTopPlaylist({ pagesize: 5, sort: 2 });
    expect(mockApiGet).toHaveBeenCalledWith("/top/playlist", { pagesize: 5, sort: 2 });
    expect(res.status).toBe(1);
  });
});
