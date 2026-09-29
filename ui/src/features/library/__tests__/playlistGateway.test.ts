import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiGet = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

import { fetchAllPlaylistTracks, fetchUserPlaylistsRaw, fetchPlaylistTracks } from "../playlistGateway";

describe("playlistGateway contract", () => {
  beforeEach(() => {
    mockApiGet.mockReset();
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

  it("marks a short page incomplete when the reported total is still larger", async () => {
    mockApiGet.mockResolvedValueOnce({
      status: 1,
      data: { list: [{ FileHash: "a" }, { FileHash: "b" }], total: 5 },
    });

    const result = await fetchAllPlaylistTracks({ id: "pl-short", pageSize: 3 });

    expect(result).toMatchObject({
      tracks: [{ FileHash: "a" }, { FileHash: "b" }],
      total: 5,
      pagesFetched: 1,
      incomplete: true,
      cancelled: false,
    });
    expect(result.error).toContain("2/5");
    expect(mockApiGet).toHaveBeenCalledTimes(1);
  });

  it("keeps an unknown-total walk incomplete when it reaches the page cap", async () => {
    mockApiGet.mockImplementation((_path, params: { page: number }) => {
      const first = (params.page - 1) * 2;
      return Promise.resolve({
        status: 1,
        data: { list: [{ FileHash: `h${first}` }, { FileHash: `h${first + 1}` }], total: 0 },
      });
    });

    const result = await fetchAllPlaylistTracks({ id: "pl-unknown", pageSize: 2, maxPages: 3 });

    expect(result.tracks).toHaveLength(6);
    expect(result.total).toBe(0);
    expect(result.pagesFetched).toBe(3);
    expect(result.incomplete).toBe(true);
    expect(result.error).toContain("上限");
    expect(mockApiGet).toHaveBeenCalledTimes(3);
  });

  it("stops at a repeated page and keeps the queue marked incomplete", async () => {
    mockApiGet.mockResolvedValue({
      status: 1,
      data: { list: [{ FileHash: "a" }, { FileHash: "b" }], total: 0 },
    });

    const result = await fetchAllPlaylistTracks({ id: "pl-repeat", pageSize: 2, maxPages: 8 });

    expect(result.tracks).toEqual([{ FileHash: "a" }, { FileHash: "b" }]);
    expect(result.pagesFetched).toBe(2);
    expect(result.incomplete).toBe(true);
    expect(result.error).toContain("重复");
    expect(mockApiGet).toHaveBeenCalledTimes(2);
  });

  it("returns partial rows after a later page fails", async () => {
    mockApiGet
      .mockResolvedValueOnce({ status: 1, data: { list: [{ FileHash: "a" }, { FileHash: "b" }], total: 4 } })
      .mockResolvedValueOnce({ status: 0, error: "second page unavailable" });

    const result = await fetchAllPlaylistTracks({ id: "pl-partial", pageSize: 2 });

    expect(result.tracks).toEqual([{ FileHash: "a" }, { FileHash: "b" }]);
    expect(result.pagesFetched).toBe(1);
    expect(result.incomplete).toBe(true);
    expect(result.error).toBe("second page unavailable");
  });

  it("stops the walk when the caller cancels after an in-flight page returns", async () => {
    let current = true;
    mockApiGet.mockImplementationOnce(async () => {
      current = false;
      return { status: 1, data: { list: [{ FileHash: "a" }, { FileHash: "b" }], total: 4 } };
    });

    const result = await fetchAllPlaylistTracks({
      id: "pl-cancel",
      pageSize: 2,
      isCurrent: () => current,
    });

    expect(result).toMatchObject({
      tracks: [],
      pagesFetched: 0,
      incomplete: true,
      cancelled: true,
    });
    expect(mockApiGet).toHaveBeenCalledTimes(1);
  });
});
