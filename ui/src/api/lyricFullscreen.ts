import { ref } from 'vue';

/** Shared reactive fullscreen flag. Imported by LyricView (sets it) and App.vue (reads it to hide shell). */
export const lyricFullscreen = ref(false);

export function setLyricFullscreen(value: boolean): void {
  lyricFullscreen.value = value;
}
