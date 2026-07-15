/**
 * verify-playerbar-layout.mjs
 *
 * Geometry verification script for player bar progress bar visibility.
 * Asserts that progress bar, time labels, and track are fully visible
 * and not clipped by any ancestor overflow at specified viewport sizes.
 *
 * Starts Vite dev server automatically, runs checks, then shuts down.
 * Exit code 0 = all checks pass; non-zero = layout contract violation.
 */
import { chromium } from 'playwright';
import { mkdir, writeFile } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import os from 'node:os';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const VITE_PORT = 5190;
const VITE_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const externalBaseUrl = process.env.PLAYERBAR_QA_URL;
const baseUrl = externalBaseUrl || `http://localhost:${VITE_PORT}/`;
const outDir = process.env.PLAYERBAR_QA_OUT || path.join(os.tmpdir(), 'bottlemusic-playerbar-qa');

const VIEWPORTS = [
  { width: 1332, height: 862, label: '1332x862' },
  { width: 1280, height: 720, label: '1280x720' },
  { width: 1440, height: 900, label: '1440x900' },
];

const SKIN_MODES = [
  { skin: 'aurora', mode: 'dark' },
  { skin: 'aurora', mode: 'light' },
  { skin: 'newsprint', mode: 'dark' },
  { skin: 'newsprint', mode: 'light' },
];

const TOLERANCE_PX = 1;

/** Intersection of two rectangles. */
function intersect(a, b) {
  const left = Math.max(a.x, b.x);
  const top = Math.max(a.y, b.y);
  const right = Math.min(a.x + a.width, b.x + b.width);
  const bottom = Math.min(a.y + a.height, b.y + b.height);
  if (right <= left || bottom <= top) return { x: 0, y: 0, width: 0, height: 0 };
  return { x: left, y: top, width: right - left, height: bottom - top };
}

