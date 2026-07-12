import { describe, it, expect } from 'vitest';
import { getMotionProfile } from '../motionProfiles';

describe('motionProfiles', () => {
  it('aurora controlRelease uses elastic.out', () => {
    expect(getMotionProfile('aurora').controlRelease.ease).toContain('elastic.out');
  });

  it('newsprint pageEnter uses power3.out', () => {
    expect(getMotionProfile('newsprint').pageEnter.ease).toBe('power3.out');
  });

  it('newsprint ambient is disabled', () => {
    expect(getMotionProfile('newsprint').ambient.enabled).toBe(false);
  });

  it('aurora ambient duration is bounded for jelly stage drift', () => {
    expect(getMotionProfile('aurora').ambient.duration).toBe(3);
  });

  it('aurora ambient scale <= 1.015', () => {
    expect(getMotionProfile('aurora').ambient.scale).toBeLessThanOrEqual(1.015);
  });

  it('uses a longer expo entrance and a bounded jelly card entrance for Aurora', () => {
    const profile = getMotionProfile('aurora');
    expect(profile.pageEnter).toMatchObject({ duration: 0.52, ease: 'expo.out' });
    expect(profile.pageLeave).toMatchObject({ duration: 0.2, ease: 'power2.in' });
    expect(profile.cardEnter).toMatchObject({ duration: 0.36, stagger: 0.04, maxItems: 12 });
    expect(profile.cardEnter.ease).toContain('back.out');
  });

  it('newsprint page timings remain serial-friendly and unchanged', () => {
    const profile = getMotionProfile('newsprint');
    expect(profile.pageEnter).toMatchObject({ duration: 0.3, ease: 'power3.out' });
    expect(profile.pageLeave).toMatchObject({ duration: 0.16, ease: 'power2.in' });
  });

  it('keeps Aurora control release elastic without changing Newsprint', () => {
    expect(getMotionProfile('aurora').controlRelease).toMatchObject({ duration: 0.42 });
    expect(getMotionProfile('aurora').controlRelease.ease).toContain('elastic.out');
    expect(getMotionProfile('newsprint').controlRelease.ease).toBe('power2.out');
  });

  it('newsprint has no elastic in any ease', () => {
    const profile = getMotionProfile('newsprint');
    const eases = [
      profile.pageEnter.ease,
      profile.pageLeave.ease,
      profile.controlPress.ease,
      profile.controlRelease.ease,
      profile.cardEnter.ease,
    ];
    for (const ease of eases) {
      expect(ease).not.toContain('elastic');
    }
  });

  it('aurora pageEnter uses expo.out', () => {
    expect(getMotionProfile('aurora').pageEnter.ease).toBe('expo.out');
  });

  it('aurora cardEnter uses back.out', () => {
    expect(getMotionProfile('aurora').cardEnter.ease).toContain('back.out');
  });

  it('cardEnter has stagger and maxItems', () => {
    const profile = getMotionProfile('aurora');
    expect(profile.cardEnter.stagger).toBeGreaterThan(0);
    expect(profile.cardEnter.maxItems).toBeGreaterThan(0);
  });
});
