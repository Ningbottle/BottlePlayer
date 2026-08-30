/**
 * Neutral shared media port types. Must not import platform/, playback/,
 * app/, or features/ — this file is dependency-free by design (plan §3.1).
 *
 * C3: this is the single actual definition of PreparedAudioSource. Both
 * html5Backend.ts (Playback runtime) and platform/tauri/audioProxy.ts import
 * it from here, removing the Platform → Playback type dependency.
 */
export interface PreparedAudioSource {
  url: string;
  crossOriginSafe: boolean;
}
