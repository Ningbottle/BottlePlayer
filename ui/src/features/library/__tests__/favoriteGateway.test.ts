import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiPost = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiPost: (...args: unknown[]) => mockApiPost(...args),
}));

import { addPlaylistTracks, removePlaylistTracks } from "../favoriteGateway";

describe("favoriteGateway contract", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("addPlaylistTracks calls /playlist/tracks/add with params", async () => {
    mockApiPost.mockResolvedValueOnce({ status: 1 });
    const res = await addPlaylistTracks({ listid: "101", data: "test|hash|0|0" });
    expect(mockApiPost).toHaveBeenCalledWith("/playlist/tracks/add", undefined, { listid: "101", data: "test|hash|0|0" });
    expect(res.status).toBe(1);
  });

  it("removePlaylistTracks calls /playlist/tracks/del with params", async () => {
    mockApiPost.mockResolvedValueOnce({ status: 1 });
    const res = await removePlaylistTracks({ listid: "101", fileids: "202" });
    expect(mockApiPost).toHaveBeenCalledWith("/playlist/tracks/del", undefined, { listid: "101", fileids: "202" });
    expect(res.status).toBe(1);
  });
});
