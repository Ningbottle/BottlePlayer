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

  it('aurora ambient duration >= 5 seconds', () => {
    expect(getMotionProfile('aurora').ambient.duration).toBeGreaterThanOrEqual(5);
  });

  it('aurora ambient scale <= 1.015', () => {
    expect(getMotionProfile('aurora').ambient.scale).toBeLessThanOrEqual(1.015);
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
