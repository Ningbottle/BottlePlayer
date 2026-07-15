import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { nextTick, ref } from 'vue';
import { useAutoHideControls } from '../useAutoHideControls';

const IDLE_MS = 1_200;

function pointerAt(clientY: number): PointerEvent {
  return new PointerEvent('pointermove', { clientY });
}

describe('useAutoHideControls', () => {
  beforeEach(() => {
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
  });

  it('briefly reveals fullscreen controls, then hides them after the idle delay', () => {
    const controls = useAutoHideControls({
      active: ref(true),
      onEscape: vi.fn(),
      idleMs: IDLE_MS,
    });

    expect(controls.visible.value).toBe(true);
    vi.advanceTimersByTime(IDLE_MS);
    expect(controls.visible.value).toBe(false);
    controls.dispose();
  });

  it('reveals on pointer movement and restarts the idle timer', () => {
    const controls = useAutoHideControls({
      active: ref(true),
      onEscape: vi.fn(),
      idleMs: IDLE_MS,
    });
    vi.advanceTimersByTime(IDLE_MS);

    controls.onPointerMove(pointerAt(120));
    expect(controls.visible.value).toBe(true);
    vi.advanceTimersByTime(IDLE_MS - 1);
    controls.onPointerMove(pointerAt(240));
    vi.advanceTimersByTime(IDLE_MS - 1);
    expect(controls.visible.value).toBe(true);
    vi.advanceTimersByTime(1);
    expect(controls.visible.value).toBe(false);
    controls.dispose();
  });

  it('reveals when the pointer approaches the bottom edge', () => {
    vi.spyOn(window, 'innerHeight', 'get').mockReturnValue(800);
    const controls = useAutoHideControls({
      active: ref(true),
      onEscape: vi.fn(),
      idleMs: IDLE_MS,
    });
    vi.advanceTimersByTime(IDLE_MS);

    controls.onPointerMove(pointerAt(790));
    expect(controls.visible.value).toBe(true);
    controls.dispose();
  });

  it('keeps controls visible while focus is inside and resumes idle hiding on focusout', () => {
    const controls = useAutoHideControls({
      active: ref(true),
      onEscape: vi.fn(),
      idleMs: IDLE_MS,
    });

    controls.onFocusIn();
    vi.advanceTimersByTime(IDLE_MS * 2);
    expect(controls.visible.value).toBe(true);

    controls.onFocusOut();
    vi.advanceTimersByTime(IDLE_MS);
    expect(controls.visible.value).toBe(false);
    controls.dispose();
  });

  it('requests fullscreen exit on Escape only while active', async () => {
    const active = ref(true);
    const onEscape = vi.fn();
    const controls = useAutoHideControls({ active, onEscape, idleMs: IDLE_MS });

    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    expect(onEscape).toHaveBeenCalledTimes(1);

    active.value = false;
    await nextTick();
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    expect(onEscape).toHaveBeenCalledTimes(1);
    controls.dispose();
  });

  it('reacts to fullscreen activation without staying visible outside fullscreen', async () => {
    const active = ref(false);
    const controls = useAutoHideControls({ active, onEscape: vi.fn(), idleMs: IDLE_MS });
    expect(controls.visible.value).toBe(false);

    active.value = true;
    await nextTick();
    expect(controls.visible.value).toBe(true);
    vi.advanceTimersByTime(IDLE_MS);
    expect(controls.visible.value).toBe(false);
    controls.dispose();
  });

  it('dispose clears timers and removes the Escape listener', () => {
    const onEscape = vi.fn();
    const controls = useAutoHideControls({
      active: ref(true),
      onEscape,
      idleMs: IDLE_MS,
    });
    controls.dispose();

    expect(vi.getTimerCount()).toBe(0);
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));
    expect(onEscape).not.toHaveBeenCalled();
  });

  it('keeps pointer, focus, and Escape behavior operable with reduced motion enabled', () => {
    vi.stubGlobal('matchMedia', vi.fn(() => ({
      matches: true,
      media: '(prefers-reduced-motion: reduce)',
      addEventListener: vi.fn(),
      removeEventListener: vi.fn(),
    })));
    const onEscape = vi.fn();
    const controls = useAutoHideControls({
      active: ref(true),
      onEscape,
      idleMs: IDLE_MS,
    });

    vi.advanceTimersByTime(IDLE_MS);
    controls.onPointerMove(pointerAt(790));
    controls.onFocusIn();
    window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }));

    expect(controls.visible.value).toBe(true);
    expect(onEscape).toHaveBeenCalledTimes(1);
    controls.dispose();
  });
});
