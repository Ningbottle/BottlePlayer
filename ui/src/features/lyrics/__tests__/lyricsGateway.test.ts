import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiGet = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

import { searchLyricCandidates, fetchLyricDetail } from "../lyricsGateway";

describe("lyricsGateway contract", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("searchLyricCandidates calls /search/lyric with hash", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, candidates: [{ id: "1", accesskey: "k" }] });
    const res = await searchLyricCandidates("abc");
    expect(mockApiGet).toHaveBeenCalledWith("/search/lyric", { hash: "abc" });
    expect(res.candidates?.[0].id).toBe("1");
  });

  it("fetchLyricDetail calls /lyric with id and accesskey", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, lyric: "[00:01.00]hello" });
    const res = await fetchLyricDetail("1", "k");
    expect(mockApiGet).toHaveBeenCalledWith("/lyric", { id: "1", accesskey: "k" });
    expect(res.lyric).toBe("[00:01.00]hello");
  });
});
