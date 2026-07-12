import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { ref, nextTick, effectScope } from 'vue';
import {
  useLyricFollow,
  IDLE_RESUME_MS,
  type UseLyricFollowReturn,
} from '../useLyricFollow';

function setup(opts: { activeIndex?: number; now?: () => number } = {}) {
  const activeIndex = ref(opts.activeIndex ?? 0);
  const scrolledTo: number[] = [];
  const follow = useLyricFollow({
    activeIndex,
    scrollToLine: (idx: number) => scrolledTo.push(idx),
    now: opts.now ?? (() => 0),
  });
  return { activeIndex, scrolledTo, follow };
}

describe('useLyricFollow', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.setSystemTime(new Date('2026-07-04T00:00:00Z'));
  });
  afterEach(() => vi.useRealTimers());

  it('starts auto-following active', () => {
    const { follow } = setup();
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('snaps to active line immediately on setup (immediate watch)', async () => {
    const { scrolledTo } = setup({ activeIndex: 5 });
    await nextTick();
    expect(scrolledTo).toEqual([5]);
  });

  it('auto-scrolls to the active line when activeIndex changes while following', async () => {
    const { activeIndex, scrolledTo } = setup({ activeIndex: 0 });
    await nextTick();
    scrolledTo.length = 0;
    activeIndex.value = 2;
    await nextTick();
    expect(scrolledTo).toEqual([2]);
  });

  it(`onUserScroll suspends auto-follow and resumes after ${IDLE_RESUME_MS}ms idle`, () => {
    const { follow } = setup({ now: () => 1000 });
    follow.onUserScroll();
    expect(follow.autoFollowing.value).toBe(false);
    expect(follow.manualScrollUntil.value).toBe(1000 + IDLE_RESUME_MS);

    vi.advanceTimersByTime(IDLE_RESUME_MS - 1);
    expect(follow.autoFollowing.value).toBe(false);

    vi.advanceTimersByTime(1);
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('resumeFollow immediately re-follows and scrolls to the active line', async () => {
    const { follow, activeIndex, scrolledTo } = setup({ activeIndex: 3 });
    follow.onUserScroll();
    expect(follow.autoFollowing.value).toBe(false);
    scrolledTo.length = 0;

    follow.resumeFollow();
    expect(follow.autoFollowing.value).toBe(true);
    expect(scrolledTo).toContain(activeIndex.value);
  });

  it('snapToActive forces follow and scrolls with default auto behavior', () => {
    const { follow, scrolledTo } = setup({ activeIndex: 4 });
    follow.onUserScroll();
    scrolledTo.length = 0;
    follow.snapToActive();
    expect(follow.autoFollowing.value).toBe(true);
    expect(scrolledTo).toContain(4);
  });

  it('resetForTrack resets follow state and only fires when the track key changes', () => {
    const { follow } = setup();
    follow.onUserScroll();
    expect(follow.autoFollowing.value).toBe(false);

    follow.resetForTrack('track-B');
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.manualScrollUntil.value).toBe(0);
    expect(follow.trackKey.value).toBe('track-B');

    follow.onUserScroll();
    expect(follow.autoFollowing.value).toBe(false);
    follow.resetForTrack('track-B');
    expect(follow.autoFollowing.value).toBe(false);

    follow.resetForTrack('track-C');
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.trackKey.value).toBe('track-C');
  });

  it('does not auto-scroll while auto-follow is suspended by user scroll', async () => {
    const { follow, activeIndex, scrolledTo } = setup({ activeIndex: 0 });
    follow.onUserScroll();
    scrolledTo.length = 0;

    activeIndex.value = 4;
    await nextTick();
    expect(scrolledTo).toEqual([]);
  });

  it('resetForTrack clears a pending idle resume so it cannot fire after the reset', () => {
    const { follow } = setup({ activeIndex: 5 });
    follow.onUserScroll();
    follow.resetForTrack('new-track');

    vi.advanceTimersByTime(5000);
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('does not auto-scroll when activeIndex becomes negative (guard: idx >= 0)', async () => {
    const { activeIndex, scrolledTo } = setup({ activeIndex: 2 });
    await nextTick();
    scrolledTo.length = 0;
    activeIndex.value = -1;
    await nextTick();
    expect(scrolledTo).toEqual([]);
  });

  it('repeated onUserScroll pushes out the idle timer; resumes after last scroll', () => {
    const { follow } = setup({ now: () => 1000 });
    follow.onUserScroll();
    vi.advanceTimersByTime(400);

    follow.onUserScroll();
    vi.advanceTimersByTime(400);
    expect(follow.autoFollowing.value).toBe(false);

    vi.advanceTimersByTime(IDLE_RESUME_MS);
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('resumeFollow clears the pending idle timer so it cannot fire later', () => {
    const { follow } = setup({ activeIndex: 3 });
    follow.onUserScroll();
    follow.resumeFollow();
    expect(follow.autoFollowing.value).toBe(true);

    vi.advanceTimersByTime(5000);
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.manualScrollUntil.value).toBe(0);
  });

  it('resumeFollow does not scroll when activeIndex is negative', () => {
    const { follow, scrolledTo } = setup({ activeIndex: -1 });
    follow.onUserScroll();
    scrolledTo.length = 0;

    follow.resumeFollow();
    expect(follow.autoFollowing.value).toBe(true);
    expect(scrolledTo).toEqual([]);
  });

  it('after resumeFollow, subsequent activeIndex changes auto-scroll again', async () => {
    const { follow, activeIndex, scrolledTo } = setup({ activeIndex: 0 });
    follow.onUserScroll();
    follow.resumeFollow();
    scrolledTo.length = 0;

    activeIndex.value = 7;
    await nextTick();
    expect(scrolledTo).toEqual([7]);
  });

  it('clears the idle timer when the composable scope is disposed (no late mutation)', () => {
    const scope = effectScope();
    let follow!: UseLyricFollowReturn;
    scope.run(() => {
      follow = useLyricFollow({
        activeIndex: ref(0),
        scrollToLine: () => {},
        now: () => 1000,
      });
    });
    follow.onUserScroll();
    expect(follow.autoFollowing.value).toBe(false);

    scope.stop();

    vi.advanceTimersByTime(5000);
    expect(follow.autoFollowing.value).toBe(false);
  });
});