/** Collect comprehensive geometry data for the player bar area. */
async function collectGeometry(page) {
  return await page.evaluate(() => {
    const cs = (el) => el ? getComputedStyle(el) : null;
    const rect = (el) => {
      if (!el) return null;
      const r = el.getBoundingClientRect();
      return { x: r.x, y: r.y, width: r.width, height: r.height, top: r.top, bottom: r.bottom, left: r.left, right: r.right };
    };

    const shell = document.querySelector('.app[data-shell]');
    const shellRect = rect(shell);
    const shellStyle = cs(shell);

    const playerbar = document.querySelector('.shell-playerbar');
    const pbRect = rect(playerbar);
    const pbStyle = cs(playerbar);

    // Aurora or Newsprint player bar root
    const pbRoot = document.querySelector('.aurora-pb') || document.querySelector('.np-pb');
    const pbRootRect = rect(pbRoot);
    const pbRootStyle = cs(pbRoot);

    // Transport area
    const transport = document.querySelector('.aurora-pb-transport') || document.querySelector('.np-pb-transport');
    const transportRect = rect(transport);
    const transportStyle = cs(transport);

    // Progress wrapper (Aurora) or progress container (Newsprint)
    const progressWrap = document.querySelector('.aurora-pb-progress-wrap') || document.querySelector('.np-pb-progress');
    const progressWrapRect = rect(progressWrap);
    const progressWrapStyle = cs(progressWrap);

    // Progress root
    const progressRoot = document.querySelector('.progress-root');
    const progressRootRect = rect(progressRoot);
    const progressRootStyle = cs(progressRoot);

    // Progress track
    const progressTrack = document.querySelector('.progress-track');
    const progressTrackRect = rect(progressTrack);
    const progressTrackStyle = cs(progressTrack);

    // Time labels
    const timeLabels = document.querySelectorAll('.progress-time');
    const timeRects = Array.from(timeLabels).map(el => rect(el));

    // Left and right areas
    const leftArea = document.querySelector('.aurora-pb-left') || document.querySelector('.np-pb-meta');
    const leftRect = rect(leftArea);

    const rightArea = document.querySelector('.aurora-pb-right') || document.querySelector('.np-pb-aux');
    const rightRect = rect(rightArea);

    // Viewport
    const viewport = { x: 0, y: 0, width: window.innerWidth, height: window.innerHeight };

    // Cover stack (potential height driver)
    const coverStack = document.querySelector('.aurora-pb-cover-stack') || document.querySelector('.np-pb-cover-stack');
    const coverStackRect = rect(coverStack);

    // Enter fullscreen button (part of cover stack)
    const fullscreenBtn = document.querySelector('.aurora-pb-enter-fullscreen') || document.querySelector('.np-pb-enter-fullscreen');
    const fullscreenBtnRect = rect(fullscreenBtn);

    return {
      shell: {
        rect: shellRect,
        dataShell: shell?.getAttribute('data-shell'),
        gridTemplateRows: shellStyle?.gridTemplateRows,
        overflow: shellStyle?.overflow,
        minHeight: shellStyle?.minHeight,
        height: shellStyle?.height,
      },
      playerbar: {
        rect: pbRect,
        display: pbStyle?.display,
        overflow: pbStyle?.overflow,
        padding: pbStyle?.padding,
        alignItems: pbStyle?.alignItems,
        gridRow: pbStyle?.gridRow,
      },
      pbRoot: {
        rect: pbRootRect,
        display: pbRootStyle?.display,
        overflow: pbRootStyle?.overflow,
        minHeight: pbRootStyle?.minHeight,
        height: pbRootStyle?.height,
        paddingTop: pbRootStyle?.paddingTop,
        paddingBottom: pbRootStyle?.paddingBottom,
        paddingLeft: pbRootStyle?.paddingLeft,
        paddingRight: pbRootStyle?.paddingRight,
        borderRadius: pbRootStyle?.borderRadius,
        borderBottomWidth: pbRootStyle?.borderBottomWidth,
        borderTopWidth: pbRootStyle?.borderTopWidth,
        gridTemplateColumns: pbRootStyle?.gridTemplateColumns,
        alignItems: pbRootStyle?.alignItems,
        boxSizing: pbRootStyle?.boxSizing,
      },
      transport: {
        rect: transportRect,
        display: transportStyle?.display,
        minHeight: transportStyle?.minHeight,
        height: transportStyle?.height,
      },
      progressWrap: {
        rect: progressWrapRect,
        display: progressWrapStyle?.display,
        width: progressWrapStyle?.width,
        maxWidth: progressWrapStyle?.maxWidth,
        minWidth: progressWrapStyle?.minWidth,
        padding: progressWrapStyle?.padding,
      },
      progressRoot: {
        rect: progressRootRect,
        display: progressRootStyle?.display,
        visibility: progressRootStyle?.visibility,
        opacity: progressRootStyle?.opacity,
        gap: progressRootStyle?.gap,
      },
      progressTrack: {
        rect: progressTrackRect,
        display: progressTrackStyle?.display,
        visibility: progressTrackStyle?.visibility,
        opacity: progressTrackStyle?.opacity,
        height: progressTrackStyle?.height,
        flex: progressTrackStyle?.flex,
        overflow: progressTrackStyle?.overflow,
      },
      timeRects,
      leftRect,
      rightRect,
      coverStackRect,
      fullscreenBtnRect,
      viewport,
    };
  });
}

