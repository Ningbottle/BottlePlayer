import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { describe, expect, it } from 'vitest';

function readJson(path: string) {
  return JSON.parse(readFileSync(resolve(process.cwd(), path), 'utf8'));
}

function readText(path: string) {
  return readFileSync(resolve(process.cwd(), path), 'utf8');
}

describe('release security configuration', () => {
  it('enables a restrictive production CSP and explicitly selects capabilities', () => {
    const config = readJson('src-tauri/tauri.conf.json');
    const security = config.app.security;

    expect(security.capabilities).toEqual(['default']);
    expect(security.csp).toBeTypeOf('object');
    expect(security.csp['default-src']).toContain("'self'");
    expect(security.csp['object-src']).toBe("'none'");
    expect(security.csp['base-uri']).toBe("'none'");
    expect(security.csp['connect-src']).toContain('ipc:');
    expect(security.csp['media-src']).toContain('http://127.0.0.1:*');
    expect(security.csp['worker-src']).toContain('blob:');
    expect(security.csp['script-src']).not.toContain("'unsafe-eval'");
    expect(security.devCsp).toBeTypeOf('object');
  });

  it('grants only the Tauri commands used by the main window', () => {
    const capability = readJson('src-tauri/capabilities/default.json');
    const permissionIds = capability.permissions.map((permission: string | { identifier: string }) =>
      typeof permission === 'string' ? permission : permission.identifier,
    );

    expect(permissionIds).not.toContain('core:default');
    expect(permissionIds).not.toContain('opener:default');
    expect(permissionIds).not.toContain('updater:default');
    expect(permissionIds).not.toContain('process:default');
    expect(permissionIds).toEqual(expect.arrayContaining([
      'core:event:allow-listen',
      'core:event:allow-unlisten',
      'core:window:allow-start-dragging',
      'core:window:allow-minimize',
      'core:window:allow-toggle-maximize',
      'core:window:allow-close',
      'opener:allow-open-url',
      'updater:allow-check',
      'updater:allow-download-and-install',
      'process:allow-restart',
    ]));

    const opener = capability.permissions.find(
      (permission: string | { identifier: string }) =>
        typeof permission === 'object' && permission.identifier === 'opener:allow-open-url',
    );
    expect(opener).toMatchObject({
      allow: [{ url: 'https://m.kugou.com/*' }],
    });
  });

  it('does not compile the unused shell plugin and contains release metadata', () => {
    const cargo = readText('src-tauri/Cargo.toml');
    const rust = readText('src-tauri/src/lib.rs');
    const pkg = readJson('package.json');

    expect(cargo).not.toContain('tauri-plugin-shell');
    expect(rust).not.toContain('tauri_plugin_shell');
    expect(cargo).not.toContain('authors = ["you"]');
    expect(cargo).not.toContain('description = "A Tauri App"');
    expect(pkg.description).toBeTruthy();
    expect(pkg.license).toBe('MIT');
  });

  it('runs verification before the release action receives signing secrets', () => {
    const workflow = readText('../.github/workflows/release.yml');
    const verifyIndex = workflow.indexOf('name: Verify release');
    const releaseActionIndex = workflow.indexOf('uses: tauri-apps/tauri-action');

    expect(verifyIndex).toBeGreaterThan(-1);
    expect(releaseActionIndex).toBeGreaterThan(verifyIndex);
    expect(workflow).toContain('pnpm test');
    expect(workflow).toContain('pnpm build');
    expect(workflow).toMatch(/\bcargo test\s*$/m);
    expect(workflow).toContain('cargo clippy --all-targets -- -D warnings');
  });

  it('pins every third-party release action to an immutable commit', () => {
    const workflow = readText('../.github/workflows/release.yml');
    const actionRefs = [...workflow.matchAll(/uses:\s*[^\s@]+@([^\s#]+)/g)].map((match) => match[1]);

    expect(actionRefs.length).toBeGreaterThan(0);
    for (const actionRef of actionRefs) {
      expect(actionRef).toMatch(/^[0-9a-f]{40}$/);
    }
  });

  it('uses bash for verify steps so native-command failures propagate', () => {
    const workflow = readText('../.github/workflows/release.yml');
    const verifySteps = ['Verify native core', 'Verify release', 'Verify Rust bridge'];
    for (const stepName of verifySteps) {
      const idx = workflow.indexOf(`name: ${stepName}`);
      expect(idx).toBeGreaterThan(-1);
      const rest = workflow.slice(idx);
      const nextStep = rest.search(/\n      - (?:name|uses):/);
      const block = nextStep === -1 ? rest : rest.slice(0, nextStep);
      expect(block).toMatch(/shell:\s*bash/);
      expect(block).not.toMatch(/continue-on-error:\s*true/i);
    }
  });

  it('uses bash for Build C++ Release step so cmake commands propagate failures', () => {
    const workflow = readText('../.github/workflows/release.yml');
    const idx = workflow.indexOf('name: Build C++ Release');
    expect(idx).toBeGreaterThan(-1);
    const rest = workflow.slice(idx);
    const nextStep = rest.search(/\n      - (?:name|uses):/);
    const block = nextStep === -1 ? rest : rest.slice(0, nextStep);
    expect(block).toMatch(/shell:\s*bash/);
  });

  it('runs ctest as part of native verification', () => {
    const workflow = readText('../.github/workflows/release.yml');
    const idx = workflow.indexOf('name: Verify native core');
    expect(idx).toBeGreaterThan(-1);
    const rest = workflow.slice(idx);
    const nextStep = rest.search(/\n      - (?:name|uses):/);
    const block = nextStep === -1 ? rest : rest.slice(0, nextStep);
    expect(block).toContain('ctest');
  });
});
