/**
 * Playback stress gate launcher.
 *
 * Runs the real PlaybackCommandCoordinator through Vitest (not a fake counter).
 * Does NOT claim 2h/24h soak results — only reports what actually ran.
 *
 * Usage (from ui/):
 *   node scripts/playback-stress.mjs
 *   node scripts/playback-stress.mjs --commands 1000
 *   node scripts/playback-stress.mjs --commands 100
 *
 * Env:
 *   PLAYBACK_STRESS_COMMANDS — override command count (default 1000)
 */

import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const uiRoot = path.resolve(__dirname, '..');

function parseArgs(argv) {
  const out = { commands: 1000 };
  for (let i = 2; i < argv.length; i++) {
    if (argv[i] === '--commands') out.commands = Number(argv[++i]) || 1000;
  }
  return out;
}

function main() {
  const args = parseArgs(process.argv);
  const started = Date.now();
  const env = {
    ...process.env,
    PLAYBACK_STRESS_COMMANDS: String(args.commands),
  };

  console.log(
    JSON.stringify({
      event: 'playback_stress_start',
      commands: args.commands,
      note: 'Not a 2h/24h soak — wall-clock of this run only',
    }),
  );

  const child = spawn(
    process.platform === 'win32' ? 'pnpm.cmd' : 'pnpm',
    [
      'exec',
      'vitest',
      'run',
      'src/api/__tests__/playbackStress.gate.test.ts',
      '--reporter=verbose',
    ],
    {
      cwd: uiRoot,
      env,
      stdio: 'inherit',
      shell: process.platform === 'win32',
    },
  );

  child.on('exit', (code) => {
    const wallClockMs = Date.now() - started;
    console.log(
      JSON.stringify({
        event: 'playback_stress_end',
        commands: args.commands,
        wallClockMs,
        exitCode: code ?? 1,
        soak2h: false,
        soak24h: false,
        note: 'Report only measured wallClockMs; long soaks not executed',
      }),
    );
    process.exit(code ?? 1);
  });
}

main();
