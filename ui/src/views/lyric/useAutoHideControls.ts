import {
  getCurrentScope,
  onScopeDispose,
  readonly,
  ref,
  watch,
  type Ref,
} from 'vue';

export interface AutoHideControls {
  visible: Readonly<Ref<boolean>>;
  onPointerMove(event: PointerEvent): void;
  onFocusIn(): void;
  onFocusOut(): void;
  dispose(): void;
}

export interface AutoHideControlsOptions {
  active: Readonly<Ref<boolean>>;
  onEscape: () => void;
  idleMs?: number;
}

const DEFAULT_IDLE_MS = 1_800;

export function useAutoHideControls(options: AutoHideControlsOptions): AutoHideControls {
  const visible = ref(false);
  const idleMs = options.idleMs ?? DEFAULT_IDLE_MS;
  let focusWithin = false;
  let disposed = false;
  let idleTimer: ReturnType<typeof globalThis.setTimeout> | null = null;

  function clearIdleTimer(): void {
    if (idleTimer === null) return;
    globalThis.clearTimeout(idleTimer);
    idleTimer = null;
  }

  function armIdleTimer(): void {
    clearIdleTimer();
    if (disposed || !options.active.value || focusWithin) return;
    idleTimer = globalThis.setTimeout(() => {
      idleTimer = null;
      if (!disposed && options.active.value && !focusWithin) visible.value = false;
    }, idleMs);
  }

  function reveal(): void {
    if (disposed || !options.active.value) return;
    visible.value = true;
    armIdleTimer();
  }

  function onPointerMove(_event: PointerEvent): void {
    reveal();
  }

  function onFocusIn(): void {
    if (disposed || !options.active.value) return;
    focusWithin = true;
    clearIdleTimer();
    visible.value = true;
  }

  function onFocusOut(): void {
    if (disposed) return;
    focusWithin = false;
    if (!options.active.value) {
      visible.value = false;
      clearIdleTimer();
      return;
    }
    visible.value = true;
    armIdleTimer();
  }

  function onKeydown(event: KeyboardEvent): void {
    if (disposed || event.key !== 'Escape' || !options.active.value) return;
    options.onEscape();
  }

  const stopActiveWatch = watch(
    options.active,
    (active) => {
      focusWithin = false;
      clearIdleTimer();
      visible.value = active;
      if (active) armIdleTimer();
    },
    { immediate: true },
  );

  window.addEventListener('keydown', onKeydown);

  function dispose(): void {
    if (disposed) return;
    disposed = true;
    clearIdleTimer();
    stopActiveWatch();
    window.removeEventListener('keydown', onKeydown);
  }

  if (getCurrentScope()) onScopeDispose(dispose);

  return {
    visible: readonly(visible),
    onPointerMove,
    onFocusIn,
    onFocusOut,
    dispose,
  };
}
