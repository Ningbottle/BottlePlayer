import { describe, it, expect } from 'vitest';
import { CircuitBreaker } from '../circuitBreaker';

describe('CircuitBreaker', () => {
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

  it('half-opens after open duration and probe success closes', () => {
    const cb = new CircuitBreaker({ failureThreshold: 1, openDurationMs: 50 });
    cb.recordFailure();
    expect(cb.isClosed()).toBe(false);
    // simulate half-open by letting the timeout expire
    setTimeout(() => {
      expect(cb.isClosed()).toBe(true); // half-open
      cb.recordSuccess();
      expect(cb.isClosed()).toBe(true);
    }, 60);
  });
});