/** Run layout assertions and return { passed, failures, geometry }. */
function assertLayout(geom, viewport, skinLabel) {
  const failures = [];
  const ctx = `${skinLabel} at ${viewport.width}x${viewport.height}`;

  const { progressTrack, progressRoot, progressWrap, pbRoot, playerbar, transport } = geom;

  // 1. Progress elements exist
  if (!progressRoot?.rect) {
    failures.push(`${ctx}: progress-root element not found`);
    return { passed: false, failures, geometry: geom };
  }
  if (!progressTrack?.rect) {
    failures.push(`${ctx}: progress-track element not found`);
    return { passed: false, failures, geometry: geom };
  }

  // 2. display/visibility/opacity
  if (progressRoot.display === 'none') failures.push(`${ctx}: progress-root display=none`);
  if (progressRoot.visibility === 'hidden') failures.push(`${ctx}: progress-root visibility=hidden`);
  if (parseFloat(progressRoot.opacity) < 0.1) failures.push(`${ctx}: progress-root opacity=${progressRoot.opacity}`);
  if (progressTrack.display === 'none') failures.push(`${ctx}: progress-track display=none`);
  if (progressTrack.visibility === 'hidden') failures.push(`${ctx}: progress-track visibility=hidden`);
  if (parseFloat(progressTrack.opacity) < 0.1) failures.push(`${ctx}: progress-track opacity=${progressTrack.opacity}`);

  // 3. Progress track width >= 120px
  if (progressTrack.rect.width < 120) {
    failures.push(`${ctx}: progress-track width=${progressTrack.rect.width.toFixed(1)}px < 120px minimum`);
  }

  // 4. Progress track and root fully inside player bar root
  const pbRootR = pbRoot.rect;
  const trackR = progressTrack.rect;
  const rootR = progressRoot.rect;

  // Check intersection with pbRoot
  const trackIntersectPb = intersect(trackR, pbRootR);
  const trackVisibleRatioPb = trackR.width > 0 ? (trackIntersectPb.width * trackIntersectPb.height) / (trackR.width * trackR.height) : 0;
  if (trackVisibleRatioPb < 1 - TOLERANCE_PX / Math.max(trackR.width, 1)) {
    failures.push(`${ctx}: progress-track visible ratio in pbRoot=${trackVisibleRatioPb.toFixed(4)} < 1.0 (clipped by player bar root)`);
  }

  // Check intersection with shell-playerbar
  const pbR = playerbar.rect;
  const trackIntersectPbShell = intersect(trackR, pbR);
  const trackVisibleRatioShell = trackR.width > 0 ? (trackIntersectPbShell.width * trackIntersectPbShell.height) / (trackR.width * trackR.height) : 0;
  if (trackVisibleRatioShell < 1 - TOLERANCE_PX / Math.max(trackR.width, 1)) {
    failures.push(`${ctx}: progress-track visible ratio in shell-playerbar=${trackVisibleRatioShell.toFixed(4)} < 1.0 (clipped by shell-playerbar)`);
  }

  // 5. Progress track fully inside viewport
  const vp = geom.viewport;
  const trackIntersectVp = intersect(trackR, vp);
  const trackVisibleRatioVp = trackR.width > 0 ? (trackIntersectVp.width * trackIntersectVp.height) / (trackR.width * trackR.height) : 0;
  if (trackVisibleRatioVp < 1 - TOLERANCE_PX / Math.max(trackR.width, 1)) {
    failures.push(`${ctx}: progress-track visible ratio in viewport=${trackVisibleRatioVp.toFixed(4)} < 1.0 (clipped by viewport)`);
  }

  // 6. Progress root fully inside viewport
  const rootIntersectVp = intersect(rootR, vp);
  const rootVisibleRatioVp = rootR.width > 0 ? (rootIntersectVp.width * rootIntersectVp.height) / (rootR.width * rootR.height) : 0;
  if (rootVisibleRatioVp < 1 - TOLERANCE_PX / Math.max(rootR.width, 1)) {
    failures.push(`${ctx}: progress-root visible ratio in viewport=${rootVisibleRatioVp.toFixed(4)} < 1.0 (outside viewport)`);
  }

  // 7. Transport and progress do not overlap
  if (transport?.rect && progressWrap?.rect) {
    const overlap = intersect(transport.rect, progressWrap.rect);
    if (overlap.height > TOLERANCE_PX && overlap.width > TOLERANCE_PX) {
      failures.push(`${ctx}: transport overlaps progress-wrap by ${overlap.width.toFixed(1)}x${overlap.height.toFixed(1)}px`);
    }
  }

  // 8. Time labels do not overflow player bar root
  for (let i = 0; i < geom.timeRects.length; i++) {
    const tr = geom.timeRects[i];
    if (!tr) continue;
    const labelIntersect = intersect(tr, pbRootR);
    const labelRatio = tr.width > 0 ? (labelIntersect.width * labelIntersect.height) / (tr.width * tr.height) : 0;
    if (labelRatio < 1 - TOLERANCE_PX / Math.max(tr.width, 1)) {
      failures.push(`${ctx}: time label[${i}] visible ratio in pbRoot=${labelRatio.toFixed(4)} < 1.0 (overflowing player bar)`);
    }
  }

  // 9. Progress track not below viewport bottom (clipped by viewport edge)
  if (trackR.bottom > vp.height + TOLERANCE_PX) {
    failures.push(`${ctx}: progress-track bottom=${trackR.bottom.toFixed(1)}px exceeds viewport height=${vp.height}px (clipped at bottom)`);
  }

  // 10. Progress track not above viewport top
  if (trackR.top < -TOLERANCE_PX) {
    failures.push(`${ctx}: progress-track top=${trackR.top.toFixed(1)}px above viewport top (clipped at top)`);
  }

  // 11. Player bar root must fit within shell-playerbar bounding box
  //     (fixed grid row must not be smaller than the player bar content)
  if (pbRootR && pbR) {
    const pbRootInShell = intersect(pbRootR, pbR);
    const pbRootRatio = pbRootR.width > 0
      ? (pbRootInShell.width * pbRootInShell.height) / (pbRootR.width * pbRootR.height)
      : 0;
    const overflowTop = Math.max(0, pbR.top - pbRootR.top);
    const overflowBottom = Math.max(0, pbRootR.bottom - pbR.bottom);
    const overflowLeft = Math.max(0, pbR.left - pbRootR.left);
    const overflowRight = Math.max(0, pbRootR.right - pbR.right);
    if ([overflowTop, overflowBottom, overflowLeft, overflowRight].some((value) => value > TOLERANCE_PX)) {
      failures.push(
        `${ctx}: player-bar-root visible ratio in shell-playerbar=${pbRootRatio.toFixed(4)} < 1.0 ` +
        `(overflow top=${overflowTop.toFixed(1)}px bottom=${overflowBottom.toFixed(1)}px ` +
        `left=${overflowLeft.toFixed(1)}px right=${overflowRight.toFixed(1)}px; ` +
        `pbRoot height=${pbRootR.height.toFixed(1)}px shell-playerbar height=${pbR.height.toFixed(1)}px)`
      );
    }
  }

  return { passed: failures.length === 0, failures, geometry: geom };
}

