import { describe, it, expect } from 'vitest';
import { getMotionProfile } from '../motionProfiles';

describe('motionProfiles', () => {
  it('aurora controlRelease settles without overshoot', () => {
    expect(getMotionProfile('aurora').controlRelease.ease).toBe('power2.out');
  });

  it('newsprint pageEnter uses power3.out', () => {
    expect(getMotionProfile('newsprint').pageEnter.ease).toBe('power3.out');
  });

  it('keeps Aurora entrances compact with no elastic or back overshoot', () => {
    const profile = getMotionProfile('aurora');
    expect(profile.pageEnter).toMatchObject({ duration: 0.28, ease: 'power2.out' });
    expect(profile.pageLeave).toMatchObject({ duration: 0.16, ease: 'power2.in' });
    expect(profile.cardEnter).toMatchObject({ duration: 0.28, stagger: 0.025, maxItems: 12 });
    expect(profile.cardEnter.ease).toBe('power2.out');
  });

  it('newsprint page timings stay compact and serial-friendly', () => {
    const profile = getMotionProfile('newsprint');
    expect(profile.pageEnter).toMatchObject({ duration: 0.24, ease: 'power3.out' });
    expect(profile.pageLeave).toMatchObject({ duration: 0.16, ease: 'power2.in' });
  });

  it('keeps both skins control release non-elastic', () => {
    expect(getMotionProfile('aurora').controlRelease).toMatchObject({
      duration: 0.18,
      ease: 'power2.out',
    });
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

  it('aurora pageEnter uses power2.out', () => {
    expect(getMotionProfile('aurora').pageEnter.ease).toBe('power2.out');
  });

  it('aurora cardEnter uses power2.out', () => {
    expect(getMotionProfile('aurora').cardEnter.ease).toBe('power2.out');
  });

  it('cardEnter has stagger and maxItems', () => {
    const profile = getMotionProfile('aurora');
    expect(profile.cardEnter.stagger).toBeGreaterThan(0);
    expect(profile.cardEnter.maxItems).toBeGreaterThan(0);
  });

  it('keeps page-enter travel in the skin profile', () => {
    expect(getMotionProfile('aurora').pageEnter).toMatchObject({ fromY: 12 });
    expect(getMotionProfile('newsprint').pageEnter).toMatchObject({ fromY: 8 });
  });

  it('owns the turntable vinyl spin profile', () => {
    expect(getMotionProfile('aurora').vinyl).toEqual({
      enabled: true,
      spinSeconds: 24,
      rampSeconds: 0.8,
    });
    expect(getMotionProfile('newsprint').vinyl.enabled).toBe(false);
  });
});
