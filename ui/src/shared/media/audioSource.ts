/**
 * Neutral shared media port types. Must not import platform/, playback/,
 * app/, or features/ — this file is dependency-free by design (plan §3.1).
 *
 * C1 scope: definition only. html5Backend.ts and audioProxy.ts keep using the
 * original in-replica type until C3 moves them onto this shared import.
 */
export interface PreparedAudioSource {
  url: string;
  crossOriginSafe: boolean;
}
