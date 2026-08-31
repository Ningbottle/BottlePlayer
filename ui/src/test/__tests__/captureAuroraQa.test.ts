import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

describe('capture-aurora-qa output boundary', () => {
  it('rejects an --out-dir that escapes the ui directory before launching Chromium', () => {
    const uiRoot = resolve(process.cwd());
    const scriptPath = resolve(uiRoot, 'scripts/capture-aurora-qa.mjs');
    const result = spawnSync(
      process.execPath,
      [scriptPath, '--out-dir=../outside-ui'],
      {
        cwd: uiRoot,
        encoding: 'utf8',
        env: {
          ...process.env,
          AURORA_QA_URL: 'invalid://capture-must-not-reach-browser',
        },
        timeout: 15_000,
      },
    );

    expect(result.status).not.toBe(0);
    expect(`${result.stdout}\n${result.stderr}`).toContain(
      'capture_out_dir_must_stay_inside_ui',
    );
  });
});
