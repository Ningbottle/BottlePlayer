import { describe, it, expect } from 'vitest';
import { readFileSync, writeFileSync, mkdtempSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';
import { execSync } from 'node:child_process';

describe('sync-version', () => {
  it('writes tauri.conf.json version into package.json', () => {
    const dir = mkdtempSync(join(tmpdir(), 'sv-'));
    // minimal tauri.conf.json
    writeFileSync(join(dir, 'tauri.conf.json'), JSON.stringify({ version: '2.0.0' }));
    // minimal package.json
    writeFileSync(join(dir, 'package.json'), JSON.stringify({ name: 'test', version: '1.0.0' }));

    const script = join(process.cwd(), 'scripts', 'sync-version.mjs');
    execSync(`node "${script}" --root "${dir}"`, { cwd: process.cwd() });

    const updated = JSON.parse(readFileSync(join(dir, 'package.json'), 'utf-8'));
    expect(updated.version).toBe('2.0.0');
  });
});
