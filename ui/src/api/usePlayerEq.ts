/**
 * Web Audio EQ leaf extracted from playerStore.
 * Owns eqState, graph lifecycle, and Html5 backend EQ hooks.
 * playerStore remains the stable barrel for public exports.
 */
import { reactive } from 'vue';
import { WebAudioEq } from './webAudioEq';
import { prepareAudioSourceUrl } from './audioProxy';
import { playbackDiagnostics } from './playbackDiagnostics';

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

  function restoreElementVolume(audio: HTMLAudioElement) {
    audio.volume = getStore().volume;
  }

  function recordEqEvent(
    phase: 'start' | 'ok' | 'fail' | 'noop',
    detail: string,
  ) {
    playbackDiagnostics.recordEvent({
      kind: 'eq',
      phase,
      detail,
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
      restoreElementVolume(audio);
      recordEqEvent('noop', `cors_unsafe volume=${getStore().volume}`);
      return;
    }

    await webAudioEq.awaitReady();
    if (!isCurrent()) return;

    const volumeBefore = audio.volume;
    try {
      await webAudioEq.resume();
    } catch (e) {
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      restoreElementVolume(audio);
      recordEqEvent(
        'fail',
        `resume_reject ctx=${webAudioEq.contextState} volume_before=${volumeBefore} volume_after=${audio.volume} err=${e instanceof Error ? e.message : String(e)}`,
      );
      return;
    }
    if (!isCurrent()) return;
    if (webAudioEq.contextState !== 'running') {
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      restoreElementVolume(audio);
      recordEqEvent('fail', `ctx_not_running ctx=${webAudioEq.contextState} volume=${audio.volume}`);
      return;
    }

    const attached = webAudioEq.attachSource(audio, getStore().volume);
    const leaseId = webAudioEq.currentLeaseId;
    if (!attached) {
      restoreElementVolume(audio);
      eqState.available = false;
      eqState.reason = EQ_DEGRADED_REASON;
      recordEqEvent('fail', `attach_false lease=${leaseId} volume=${audio.volume}`);
      return;
    }
    if (!isCurrent()) {
      webAudioEq.releaseLease(leaseId);
      recordEqEvent('noop', `stale_after_attach released_lease=${leaseId}`);
      return;
    }
    syncEqAvailabilityFromReroute();
    if (!isCurrent()) {
      webAudioEq.releaseLease(leaseId);
      recordEqEvent('noop', `stale_after_sync released_lease=${leaseId}`);
      return;
    }
    setWebAudioEqVolume(getStore().volume);
    recordEqEvent(
      'ok',
      `attached lease=${leaseId} proxy=${isLocalAudioProxySource(getAudioSource(audio))} ctx=${webAudioEq.contextState} volume_before=${volumeBefore} volume_after=${audio.volume}`,
    );
  }

  function disconnectWebAudioEqSource() {
    const leaseId = webAudioEq.currentLeaseId;
    currentEqSafeSource = '';
    webAudioEq.disconnectSource();
    syncEqAvailabilityFromReroute();
    recordEqEvent('ok', `disconnect lease=${leaseId} ctx=${webAudioEq.contextState}`);
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
    void webAudioEq.resume().then(() => {
      if (webAudioEq.contextState !== 'running') {
        const store = getStore();
        if (store.audio) {
          eqState.available = false;
          eqState.reason = EQ_DEGRADED_REASON;
          store.audio.volume = store.volume;
          if (webAudioEq.isRerouted) {
            webAudioEq.enterDegradation(store.audio, store.volume);
          }
        }
      }
    }).catch(() => {
      const store = getStore();
      if (store.audio) {
        eqState.available = false;
        eqState.reason = EQ_DEGRADED_REASON;
        store.audio.volume = store.volume;
        if (webAudioEq.isRerouted) {
          webAudioEq.enterDegradation(store.audio, store.volume);
        }
      }
    });
  }

  /** Retry EQ after suspend degradation (spec §4.4, §6.3). */
  async function retryEq() {
    const store = getStore();
    if (eqState.retryDisabled || !store.audio) return;
    try {
      await webAudioEq.resume();
      if (webAudioEq.contextState !== 'running') {
        throw new Error('AudioContext not running');
      }
      const recovered = webAudioEq.recoverFromDegradation(store.audio, store.volume);
      if (!recovered) throw new Error('eq recover failed');
      eqState.available = true;
      eqState.reason = '';
      eqState.retryFailCount = 0;
    } catch {
      store.audio.volume = store.volume;
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
