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
  pageEnter: { duration: 0.52, ease: 'expo.out' },
  pageLeave: { duration: 0.2, ease: 'power2.in' },
  controlPress: { duration: 0.1, ease: 'power2.out' },
  controlRelease: { duration: 0.42, ease: 'elastic.out(1, 0.55)' },
  cardEnter: { duration: 0.36, ease: 'back.out(1.25)', stagger: 0.04, maxItems: 12 },
  ambient: { enabled: true, duration: 3, scale: 1.01 },
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
