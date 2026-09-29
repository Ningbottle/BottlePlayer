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

  it("addPlaylistTracks sends structured tracks in the JSON body without delimiter escaping", async () => {
    mockApiPost.mockResolvedValueOnce({ status: 1 });
    const params = {
      listid: "101",
      data: [{ name: "Song, with comma | pipe %7C and 中文", hash: "HASH1", album_id: "111", mixsongid: "222" }],
    };
    const res = await addPlaylistTracks(params);
    expect(mockApiPost).toHaveBeenCalledWith("/playlist/tracks/add", JSON.stringify(params));
    expect(res.status).toBe(1);
  });

  it("removePlaylistTracks calls /playlist/tracks/del with params", async () => {
    mockApiPost.mockResolvedValueOnce({ status: 1 });
    const res = await removePlaylistTracks({ listid: "101", fileids: "202" });
    expect(mockApiPost).toHaveBeenCalledWith("/playlist/tracks/del", undefined, { listid: "101", fileids: "202" });
    expect(res.status).toBe(1);
  });
});
