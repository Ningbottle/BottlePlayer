import { chromium } from 'playwright';
import { mkdir } from 'node:fs/promises';

const url = process.env.AURORA_QA_URL || 'http://127.0.0.1:5182/';
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
await page.waitForTimeout(900);

// Inject visual demo content into empty shell
await page.evaluate(() => {
  const cover = document.querySelector('.aurora-cover');
  if (cover) {
    cover.innerHTML =
      '<img alt="cover" src="data:image/svg+xml;utf8,' +
      encodeURIComponent(
        '<svg xmlns="http://www.w3.org/2000/svg" width="400" height="400">' +
          '<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">' +
          '<stop offset="0%" stop-color="#3d524b"/><stop offset="100%" stop-color="#1a2421"/>' +
          '</linearGradient></defs>' +
          '<rect fill="url(#g)" width="400" height="400"/>' +
          '<text x="50%" y="52%" fill="#cfe" font-size="42" text-anchor="middle" font-family="Georgia,serif">苏丽珍</text>' +
          '</svg>',
      ) +
      '" />';
  }
  const name = document.querySelector('.aurora-song-name');
  if (name) name.textContent = '苏丽珍';
  const artist = document.querySelector('.aurora-artist');
  if (artist) artist.innerHTML = '方大同 <span class="aurora-artist-chevron">›</span>';

  const rail = document.querySelector('[data-test="queue-rail"]');
  if (rail) {
    const songs = [
      ['01', '我对缘分小心翼翼', '林俊杰', '4:42'],
      ['02', '不只是场梦', '李玖哲', '3:32'],
      ['03', '苏丽珍', '方大同', '3:37'],
      ['04', '蜂鸟', '吴青峰', '3:45'],
      ['05', '非你不爱', '弦子', '4:17'],
      ['06', '故事细腻', '林俊杰', '3:34'],
      ['07', '交换余生', '林俊杰', '4:44'],
      ['08', '温柔的', '五月天', '4:33'],
      ['09', '重拾·快乐', '陈奕迅', '4:18'],
      ['10', '林俊杰的温柔', '林俊杰', '5:01'],
      ['11', '燃曲', '林俊杰', '3:56'],
      ['12', 'ICE小寒喜欢的音乐', '群星', '6:02'],
    ];
    const head = rail.querySelector('.aurora-queue-rail-head');
    if (head) {
      const h2 = head.querySelector('h2');
      if (h2) h2.innerHTML = '播放队列 <span>12</span>';
    }
    let ol = rail.querySelector('ol');
    if (!ol) {
      const empty = rail.querySelector('.aurora-queue-empty');
      if (empty) empty.remove();
      ol = document.createElement('ol');
      ol.className = 'aurora-queue-list';
      rail.appendChild(ol);
    }
    ol.innerHTML = songs
      .map(
        ([i, t, a, d], idx) =>
          `<li class="aurora-queue-row"><button type="button" class="${idx === 2 ? 'is-active' : ''}" ${idx === 2 ? 'aria-current="true"' : ''}>` +
          `<span class="aurora-queue-index">${i}</span>` +
          `<span class="aurora-queue-copy"><b>${t}</b><small>${a}</small></span>` +
          `<span class="aurora-queue-duration">${d}</span></button></li>`,
      )
      .join('');
  }

  // player dock demo
  const img = document.querySelector('.aurora-pb-cover img');
  if (img) {
    img.src =
      'data:image/svg+xml;utf8,' +
      encodeURIComponent(
        '<svg xmlns="http://www.w3.org/2000/svg" width="80" height="80"><rect fill="#2a3a36" width="80" height="80"/><circle cx="40" cy="32" r="12" fill="#7a9"/></svg>',
      );
  }
  const info = document.querySelector('.aurora-pb-info-btn');
  if (info) info.innerHTML = '<b>苏丽珍</b><span>方大同</span>';
  const track = document.querySelector('.progress-track');
  if (track) track.style.setProperty('--progress-pct', '28%');
  const times = document.querySelectorAll('.progress-time');
  if (times[0]) times[0].textContent = '00:34';
  if (times[1]) times[1].textContent = '03:37';
});

await page.waitForTimeout(300);
await page.screenshot({ path: 'design-qa-captures/live-preview/stage-redesign-full.png', fullPage: false });
const pb = page.locator('.aurora-pb').first();
if (await pb.count()) await pb.screenshot({ path: 'design-qa-captures/live-preview/stage-redesign-dock.png' });
const rail = page.locator('[data-test="queue-rail"]').first();
if (await rail.count()) await rail.screenshot({ path: 'design-qa-captures/live-preview/stage-redesign-queue.png' });
console.log('ok');
await browser.close();
