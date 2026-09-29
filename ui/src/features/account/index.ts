export { default as LoginView } from './LoginView.vue';

export {
  userStore,
  formatVipClaimFailure,
  ensureVipDeviceReady,
  checkLoginStatus,
  claimVip,
  claimVipViaRoute,
  claimDayVipConceptCandidate,
  isDayConceptCandidateUiEnabled,
  logoutLocal,
  refreshLiveVip,
  startVipClock,
  stopVipClock,
  type UserState,
  type VipDeviceResult,
  type VipClaimRoute,
} from './userStore';

export {
  resolveVip,
  parseVipEndTime,
  liveVipView,
  type VipResolution,
  type VipSnapshot,
  type LiveVipView,
} from './vipResolver';

export {
  configureAccountEffects,
  notifyAccountReady,
  notifyAccountCleared,
  notifyLocalLogout,
  type AccountEffects,
} from './accountEffects';

export {
  registerDevice,
  fetchUserDetail,
  fetchVipDetail,
  claimDailyVipSong,
  claimYouthListenSong,
  claimYouthVipAd,
  fetchQrKey,
  checkQrStatus,
  logoutAuth,
  type DeviceRegisterData,
  type DeviceRegisterResponse,
  type UserDetailData,
  type UserDetailResponse,
  type QrKeyData,
  type QrKeyResponse,
  type QrCheckData,
  type QrCheckResponse,
  type YouthListenSongResponse,
  type YouthVipAdResponse,
  type LogoutResponse,
} from './accountGateway';
