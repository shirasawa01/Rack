// Runs a scenario in the prototype reference/cable-bench.html (headless Chromium) and prints the same dump as check.cpp.
// frameSim is a copy of the prototype's frame(): keep them the same.
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
	const lines = fs.readFileSync(process.argv[2], 'utf8').split('\n');
	const browser = await chromium.launch();
	const page = await browser.newPage();
	// stop the prototype's own animation loop so only this script steps it
	await page.addInitScript(() => { window.requestAnimationFrame = () => 0; });
	await page.goto('file://' + path.resolve(__dirname, '../cable-bench.html'));
	const out = await page.evaluate((lines) => {
		const out = [];
		cables = []; jacks.length = 0; knobs.length = 0; awake = 0; still = 0; drag = null;
		const extraParts = [];
		const frameSim = () => {
			if (awake > 0 || drag) {
				const k = drag ? 1 : 4;
				for (const c of cables) { c.fx.set(c.x); c.fy.set(c.y); c.fz.set(c.z); }
				for (let s = 0; s < k; s++) step(1 / 60);
				if (!drag) {
					awake--;
					let v = 0;
					for (const c of cables) for (let i = 2; i < c.n - 2; i++) v = Math.max(v, Math.abs(c.x[i] - c.fx[i]) + Math.abs(c.y[i] - c.fy[i]) + Math.abs(c.z[i] - c.fz[i]));
					still = v < 0.004 ? still + 1 : 0;
					if (still > 12) awake = 0;
				}
			}
		};
		for (const line of lines) {
			const t = line.trim().split(/\s+/); if (!t[0]) continue;
			const n = t.slice(1).map(Number);
			if (t[0] === 'params') {
				P.stiff = n[0]; P.weight = n[1]; P.mem = n[2]; P.tilt = n[3]; RC = n[4] / 20; RB = 0.13 + RC * 1.25;
				DESK = n[5] ? n[6] : 1e9; P.fric = n[7];
			}
			else if (t[0] === 'jack') jacks.push({ x: n[0], y: n[1], cable: -1 });
			else if (t[0] === 'part') knobs.push({ x: n[0], y: n[1], R: n[2] / 1.12, H: n[3], ang: 0 });
			else if (t[0] === 'cable') connect(n[0], n[1], 'white', n[2]);
			else if (t[0] === 'frames') for (let i = 0; i < n[0]; i++) frameSim();
			else if (t[0] === 'settle') { let k = 0; while ((awake > 0 || drag) && k < 1000) { frameSim(); k++; } out.push('settled ' + k); }
			else if (t[0] === 'drag') {
				const [ci, i, dx, dy, frames] = n, c = cables[ci];
				const x0 = c.x[i], y0 = c.y[i], z0 = c.z[i];
				c.w[i] = 0; drag = { ci, i, tx: x0, ty: y0, tz: z0 };
				for (let k = 1; k <= frames; k++) { drag.tx = x0 + dx * k / frames; drag.ty = y0 + dy * k / frames; frameSim(); }
				c.w[i] = 1; drag = null; wake(100);
			}
			else if (t[0] === 'dump') {
				out.push('dump ' + t[1]);
				for (const c of cables) { let s = ''; for (let i = 0; i < c.n; i++) s += c.x[i].toFixed(9) + ' ' + c.y[i].toFixed(9) + ' ' + c.z[i].toFixed(9) + ' '; out.push(s); }
			}
		}
		return out.join('\n');
	}, lines);
	console.log(out);
	await browser.close();
})();