/** Initialize deterministic player state via dynamic import of playerStore. */
async function initPlayerState(page) {
  await page.evaluate(async () => {
    const mod = await import('/src/api/playerStore.ts');
    const store = mod.playerStore;
    store.currentTrack = {
      FileHash: 'test-hash-001',
      SongName: '测试歌曲',
      SingerName: '测试歌手',
      Duration: 180,
      Image: '',
    };
    store.currentTime = 60;
    store.duration = 180;
    store.isPlaying = false;
    store.isLoading = false;
  });
  // Wait for Vue reactivity to flush
  await page.waitForTimeout(500);
}

async function run() {
  await mkdir(outDir, { recursive: true });

  const browser = await chromium.launch({ headless: true });
  const allResults = [];
  let allPassed = true;

  for (const vp of VIEWPORTS) {
    for (const sm of SKIN_MODES) {
    // For non-1332x862 viewports, only test dark mode (per task spec)
    if (vp.label !== '1332x862' && sm.mode !== 'dark') continue;

    const label = `${sm.skin}-${sm.mode}`;
    const context = await browser.newContext({
      viewport: { width: vp.width, height: vp.height },
      deviceScaleFactor: 1,
      colorScheme: sm.mode === 'dark' ? 'dark' : 'light',
    });
    const page = await context.newPage();

    // Capture console errors
    const consoleErrors = [];
    page.on('console', (msg) => {
      if (msg.type() === 'error') consoleErrors.push(msg.text());
    });

    // Set skin/mode via localStorage before page loads
    await page.addInitScript(({ skin, mode }) => {
      try {
        localStorage.setItem('tweak_skin', skin);
        localStorage.setItem('tweak_mode', mode);
      } catch {}
    }, sm);

    await page.goto(baseUrl, { waitUntil: 'networkidle', timeout: 60000 });

    // Set data-skin/data-mode on root for CSS
    await page.evaluate(({ skin, mode }) => {
      const root = document.documentElement;
      root.setAttribute('data-skin', skin);
      root.setAttribute('data-mode', mode);
      if (document.body) {
        document.body.setAttribute('data-skin', skin);
        document.body.setAttribute('data-mode', mode);
      }
    }, sm);

    // Wait for Vue to mount
    await page.waitForTimeout(800);

    // Initialize deterministic player state
    await initPlayerState(page);

    // Collect geometry
    const geom = await collectGeometry(page);

    // Run assertions
    const result = assertLayout(geom, vp, label);
    const relevantConsoleErrors = consoleErrors.filter((message) =>
      !message.includes("Cannot read properties of undefined (reading 'invoke')")
    );
    if (relevantConsoleErrors.length > 0) {
      result.passed = false;
      for (const message of relevantConsoleErrors) {
        result.failures.push(`${label} at ${vp.width}x${vp.height}: console error: ${message}`);
      }
    }

    // Take screenshot
    const shotName = `${sm.skin}-${sm.mode}-${vp.label}.png`;
    const shotPath = path.join(outDir, shotName);
    await page.screenshot({ path: shotPath, fullPage: false });

    allResults.push({
      viewport: vp.label,
      skin: sm.skin,
      mode: sm.mode,
      passed: result.passed,
      failures: result.failures,
      consoleErrors,
      screenshot: shotPath,
      geometry: result.geometry,
    });

    if (!result.passed) {
      allPassed = false;
      console.error(`FAIL: ${label} @ ${vp.label}`);
      for (const f of result.failures) {
        console.error(`  - ${f}`);
      }
    } else {
      console.log(`PASS: ${label} @ ${vp.label}`);
    }

    await context.close();
    }
  }

  await browser.close();

  // Write manifest
  const manifestPath = path.join(outDir, 'geometry-manifest.json');
  await writeFile(manifestPath, JSON.stringify(allResults, null, 2), 'utf8');

  console.log(`\nManifest: ${manifestPath}`);
  console.log(`Screenshots: ${outDir}`);

  if (!allPassed) {
    console.error('\nLAYOUT CONTRACT VIOLATIONS DETECTED');
    return false;
  } else {
    console.log('\nAll layout checks passed.');
    return true;
  }
}

