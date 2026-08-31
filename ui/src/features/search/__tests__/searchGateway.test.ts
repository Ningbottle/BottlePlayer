import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiGet = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

import { searchSongs } from "../searchGateway";

describe("searchGateway contract", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("searchSongs calls /search with keywords and pagination", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { lists: [], total: 0 } });
    const res = await searchSongs({ keywords: "test", page: 1, pagesize: 25 });
    expect(mockApiGet).toHaveBeenCalledWith("/search", { keywords: "test", page: 1, pagesize: 25 });
    expect(res.status).toBe(1);
  });
});
