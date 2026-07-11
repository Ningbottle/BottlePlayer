import { chromium } from 'playwright';
import { mkdir } from 'node:fs/promises';

const url = process.env.AURORA_QA_URL || 'http://127.0.0.1:5181/';
await mkdir('design-qa-captures/live-preview', { recursive: true });

const browser = await chromium.launch({ headless: true });
const ctx = await browser.newContext({
  viewport: { width: 1586, height: 1024 },
  colorScheme: 'dark',
});
const page = await ctx.newPage();
await page.addInitScript(() => {
  localStorage.setItem('tweak_skin', 'aurora');
  localStorage.setItem('tweak_mode', 'dark');
  localStorage.removeItem('tweak_accent');
});
await page.goto(url, { waitUntil: 'networkidle', timeout: 60000 });
await page.evaluate(() => {
  document.documentElement.dataset.skin = 'aurora';
  document.documentElement.dataset.mode = 'dark';
});
await page.waitForTimeout(1000);

await page.evaluate(() => {
  const svg =
    '<svg xmlns="http://www.w3.org/2000/svg" width="120" height="120">' +
    '<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">' +
    '<stop offset="0%" stop-color="#3a4f48"/>' +
    '<stop offset="100%" stop-color="#1a2422"/>' +
    '</linearGradient></defs>' +
    '<rect fill="url(#g)" width="120" height="120"/>' +
    '<circle cx="60" cy="48" r="18" fill="#6a8a7e"/>' +
    '<ellipse cx="60" cy="92" rx="28" ry="18" fill="#6a8a7e"/>' +
    '</svg>';
  const img = document.querySelector('.aurora-pb-cover img');
  if (img) img.src = 'data:image/svg+xml;utf8,' + encodeURIComponent(svg);

  const info = document.querySelector('.aurora-pb-info-btn');
  if (info) info.innerHTML = '<b>苏丽珍</b><span>方大同</span>';

  const track = document.querySelector('.progress-track');
  if (track) track.style.setProperty('--progress-pct', '28%');

  const times = document.querySelectorAll('.progress-time');
  if (times[0]) times[0].textContent = '00:34';
  if (times[1]) times[1].textContent = '03:37';

  const q = document.querySelector('.aurora-pb-q-main');
  if (q) q.textContent = '无损';

  const vol = document.querySelector('.aurora-pb-vol-fill');
  if (vol) vol.style.width = '62%';
});

await page.waitForTimeout(250);
await page.screenshot({
  path: 'design-qa-captures/live-preview/player-redesign-full.png',
  fullPage: false,
});
await page.locator('.aurora-pb').first().screenshot({
  path: 'design-qa-captures/live-preview/player-redesign-dock.png',
});
console.log('shots ok');
await browser.close();
