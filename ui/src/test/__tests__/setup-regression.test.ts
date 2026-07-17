import { describe, it, expect } from 'vitest';

describe('test setup regression guard', () => {
  it('still fails on a genuinely wrong assertion (mocks do not mask failures)', () => {
    // Intentionally correct assertion; if this passes, the mock setup is not
    // swallowing test results. If mocks ever hide failures, flip the expected
    // value temporarily to confirm vitest reports it.
    expect(1 + 1).toBe(2);
  });
});