import { describe, it, expect, vi, beforeEach } from "vitest";

const mockApiGet = vi.fn();

vi.mock("../../../platform/tauri/nativeClient", () => ({
  apiGet: (...args: unknown[]) => mockApiGet(...args),
}));

import { fetchPersonalFm } from "../fmGateway";

describe("fmGateway contract", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("fetchPersonalFm calls /personal/fm with query params", async () => {
    mockApiGet.mockResolvedValueOnce({ status: 1, data: [] });
    const res = await fetchPersonalFm({ hash: "abc", remain_songcnt: 0, is_overplay: 1 });
    expect(mockApiGet).toHaveBeenCalledWith("/personal/fm", { hash: "abc", remain_songcnt: 0, is_overplay: 1 });
    expect(res.status).toBe(1);
  });
});
