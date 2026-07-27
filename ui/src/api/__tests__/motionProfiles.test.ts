import { describe, it, expect } from 'vitest';
import { getMotionProfile, type MotionProfile } from '../motionProfiles';

describe('motionProfiles', () => {
  it('aurora controlRelease uses elastic.out', () => {
    expect(getMotionProfile('aurora').controlRelease.ease).toContain('elastic.out');
  });

  it('newsprint pageEnter uses power3.out', () => {
    expect(getMotionProfile('newsprint').pageEnter.ease).toBe('power3.out');
  });

  it('uses a longer expo entrance and a bounded jelly card entrance for Aurora', () => {
    const profile = getMotionProfile('aurora');
    expect(profile.pageEnter).toMatchObject({ duration: 0.56, ease: 'expo.out' });
    expect(profile.pageLeave).toMatchObject({ duration: 0.2, ease: 'power2.in' });
    expect(profile.cardEnter).toMatchObject({ duration: 0.4, stagger: 0.04, maxItems: 12 });
    expect(profile.cardEnter.ease).toContain('back.out');
  });

  it('newsprint page timings stay compact and serial-friendly', () => {
    const profile = getMotionProfile('newsprint');
    expect(profile.pageEnter).toMatchObject({ duration: 0.24, ease: 'power3.out' });
    expect(profile.pageLeave).toMatchObject({ duration: 0.16, ease: 'power2.in' });
  });

  it('keeps Aurora control release elastic while Newsprint stays non-elastic', () => {
    expect(getMotionProfile('aurora').controlRelease).toMatchObject({
      duration: 0.58,
      ease: 'elastic.out(1.12, 0.42)',
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

  it('keeps page-enter travel in the skin profile', () => {
    expect(getMotionProfile('aurora').pageEnter).toMatchObject({ fromY: 28 });
    expect(getMotionProfile('newsprint').pageEnter).toMatchObject({ fromY: 8 });
  });

  it('owns calmer Dock and Cover particle speeds in the Aurora profile', () => {
    const profile: MotionProfile = getMotionProfile('aurora');

    expect(profile.particles.dock.speed.playing).toBeLessThan(0.85);
    expect(profile.particles.dock.speed.paused).toBeLessThan(0.65);
    expect(profile.particles.cover.timeScale.playing).toBeLessThan(0.9);
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
