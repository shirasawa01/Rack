// shot_bench.js — 試作 cable-bench.html を画面なしの Chromium で開き、Rack と同じ真正面（カメラ 0°）で撮る（見比べの正解）。
//   node reference/shot_bench.js out_dir      → out_dir/bench_flat.png, bench_round.png
const { chromium } = require('playwright');
const path = require('path');
(async () => {
	const out = process.argv[2] || '.';
	const browser = await chromium.launch();
	const page = await browser.newPage({ viewport: { width: 1400, height: 1000 } });
	await page.goto('file://' + path.resolve(__dirname, 'cable-bench.html'));
	await page.waitForTimeout(1500);
	await page.evaluate(() => { const el = document.getElementById('cam'); el.value = 0; el.dispatchEvent(new Event('input')); });
	for (const style of ['flat', 'round']) {
		await page.evaluate((s) => document.getElementById(s === 'flat' ? 'sFlat' : 'sRound').click(), style);
		await page.waitForTimeout(3000);
		await page.locator('#cv').screenshot({ path: path.join(out, 'bench_' + style + '.png') });
	}
	await browser.close();
})();
