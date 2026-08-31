import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiGet = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

import { fetchUserPlaylistsRaw, fetchPlaylistTracks } from "../playlistGateway";

describe("playlistGateway contract", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("fetchUserPlaylistsRaw calls /user/playlist with pagination", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { info: [] } });
    const res = await fetchUserPlaylistsRaw(1, 100);
    expect(mockApiGet).toHaveBeenCalledWith("/user/playlist", { page: 1, pagesize: 100 });
    expect(res.status).toBe(1);
  });

  it("fetchPlaylistTracks calls /playlist/track/all with params", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { list: [], total: 0 } });
    const res = await fetchPlaylistTracks({ id: "collection_1", page: 2, pagesize: 50 });
    expect(mockApiGet).toHaveBeenCalledWith("/playlist/track/all", { id: "collection_1", page: 2, pagesize: 50 });
    expect(res.status).toBe(1);
  });
});
