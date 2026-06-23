export interface CircuitBreakerOptions {
  failureThreshold: number;
  openDurationMs: number;
}

export class CircuitBreaker {
  private failures = 0;
  private openedAt = 0;
  private readonly threshold: number;
  private readonly durationMs: number;

  constructor(opts: CircuitBreakerOptions) {
    this.threshold = opts.failureThreshold;
    this.durationMs = opts.openDurationMs;
  }

  isClosed(): boolean {
    if (this.openedAt === 0) return true;
    if (Date.now() - this.openedAt >= this.durationMs) {
      this.openedAt = 0;
      this.failures = 0;
      return true;
    }
    return false;
  }

  recordSuccess(): void {
    this.failures = 0;
    this.openedAt = 0;
  }

  recordFailure(): void {
    this.failures += 1;
    if (this.failures >= this.threshold) {
      this.openedAt = Date.now();
    }
  }
}
