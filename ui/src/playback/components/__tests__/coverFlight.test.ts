import { describe, it, expect, vi, beforeEach } from 'vitest';

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
