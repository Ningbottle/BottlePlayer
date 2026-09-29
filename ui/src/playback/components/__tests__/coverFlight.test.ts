import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';

const fitMock = vi.hoisted(() => vi.fn((_el: unknown, _target: unknown, vars: unknown) => vars));

vi.mock('gsap', () => ({
  gsap: { registerPlugin: vi.fn() },
}));
vi.mock('gsap/Flip', () => ({
  Flip: { fit: fitMock },
}));
vi.mock('../../../shared/motion/motion', () => ({
  isReducedMotion: vi.fn(() => false),
}));

import { flyCoverToElement, flyCoverToDock } from '../coverFlight';

describe('coverFlight', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    document.body.innerHTML = '<div class="aurora-pb-cover"></div>';
    vi.spyOn(HTMLElement.prototype, 'getBoundingClientRect').mockReturnValue({ left: 10, top: 20, width: 60, height: 60 } as DOMRect);
  });
  afterEach(() => { vi.restoreAllMocks(); vi.useRealTimers(); });

  it('captures the origin before fullscreen hides the dock and cleans up an interrupted flight', () => {
    vi.useFakeTimers();
    const from = document.createElement('div');
    const rect = vi.fn().mockReturnValue({ left: 10, top: 30, width: 60, height: 60 } as DOMRect);
    Object.defineProperty(from, 'getBoundingClientRect', { value: rect });
    flyCoverToElement(from, '.aurora-pb-cover', 'http://img.example/c.jpg', 300);
    rect.mockReturnValue({ left: 0, top: 0, width: 0, height: 0 } as DOMRect);
    vi.advanceTimersByTime(300);
    const ghost = document.querySelector<HTMLElement>('.aurora-cover-ghost')!;
    expect(ghost.style.width).toBe('60px');
    expect(ghost.style.top).toBe('30px');
    const vars = fitMock.mock.calls[0][2] as { onInterrupt(): void };
    vars.onInterrupt();
    expect(document.querySelector('.aurora-cover-ghost')).toBeNull();
  });

  it('morphs the ghost from square to round while flying', () => {
    const from = document.createElement('div');
    document.body.appendChild(from);

    flyCoverToElement(from, '.aurora-pb-cover', 'http://img.example/c.jpg');

    expect(fitMock).toHaveBeenCalledTimes(1);
    const vars = fitMock.mock.calls[0][2] as Record<string, unknown>;
    expect(vars.borderRadius).toBe('50%');
    expect(vars.duration).toBe(0.55);
  });

  it('flyCoverToDock targets the dock cover', () => {
    const from = document.createElement('div');
    document.body.appendChild(from);

    flyCoverToDock(from, 'http://img.example/c.jpg');

    const target = fitMock.mock.calls[0][1] as HTMLElement;
    expect(target.className).toBe('aurora-pb-cover');
  });

  it('skips entirely without an image url', () => {
    const from = document.createElement('div');
    document.body.appendChild(from);

    flyCoverToDock(from, '');
    expect(fitMock).not.toHaveBeenCalled();
  });
});
