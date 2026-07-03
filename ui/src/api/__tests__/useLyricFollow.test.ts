import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { ref, nextTick } from 'vue';
import { useLyricFollow } from '../useLyricFollow';

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

  it('auto-scrolls to the active line when activeIndex changes while following', async () => {
    const { activeIndex, scrolledTo } = setup({ activeIndex: 0 });
    activeIndex.value = 2;
    await nextTick();
    expect(scrolledTo).toEqual([2]);
  });

  it('onUserScroll suspends auto-follow and resumes after 3s idle', () => {
    const { follow } = setup({ now: () => 1000 });
    follow.onUserScroll();
    expect(follow.autoFollowing.value).toBe(false);
    expect(follow.manualScrollUntil.value).toBe(1000 + 3000);

    vi.advanceTimersByTime(2999);
    expect(follow.autoFollowing.value).toBe(false);

    vi.advanceTimersByTime(1);
    expect(follow.autoFollowing.value).toBe(true);
  });

  it('resumeFollow immediately re-follows and scrolls to the active line', async () => {
    const { follow, activeIndex, scrolledTo } = setup({ activeIndex: 3 });
    follow.onUserScroll(); // suspend
    expect(follow.autoFollowing.value).toBe(false);
    scrolledTo.length = 0;

    follow.resumeFollow();
    expect(follow.autoFollowing.value).toBe(true);
    expect(scrolledTo).toContain(activeIndex.value);
  });

  it('resetForTrack resets follow state and only fires when the track key changes', () => {
    const { follow } = setup();
    follow.onUserScroll(); // suspend
    expect(follow.autoFollowing.value).toBe(false);

    follow.resetForTrack('track-B');
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.manualScrollUntil.value).toBe(0);
    expect(follow.trackKey.value).toBe('track-B');

    // Same key again — must NOT reset (so a metadata refresh on the same
    // track doesn't wipe an in-progress manual-scroll suspension).
    follow.onUserScroll();
    expect(follow.autoFollowing.value).toBe(false);
    follow.resetForTrack('track-B');
    expect(follow.autoFollowing.value).toBe(false);

    // A different key DOES reset.
    follow.resetForTrack('track-C');
    expect(follow.autoFollowing.value).toBe(true);
    expect(follow.trackKey.value).toBe('track-C');
  });

  it('does not auto-scroll while auto-follow is suspended by user scroll', async () => {
    const { follow, activeIndex, scrolledTo } = setup({ activeIndex: 0 });
    follow.onUserScroll(); // suspend
    scrolledTo.length = 0;

    activeIndex.value = 4;
    await nextTick();
    expect(scrolledTo).toEqual([]); // no scroll — user is browsing freely
  });

  it('resetForTrack clears a pending idle resume so it cannot fire after the reset', () => {
    const { follow } = setup({ activeIndex: 5 });
    follow.onUserScroll(); // schedules a 3s resume
    follow.resetForTrack('new-track');

    // Advance well past the idle window — the stale timer must not have fired
    // (it was cleared; autoFollowing stays true from the reset, not from a late timer).
    vi.advanceTimersByTime(5000);
    expect(follow.autoFollowing.value).toBe(true);
  });
});
