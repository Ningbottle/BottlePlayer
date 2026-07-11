import type { SkinId } from './themeStore';

export interface TweenSpec {
  duration: number;
  ease: string;
  delay?: number;
}

export interface MotionProfile {
  pageEnter: TweenSpec;
  pageLeave: TweenSpec;
  controlPress: TweenSpec;
  controlRelease: TweenSpec;
  cardEnter: TweenSpec & { stagger: number; maxItems: number };
  ambient: { enabled: boolean; duration: number; scale: number };
}

export type ProfileKey = 'pageEnter' | 'pageLeave' | 'controlPress' | 'controlRelease';

const auroraProfile: MotionProfile = {
  pageEnter: { duration: 0.34, ease: 'expo.out' },
  pageLeave: { duration: 0.18, ease: 'power2.in' },
  controlPress: { duration: 0.1, ease: 'power2.out' },
  controlRelease: { duration: 0.4, ease: 'elastic.out(1, 0.5)' },
  cardEnter: { duration: 0.3, ease: 'back.out(1.35)', stagger: 0.04, maxItems: 20 },
  ambient: { enabled: true, duration: 6, scale: 1.015 },
};

const newsprintProfile: MotionProfile = {
  pageEnter: { duration: 0.3, ease: 'power3.out' },
  pageLeave: { duration: 0.16, ease: 'power2.in' },
  controlPress: { duration: 0.1, ease: 'power2.out' },
  controlRelease: { duration: 0.18, ease: 'power2.out' },
  cardEnter: { duration: 0.25, ease: 'power3.out', stagger: 0.03, maxItems: 20 },
  ambient: { enabled: false, duration: 0, scale: 1 },
};

export function getMotionProfile(skinId: SkinId): MotionProfile {
  return skinId === 'aurora' ? auroraProfile : newsprintProfile;
}
