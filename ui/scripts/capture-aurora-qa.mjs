import { chromium } from 'playwright';
import { mkdir, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const uiRoot = path.resolve(__dirname, '..');
const outPrefix = '--out-dir=';
const outArg = process.argv.find((arg) => arg.startsWith(outPrefix));
const outDir = outArg
  ? path.resolve(uiRoot, outArg.slice(outPrefix.length))
  : path.join(uiRoot, 'design-qa-captures');
const relativeOut = path.relative(uiRoot, outDir);
if (relativeOut.startsWith('..') || path.isAbsolute(relativeOut)) {
  throw new Error('capture_out_dir_must_stay_inside_ui');
}
const baseUrl = process.env.AURORA_QA_URL || 'http://127.0.0.1:5173/';

const shots = [
  { name: 'aurora-home-1586x1024-dark.png', width: 1586, height: 1024, mode: 'dark', reduced: false },
  { name: 'aurora-home-1586x1024-light.png', width: 1586, height: 1024, mode: 'light', reduced: false },
  { name: 'aurora-home-1440x900-dark.png', width: 1440, height: 900, mode: 'dark', reduced: false },
  { name: 'aurora-home-1280x720-dark.png', width: 1280, height: 720, mode: 'dark', reduced: false },
  { name: 'aurora-home-900-dark.png', width: 900, height: 720, mode: 'dark', reduced: false },
  { name: 'aurora-home-1586x1024-dark-reduced-motion.png', width: 1586, height: 1024, mode: 'dark', reduced: true },
];

await mkdir(outDir, { recursive: true });

const browser = await chromium.launch({ headless: true });

const manifest = [];

for (const shot of shots) {
  const context = await browser.newContext({
    viewport: { width: shot.width, height: shot.height },
    deviceScaleFactor: 1,
    reducedMotion: shot.reduced ? 'reduce' : 'no-preference',
    colorScheme: shot.mode === 'dark' ? 'dark' : 'light',
  });
  const page = await context.newPage();
  page.on('console', (msg) => {
    if (msg.type() === 'error') console.error('[page]', msg.text());
  });

  await page.addInitScript(({ mode }) => {
    try {
      localStorage.setItem('tweak_skin', 'aurora');
      localStorage.setItem('tweak_mode', mode);
    } catch {}
  }, { mode: shot.mode });

  await page.goto(baseUrl, { waitUntil: 'networkidle', timeout: 60000 });
  await page.evaluate(({ mode }) => {
    const root = document.documentElement;
    root.setAttribute('data-skin', 'aurora');
    root.setAttribute('data-mode', mode);
    document.body?.setAttribute('data-skin', 'aurora');
    document.body?.setAttribute('data-mode', mode);
  }, { mode: shot.mode });

  // Give Vue a beat to paint
  await page.waitForTimeout(800);

  const filePath = path.join(outDir, shot.name);
  await page.screenshot({ path: filePath, fullPage: false });

  const observed = await page.evaluate(() => {
    const shell = document.querySelector('[data-shell="aurora"]');
    const layout = shell?.getAttribute('data-layout') || '';
    const titlebar = document.querySelector('[data-shell="aurora"] .titlebar');
    const topbar = document.querySelector('[data-shell="aurora"] .shell-topbar');
    const rail = document.querySelector('[data-test="queue-rail"]');
    const stage = document.querySelector('[data-test="aurora-stage"]');
    const consoleEl = document.querySelector('[data-test="aurora-player-console"]');
    const progress = document.querySelector('[data-test="aurora-player-progress"]');
    const cs = (el) => (el ? getComputedStyle(el) : null);
    const borderBottom = titlebar ? cs(titlebar).borderBottomWidth : 'n/a';
    const topbarBorder = topbar ? cs(topbar).borderBottomWidth : 'n/a';
    const railDisplay = rail ? cs(rail).display : 'missing';
    const railRows = rail ? rail.querySelectorAll('[data-test^="queue-track-"]').length : 0;
    const accent = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
    const appBg = getComputedStyle(document.documentElement).getPropertyValue('--app-bg').trim();
    const progressFill = getComputedStyle(document.documentElement).getPropertyValue('--progress-fill').trim();
    const progressTrack = getComputedStyle(document.documentElement).getPropertyValue('--progress-track').trim();
    return {
      layout,
      hasStage: !!stage,
      hasConsole: !!consoleEl,
      hasProgress: !!progress,
      borderBottom,
      topbarBorder,
      railDisplay,
      railRows,
      accent,
      appBg,
      progressFill,
      progressTrack,
      wordmark: document.querySelector('.titlebar-logo')?.textContent?.replace(/\s+/g, ' ').trim() || '',
    };
  });

  manifest.push({
    file: path.relative(path.join(__dirname, '..'), filePath).replace(/\\/g, '/'),
    viewport: `${shot.width} × ${shot.height}`,
    mode: shot.mode,
    reducedMotion: shot.reduced,
    observed,
  });

  await context.close();
  console.log('captured', shot.name, JSON.stringify(observed));
}

await writeFile(path.join(outDir, 'manifest.json'), JSON.stringify(manifest, null, 2), 'utf8');
await browser.close();
console.log('done', outDir);
