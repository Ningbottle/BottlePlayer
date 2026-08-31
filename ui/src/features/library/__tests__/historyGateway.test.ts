import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiGet = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

import { fetchUserHistory } from "../historyGateway";

describe("historyGateway contract", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("fetchUserHistory calls /user/history with pagesize", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: { info: [] } });
    const res = await fetchUserHistory(100);
    expect(mockApiGet).toHaveBeenCalledWith("/user/history", { pagesize: 100 });
    expect(res.status).toBe(1);
  });
});
