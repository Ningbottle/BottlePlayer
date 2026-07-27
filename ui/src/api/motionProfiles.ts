import type { SkinId } from './themeStore';

export interface TweenSpec {
  duration: number;
  ease: string;
  delay?: number;
  fromY?: number;
}

export interface MotionProfile {
  pageEnter: TweenSpec;
  pageLeave: TweenSpec;
  controlPress: TweenSpec;
  controlRelease: TweenSpec;
  cardEnter: TweenSpec & { stagger: number; maxItems: number };
  /** Turntable night: hero vinyl rotation. */
  vinyl: { enabled: boolean; spinSeconds: number; rampSeconds: number };
}

export type ProfileKey = 'pageEnter' | 'pageLeave' | 'controlPress' | 'controlRelease';

const auroraProfile: MotionProfile = {
  pageEnter: { duration: 0.56, ease: 'expo.out', fromY: 28 },
  pageLeave: { duration: 0.2, ease: 'power2.in' },
  controlPress: { duration: 0.08, ease: 'power2.out' },
  controlRelease: { duration: 0.58, ease: 'elastic.out(1.12, 0.42)' },
  cardEnter: { duration: 0.4, ease: 'back.out(1.5)', stagger: 0.04, maxItems: 12 },
  vinyl: { enabled: true, spinSeconds: 24, rampSeconds: 0.8 },
};

const newsprintProfile: MotionProfile = {
  pageEnter: { duration: 0.24, ease: 'power3.out', fromY: 8 },
  pageLeave: { duration: 0.16, ease: 'power2.in' },
  controlPress: { duration: 0.1, ease: 'power2.out' },
  controlRelease: { duration: 0.18, ease: 'power2.out' },
  cardEnter: { duration: 0.25, ease: 'power3.out', stagger: 0.03, maxItems: 20 },
  vinyl: { enabled: false, spinSeconds: 0, rampSeconds: 0 },
};

export function getMotionProfile(skinId: SkinId): MotionProfile {
  return skinId === 'aurora' ? auroraProfile : newsprintProfile;
}
