import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { CircuitBreaker } from '../circuitBreaker';

describe('CircuitBreaker', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it('opens after 5 consecutive failures', () => {
    const cb = new CircuitBreaker({ failureThreshold: 5, openDurationMs: 30_000 });
    expect(cb.isClosed()).toBe(true);
    for (let i = 0; i < 5; i++) {
      cb.recordFailure();
    }
    expect(cb.isClosed()).toBe(false);
  });

  it('closes again after a success', () => {
    const cb = new CircuitBreaker({ failureThreshold: 2, openDurationMs: 30_000 });
    cb.recordFailure();
    cb.recordFailure();
    expect(cb.isClosed()).toBe(false);
    cb.recordSuccess();
    expect(cb.isClosed()).toBe(true);
  });

  it('half-opens after open duration and probe success closes', async () => {
    const cb = new CircuitBreaker({ failureThreshold: 1, openDurationMs: 50 });
    cb.recordFailure();
    expect(cb.isClosed()).toBe(false);
    // Advance fake timers past the open duration
    vi.advanceTimersByTime(60);
    // Now isClosed() should return true (half-open: timer expired, reset)
    expect(cb.isClosed()).toBe(true);
    cb.recordSuccess();
    expect(cb.isClosed()).toBe(true);
  });

  it('stays open before open duration expires', () => {
    const cb = new CircuitBreaker({ failureThreshold: 1, openDurationMs: 50_000 });
    cb.recordFailure();
    vi.advanceTimersByTime(49_999);
    expect(cb.isClosed()).toBe(false);
  });

  it('does not open if failures are below threshold', () => {
    const cb = new CircuitBreaker({ failureThreshold: 5, openDurationMs: 30_000 });
    for (let i = 0; i < 4; i++) cb.recordFailure();
    expect(cb.isClosed()).toBe(true);
    cb.recordSuccess();
    expect(cb.isClosed()).toBe(true); // still closed, success resets counter
  });
});
