/**
 * Web Audio EQ leaf extracted from playerStore.
 * Owns eqState, graph lifecycle, and Html5 backend EQ hooks.
 * playerStore remains the stable barrel for public exports.
 */
import { reactive } from 'vue';
import { WebAudioEq } from './webAudioEq';
import { prepareAudioSourceUrl } from './audioProxy';

export type PlayerEqStoreSlice = {
  eqEnabled: boolean;
  eqBands: number[];
  volume: number;
  audio: HTMLAudioElement | null;
};

const EQ_UNAVAILABLE_REASON =
  '当前音源直连播放，未经过本地音频处理链路，EQ 暂不可用。';
const EQ_DEGRADED_REASON = 'EQ 暂不可用，点击重试';

function getAudioSource(audio: HTMLAudioElement): string {
  return audio.getAttribute('src') || audio.currentSrc || audio.src || '';
}

function isLocalAudioProxySource(src: string): boolean {
  if (!src) return false;
  try {
    const url = new URL(src, window.location.href);
    return (
      url.protocol === 'http:'
      && url.hostname === '127.0.0.1'
      && url.pathname.startsWith('/audio/')
    );
  } catch {
    return false;
  }
}

/**
 * Factory keeps EQ free of a hard import on playerStore (avoids circular init).
 * Wire with createPlayerEq(() => playerStore) after playerStore exists.
 */
export function createPlayerEq(getStore: () => PlayerEqStoreSlice) {
  // Routes <audio> through a BiquadFilter chain. KuGou CDN has no CORS;
  // only proxy-backed 127.0.0.1 media is rerouted (see webAudioEq.ts).
  const webAudioEq = new WebAudioEq(() => {
    const Ctx =
      window.AudioContext
      || (window as unknown as { webkitAudioContext?: typeof AudioContext })
        .webkitAudioContext;
    return Ctx ? new Ctx() : null;
  });

  let currentEqSafeSource = '';

  const eqState = reactive({
    available: false,
    reason: EQ_UNAVAILABLE_REASON,
    retryFailCount: 0,
    retryDisabled: false,
  });

  function __resetWebAudioEqForTests() {
    webAudioEq.close();
  }

  function closeWebAudioEq() {
    webAudioEq.close();
  }

  function syncEqAvailabilityFromReroute() {
    eqState.available = webAudioEq.isRerouted;
    eqState.reason = eqState.available ? '' : EQ_UNAVAILABLE_REASON;
  }

  async function preparePlaybackAudioSourceUrl(url: string) {
    if (!getStore().eqEnabled) {
      return { url, crossOriginSafe: false };
    }
    return prepareAudioSourceUrl(url);
  }

  /** Build the long-lived worklet graph once at app startup (spec §5.1). */
  function initWebAudioEQ() {
    webAudioEq.init({
      enabled: getStore().eqEnabled,
      bands: getStore().eqBands,
      onDegraded: () => {
        eqState.available = false;
        eqState.reason = EQ_DEGRADED_REASON;
      },
      onRecovered: () => {
        syncEqAvailabilityFromReroute();
      },
    });
  }

  /** Post-play attach: captureStream → worklet (spec §5.2). Skips when not CORS-safe. */
  async function attachWebAudioEqSource(
    audio: HTMLAudioElement,
    crossOriginSafe = false,
    isCurrent: () => boolean = () => true,
  ) {
    if (!isCurrent()) return;
    currentEqSafeSource = crossOriginSafe ? getAudioSource(audio) : '';
    if (!crossOriginSafe) {
      eqState.available = false;
      eqState.reason = EQ_UNAVAILABLE_REASON;
      audio.volume = getStore().volume;
      return;
    }

    await webAudioEq.awaitReady();
    if (!isCurrent()) return;
    webAudioEq.attachSource(audio);
    if (!isCurrent()) return;
    syncEqAvailabilityFromReroute();
    if (!isCurrent()) return;
    setWebAudioEqVolume(getStore().volume);
  }

  function disconnectWebAudioEqSource() {
    currentEqSafeSource = '';
    webAudioEq.disconnectSource();
    syncEqAvailabilityFromReroute();
  }

  function setWebAudioEqVolume(vol: number) {
    webAudioEq.setVolume(vol);
  }

  function setWebAudioEqBand(index: number, gainDb: number) {
    webAudioEq.setBand(index, gainDb, getStore().eqEnabled);
  }

  function setWebAudioEqEnabled(enabled: boolean) {
    const store = getStore();
    webAudioEq.setEnabled(enabled, store.eqBands);
    if (!enabled) {
      webAudioEq.disconnectSource();
      if (store.audio) store.audio.volume = store.volume;
      syncEqAvailabilityFromReroute();
      return;
    }
    const audio = store.audio;
    const source = audio ? getAudioSource(audio) : '';
    const sourceSafe =
      !!source
      && (source === currentEqSafeSource || isLocalAudioProxySource(source));
    if (audio && !webAudioEq.isRerouted && sourceSafe) {
      void attachWebAudioEqSource(audio, true);
    } else {
      syncEqAvailabilityFromReroute();
    }
  }

  /** Resume the AudioContext after a user gesture (autoplay policy). */
  function resumeAudioContext() {
    void webAudioEq.resume().catch(() => {
      const store = getStore();
      if (store.audio && webAudioEq.isRerouted) {
        webAudioEq.enterDegradation(store.audio, store.volume);
      }
    });
  }

  /** Retry EQ after suspend degradation (spec §4.4, §6.3). */
  async function retryEq() {
    const store = getStore();
    if (eqState.retryDisabled || !store.audio) return;
    try {
      await webAudioEq.resume();
      webAudioEq.recoverFromDegradation(store.audio);
      eqState.available = true;
      eqState.reason = '';
      eqState.retryFailCount = 0;
    } catch {
      eqState.retryFailCount++;
      if (eqState.retryFailCount >= 3) {
        eqState.retryDisabled = true;
      }
    }
  }

  /**
   * Single owner of Html5 backend EQ hooks — dedupes the initEq branch
   * that previously lived inline in initPlayerBackend.
   */
  function makeBackendEqHooks() {
    return {
      prepareSourceUrl: preparePlaybackAudioSourceUrl,
      initEq: async (
        audio: HTMLAudioElement,
        crossOriginSafe: boolean,
        isCurrent: () => boolean,
      ) => {
        if (!isCurrent()) return;
        if (!getStore().eqEnabled) {
          if (!isCurrent()) return;
          currentEqSafeSource = crossOriginSafe ? getAudioSource(audio) : '';
          if (!isCurrent()) return;
          eqState.available = false;
          eqState.reason = EQ_UNAVAILABLE_REASON;
          if (!isCurrent()) return;
          audio.volume = getStore().volume;
          return;
        }
        await attachWebAudioEqSource(audio, crossOriginSafe, isCurrent);
      },
      disconnectEq: disconnectWebAudioEqSource,
      isEqRerouted: () => webAudioEq.isRerouted,
      setEqVolume: setWebAudioEqVolume,
    };
  }

  function resetRetryState() {
    eqState.retryFailCount = 0;
    eqState.retryDisabled = false;
  }

  return {
    eqState,
    __resetWebAudioEqForTests,
    closeWebAudioEq,
    initWebAudioEQ,
    attachWebAudioEqSource,
    disconnectWebAudioEqSource,
    setWebAudioEqVolume,
    setWebAudioEqBand,
    setWebAudioEqEnabled,
    resumeAudioContext,
    retryEq,
    makeBackendEqHooks,
    resetRetryState,
  };
}

export type PlayerEqApi = ReturnType<typeof createPlayerEq>;