/** Start Vite dev server and wait until it's ready. */
async function startVite() {
  return new Promise((resolve, reject) => {
    const child = spawn('node', [
      'node_modules/vite/bin/vite.js',
      '--port', String(VITE_PORT),
      '--strictPort',
    ], {
      cwd: VITE_DIR,
      stdio: ['ignore', 'pipe', 'pipe'],
      detached: false,
      shell: false,
    });

    let resolved = false;
    const timer = setTimeout(() => {
      if (!resolved) {
        resolved = true;
        child.kill('SIGTERM');
        reject(new Error('Vite startup timeout (20s)'));
      }
    }, 20000);

    child.stdout.on('data', (data) => {
      const text = data.toString();
      process.stdout.write('[vite] ' + text);
      if (text.includes('ready in') && !resolved) {
        resolved = true;
        clearTimeout(timer);
        setTimeout(() => resolve(child), 1500);
      }
    });

    child.stderr.on('data', (data) => {
      process.stderr.write('[vite-err] ' + data.toString());
    });

    child.on('error', (err) => {
      if (!resolved) { resolved = true; clearTimeout(timer); child.kill('SIGTERM'); reject(err); }
    });

    child.on('exit', (code) => {
      if (!resolved) { resolved = true; clearTimeout(timer); reject(new Error(`Vite exited early code=${code}`)); }
    });
  });
}

/** Wait for HTTP server to respond. */
async function waitForServer(url, retries = 15) {
  for (let i = 0; i < retries; i++) {
    try {
      const resp = await fetch(url, { signal: AbortSignal.timeout(2000) });
      if (resp.ok || resp.status > 0) return;
    } catch { /* retry */ }
    await new Promise(r => setTimeout(r, 500));
  }
  throw new Error(`Server at ${url} not reachable after ${retries} retries`);
}

async function main() {
  let viteProc = null;
  try {
    if (externalBaseUrl) {
      console.log('Using external playerbar QA URL:', externalBaseUrl);
      await waitForServer(externalBaseUrl);
    } else {
      console.log('Starting Vite dev server on port', VITE_PORT);
      viteProc = await startVite();
      console.log('Vite ready. Waiting for HTTP...');
      await waitForServer(baseUrl);
    }
    console.log('Server reachable. Running geometry verification...\n');

    const passed = await run();

    if (!passed) {
      process.exitCode = 1;
    }
  } catch (err) {
    console.error('Fatal:', err.message || err);
    process.exitCode = 2;
  } finally {
    if (viteProc) {
      console.log('\nShutting down Vite...');
      try {
        viteProc.kill('SIGTERM');
        await new Promise((resolve) => {
          const t = setTimeout(resolve, 3000);
          viteProc.on('exit', () => { clearTimeout(t); resolve(); });
        });
      } catch { /* ignore */ }
    }
  }
}

main();
