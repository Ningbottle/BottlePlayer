import { ref } from 'vue';

/** Shared reactive fullscreen flag. Imported by LyricView (sets it) and App.vue (reads it to hide shell). */
export const lyricFullscreen = ref(false);

export function setLyricFullscreen(value: boolean): void {
  lyricFullscreen.value = value;
}

/** Idempotent: any non-lyric route must not keep the shell hidden. */
export function clearLyricFullscreenUnlessOnLyric(isLyricRoute: boolean): void {
  if (!isLyricRoute && lyricFullscreen.value) {
    lyricFullscreen.value = false;
  }
}
