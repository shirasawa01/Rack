// Kept free of Rack headers on purpose: reference/physics_check/ builds this file alone and compares it with the prototype.
#include <app/CablePhysics.hpp>
#include <cmath>
#include <algorithm>


namespace rack {
namespace app {
namespace cablephysics {


/** hypot(x, y, z) as C++17 has it (Rack builds as C++11). Scaled like the prototype's Math.hypot. */
static double hypot3(double x, double y, double z) {
	x = std::fabs(x);
	y = std::fabs(y);
	z = std::fabs(z);
	double a = std::max(x, std::max(y, z));
	if (a == 0)
		return 0;
	x /= a;
	y /= a;
	z /= a;
	return a * std::sqrt(x * x + y * y + z * z);
}


const double STD_LEN[7] = {15, 30, 45, 60, 90, 120, 150};


double chooseLength(double dist) {
	for (double l : STD_LEN) {
		if (l >= dist * 1.1 + 2 * LB + 3)
			return l;
	}
	return STD_LEN[6];
}


bool tooShort(double len, double dist) {
	return len < dist + 2 * LB + 1.5;
}


double Cable::span(int i, int j) const {
	return hypot3(x[j] - x[i], y[j] - y[i], z[j] - z[i]);
}


double Cable::offset(int i, int s) const {
	return hypot3(x[i] - 0.5 * (x[i - s] + x[i + s]), y[i] - 0.5 * (y[i - s] + y[i + s]), z[i] - 0.5 * (z[i - s] + z[i + s]));
}


void Cable::plastic(double rate) {
	for (int k = 0; k < 3; k++) {
		const int s = BEND_SPANS[k];
		for (int i = s; i < n - s; i++)
			restH[k][i] += (offset(i, s) - restH[k][i]) * rate;
	}
}


void Cable::moveEnds(double ax, double ay, double bx, double by) {
	const int ends[4] = {0, 1, n - 2, n - 1};
	for (int k = 0; k < 4; k++) {
		int i = ends[k];
		double ex = (k < 2) ? ax : bx;
		double ey = (k < 2) ? ay : by;
		x[i] = px[i] = ex;
		y[i] = py[i] = ey;
	}
	version++;
}


void makeCable(Cable& c, double ax, double ay, double bx, double by, double len, double mem) {
	const double L = len - 2 * LB;
	const int m = std::max(3, (int) std::round(L / SEG));
	const double seg = L / m;
	const int n = m + 3;
	c.len = len;
	c.n = n;
	c.seg = seg;
	c.x.assign(n, 0); c.y.assign(n, 0); c.z.assign(n, 0);
	c.px.assign(n, 0); c.py.assign(n, 0); c.pz.assign(n, 0);
	c.w.assign(n, 0);
	c.rest1.clear();

	// Initial shape: the arc a person leaves when plugging in. Short cables get an upward loop,
	// longer ones a downward one; both lie on a circle whose arc length equals the free length.
	const double ex = bx - ax, ey = by - ay, dist = std::hypot(ex, ey), half = dist / 2;
	double e1[3] = {1, 0, 0};
	if (dist > 1e-6) {
		e1[0] = ex / dist;
		e1[1] = ey / dist;
	}
	const bool up = L / std::max(dist, 0.1) > 2.5 && L < 26;
	double v[3] = {0, 0.75, 0.66};
	if (up) {
		v[1] = -0.8;
		v[2] = 0.6;
	}
	const double dot = v[0] * e1[0] + v[1] * e1[1] + v[2] * e1[2];
	for (int k = 0; k < 3; k++)
		v[k] -= dot * e1[k];
	double vl = hypot3(v[0], v[1], v[2]);
	if (vl == 0)
		vl = 1;
	const double e2[3] = {v[0] / vl, v[1] / vl, v[2] / vl};

	double R = half, sweep = 0, th0 = 0, ox = 0, oy = 0;
	if (L > M_PI * half * 1.02) {
		double lo = std::max(half, 1e-3), hi = L;
		for (int k = 0; k < 60; k++) {
			double mid = (lo + hi) / 2;
			double l = mid * (2 * M_PI - 2 * std::asin(std::min(1.0, half / mid)));
			if (l > L)
				hi = mid;
			else
				lo = mid;
		}
		R = (lo + hi) / 2;
		double al = std::asin(std::min(1.0, half / R));
		sweep = 2 * M_PI - 2 * al;
		ox = 0;
		oy = R * std::cos(al);
		th0 = std::atan2(-oy, -half);
	}
	const double mx = (ax + bx) / 2, my = (ay + by) / 2;
	for (int i = 0; i < n; i++) {
		double X, Y, Z;
		if (i == 0) {
			X = ax; Y = ay; Z = 0;
		}
		else if (i == n - 1) {
			X = bx; Y = by; Z = 0;
		}
		else {
			double u = (double) (i - 1) / m;
			double cx, cy;
			if (sweep != 0) {
				double th = th0 - u * sweep;
				cx = ox + R * std::cos(th);
				cy = oy + R * std::sin(th);
			}
			else {
				cx = -half + dist * u;
				cy = std::sin(M_PI * u) * std::max(0.3, (L - dist) * 0.5);
			}
			X = mx + e1[0] * cx + e2[0] * cy;
			Y = my + e1[1] * cx + e2[1] * cy;
			Z = LB + e1[2] * cx + e2[2] * cy;
		}
		c.x[i] = c.px[i] = X;
		c.y[i] = c.py[i] = Y;
		c.z[i] = c.pz[i] = Z;
		c.w[i] = (i <= 1 || i >= n - 2) ? 0 : 1;
	}
	// Length of link i -> i+1
	auto d = [&](int i) {return (i == 0 || i == n - 2) ? LB : seg;};
	for (int i = 0; i < n - 1; i++)
		c.rest1.push_back(d(i));
	// A cable keeps some of the curve it was plugged in with (PVC/silicone memory): from straight toward that curve
	const double curl = mem * 0.8;
	for (int k = 0; k < 3; k++) {
		const int s = BEND_SPANS[k];
		c.restH[k].assign(n, 0);
		for (int i = s; i < n - s; i++)
			c.restH[k][i] = c.offset(i, s) * curl;
	}
	c.plast = 0;
	c.version++;
}


// ---------- obstacles (panel parts and plug boots as upright cylinders) ----------

static const double OB_CELL = 2.0;

static int64_t cellKey(int64_t ix, int64_t iy, int64_t iz = 0) {
	const int64_t o = 1 << 20;
	return ((ix + o) << 42) | ((iy + o) << 21) | (iz + o);
}


void World::rebuildObstacles() {
	obstacles = parts;
	const double RB = P.rb();
	for (size_t ci = 0; ci < cables.size(); ci++) {
		const Cable* c = cables[ci];
		obstacles.push_back(Obstacle{c->ax(), c->ay(), RB, LB, (int) ci, 0});
		obstacles.push_back(Obstacle{c->bx(), c->by(), RB, LB, (int) ci, 1});
	}
	obGrid.clear();
	const double RC = P.rc();
	for (int k = 0; k < (int) obstacles.size(); k++) {
		const Obstacle& o = obstacles[k];
		int64_t x0 = std::floor((o.x - o.R - RC) / OB_CELL), x1 = std::floor((o.x + o.R + RC) / OB_CELL);
		int64_t y0 = std::floor((o.y - o.R - RC) / OB_CELL), y1 = std::floor((o.y + o.R + RC) / OB_CELL);
		for (int64_t ix = x0; ix <= x1; ix++)
			for (int64_t iy = y0; iy <= y1; iy++)
				obGrid[cellKey(ix, iy)].push_back(k);
	}
}


void World::wake(int frames) {
	if (awake == 0) {
		for (Cable* c : cables) {
			c->px = c->x;
			c->py = c->y;
			c->pz = c->z;
		}
	}
	awake = std::max(awake, frames);
	still = 0;
}


static void solveDist(Cable& c, int i, int j, double rest, double k) {
	const double wi = c.w[i], wj = c.w[j], ws = wi + wj;
	if (ws == 0)
		return;
	const double dx = c.x[j] - c.x[i], dy = c.y[j] - c.y[i], dz = c.z[j] - c.z[i];
	const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (d < 1e-9)
		return;
	const double f = (d - rest) / (d * ws) * k;
	c.x[i] += dx * wi * f; c.y[i] += dy * wi * f; c.z[i] += dz * wi * f;
	c.x[j] -= dx * wj * f; c.y[j] -= dy * wj * f; c.z[j] -= dz * wj * f;
}


/** Bending: keeps point i at its rest offset from the middle of its neighbours. The offset grows linearly with curvature,
so even gentle bends push back and the cable holds itself up (chord-distance bending is nearly free near straight).
With i = 1 and n - 2 the fixed jack base and boot tip keep the cable leaving the boot straight. */
static void solveBend(Cable& c, int i, int s, double rest, double k) {
	const int a = i - s, b = i + s;
	const double wa = c.w[a], wi = c.w[i], wb = c.w[b];
	const double W = wi + 0.25 * (wa + wb);
	if (W == 0)
		return;
	const double dx = c.x[i] - 0.5 * (c.x[a] + c.x[b]);
	const double dy = c.y[i] - 0.5 * (c.y[a] + c.y[b]);
	const double dz = c.z[i] - 0.5 * (c.z[a] + c.z[b]);
	const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (d < 1e-9)
		return;
	const double f = (d - rest) / d * k / W;
	const double cx = dx * f, cy = dy * f, cz = dz * f;
	c.x[i] -= wi * cx; c.y[i] -= wi * cy; c.z[i] -= wi * cz;
	c.x[a] += 0.5 * wa * cx; c.y[a] += 0.5 * wa * cy; c.z[a] += 0.5 * wa * cz;
	c.x[b] += 0.5 * wb * cx; c.y[b] += 0.5 * wb * cy; c.z[b] += 0.5 * wb * cz;
}


// Friction on a contact surface: slow sliding sticks (static friction), faster sliding is slowed
static const double STICK = 0.02;

static void rub(std::vector<double>& pa, const std::vector<double>& a, std::vector<double>& pb, const std::vector<double>& b, int i, double kinetic) {
	const double va = a[i] - pa[i], vb = b[i] - pb[i];
	if (va * va + vb * vb < STICK * STICK) {
		pa[i] = a[i];
		pb[i] = b[i];
	}
	else {
		pa[i] += va * kinetic;
		pb[i] += vb * kinetic;
	}
}


void World::collideStatic() {
	const double RC = P.rc();
	const double DESK = P.deskY;
	for (int ci = 0; ci < (int) cables.size(); ci++) {
		Cable& c = *cables[ci];
		for (int i = 2; i < c.n - 2; i++) {
			if (c.w[i] == 0)
				continue;
			if (P.desk && c.y[i] > DESK - RC && c.z[i] > -0.5) {
				// Desk surface in front of the case
				c.y[i] = DESK - RC;
				c.py[i] = DESK - RC;
				rub(c.px, c.x, c.pz, c.z, i, 0.4);
			}
			if (c.z[i] < RC) {
				// Panel face
				c.z[i] = RC;
				c.pz[i] = RC;
				rub(c.px, c.x, c.py, c.y, i, 0.35);
			}
			auto it = obGrid.find(cellKey(std::floor(c.x[i] / OB_CELL), std::floor(c.y[i] / OB_CELL)));
			if (it == obGrid.end())
				continue;
			for (int k : it->second) {
				const Obstacle& o = obstacles[k];
				if (o.owner >= 0) {
					// A cable passes through its own boot near its ends. Stacked plugs on the same jack count as its own too.
					bool atA = std::fabs(o.x - c.x[0]) < 1e-6 && std::fabs(o.y - c.y[0]) < 1e-6;
					bool atB = std::fabs(o.x - c.x[c.n - 1]) < 1e-6 && std::fabs(o.y - c.y[c.n - 1]) < 1e-6;
					if ((atA && i <= 3) || (atB && i >= c.n - 4))
						continue;
				}
				const double top = o.H + RC;
				if (c.z[i] >= top)
					continue;
				const double dx = c.x[i] - o.x, dy = c.y[i] - o.y, rr = o.R + RC;
				const double d2 = dx * dx + dy * dy;
				if (d2 >= rr * rr)
					continue;
				const double d = std::sqrt(d2), pr = rr - d, pv = top - c.z[i];
				if (pv < pr) {
					// Rest on top
					c.z[i] = top;
					c.pz[i] = top;
					rub(c.px, c.x, c.py, c.y, i, 0.3);
				}
				else {
					const double nx = d > 1e-6 ? dx / d : 1, ny = d > 1e-6 ? dy / d : 0;
					c.x[i] += nx * pr;
					c.y[i] += ny * pr;
				}
			}
		}
	}
}


// ---------- cable vs cable: segments in a 3D hash ----------

static const double SH = 1.2;

void World::collideCables() {
	const double RC = P.rc();
	shIndex.clear();
	for (size_t k = 0; k < shUsed; k++)
		shCells[k].clear();
	shUsed = 0;
	for (int ci = 0; ci < (int) cables.size(); ci++) {
		const Cable& c = *cables[ci];
		for (int i = 1; i < c.n - 2; i++) {
			int64_t x0 = std::floor((std::min(c.x[i], c.x[i + 1]) - RC) / SH), x1 = std::floor((std::max(c.x[i], c.x[i + 1]) + RC) / SH);
			int64_t y0 = std::floor((std::min(c.y[i], c.y[i + 1]) - RC) / SH), y1 = std::floor((std::max(c.y[i], c.y[i + 1]) + RC) / SH);
			int64_t z0 = std::floor((std::min(c.z[i], c.z[i + 1]) - RC) / SH), z1 = std::floor((std::max(c.z[i], c.z[i + 1]) + RC) / SH);
			const int64_t id = (int64_t) ci * 4096 + i;
			for (int64_t ix = x0; ix <= x1; ix++)
				for (int64_t iy = y0; iy <= y1; iy++)
					for (int64_t iz = z0; iz <= z1; iz++) {
						auto ins = shIndex.emplace(cellKey(ix, iy, iz), (int) shUsed);
						if (ins.second) {
							if (shUsed == shCells.size())
								shCells.emplace_back();
							shUsed++;
						}
						shCells[ins.first->second].push_back(id);
					}
		}
	}
	shDone.clear();
	for (size_t k = 0; k < shUsed; k++) {
		const std::vector<int64_t>& list = shCells[k];
		if (list.size() < 2)
			continue;
		for (size_t p = 0; p < list.size(); p++) {
			for (size_t q = p + 1; q < list.size(); q++) {
				const int64_t s1 = list[p], s2 = list[q];
				const int c1i = s1 / 4096, i = s1 % 4096, c2i = s2 / 4096, j = s2 % 4096;
				if (c1i == c2i && std::abs(i - j) <= 3)
					continue;
				const int64_t key = s1 < s2 ? s1 * 10000000 + s2 : s2 * 10000000 + s1;
				if (!shDone.insert(key).second)
					continue;
				segPush(*cables[c1i], i, *cables[c2i], j);
			}
		}
	}
}


static double clamp01(double v) {
	return v < 0 ? 0 : v > 1 ? 1 : v;
}


void World::segPush(Cable& A, int i, Cable& B, int j) {
	const double p1x = A.x[i], p1y = A.y[i], p1z = A.z[i];
	const double d1x = A.x[i + 1] - p1x, d1y = A.y[i + 1] - p1y, d1z = A.z[i + 1] - p1z;
	const double p2x = B.x[j], p2y = B.y[j], p2z = B.z[j];
	const double d2x = B.x[j + 1] - p2x, d2y = B.y[j + 1] - p2y, d2z = B.z[j + 1] - p2z;
	const double rx = p1x - p2x, ry = p1y - p2y, rz = p1z - p2z;
	const double a = d1x * d1x + d1y * d1y + d1z * d1z, e = d2x * d2x + d2y * d2y + d2z * d2z, f = d2x * rx + d2y * ry + d2z * rz;
	const double cc = d1x * rx + d1y * ry + d1z * rz, b = d1x * d2x + d1y * d2y + d1z * d2z, den = a * e - b * b;
	double s = den > 1e-12 ? clamp01((b * f - cc * e) / den) : 0;
	double t = (b * s + f) / e;
	if (t < 0) {
		t = 0;
		s = clamp01(-cc / a);
	}
	else if (t > 1) {
		t = 1;
		s = clamp01((b - cc) / a);
	}
	const double ax = p1x + d1x * s, ay = p1y + d1y * s, az = p1z + d1z * s;
	const double bx = p2x + d2x * t, by = p2y + d2y * t, bz = p2z + d2z * t;
	double nx = ax - bx, ny = ay - by, nz = az - bz;
	const double d = std::sqrt(nx * nx + ny * ny + nz * nz), min = 2 * P.rc();
	if (d >= min)
		return;
	if (d > 1e-7) {
		nx /= d; ny /= d; nz /= d;
	}
	else {
		nx = 0; ny = 0; nz = 1;
	}
	const double wa0 = A.w[i] * (1 - s), wa1 = A.w[i + 1] * s, wb0 = B.w[j] * (1 - t), wb1 = B.w[j + 1] * t;
	const double den2 = wa0 * (1 - s) + wa1 * s + wb0 * (1 - t) + wb1 * t;
	if (den2 < 1e-9)
		return;
	const double lam = (min - d) / den2 * 0.8;
	A.x[i] += nx * wa0 * lam; A.y[i] += ny * wa0 * lam; A.z[i] += nz * wa0 * lam;
	A.x[i + 1] += nx * wa1 * lam; A.y[i + 1] += ny * wa1 * lam; A.z[i + 1] += nz * wa1 * lam;
	B.x[j] -= nx * wb0 * lam; B.y[j] -= ny * wb0 * lam; B.z[j] -= nz * wb0 * lam;
	B.x[j + 1] -= nx * wb1 * lam; B.y[j + 1] -= ny * wb1 * lam; B.z[j + 1] -= nz * wb1 * lam;
	// Friction: the sliding of one cable over the other in this step, across the contact normal.
	// Slow sliding sticks, faster sliding is slowed (the same numbers as the panel at friction 0.5).
	const double mu = P.friction;
	if (mu <= 0 || !frictionPass)
		return;
	const double vx = (1 - s) * (A.x[i] - A.px[i]) + s * (A.x[i + 1] - A.px[i + 1]) - (1 - t) * (B.x[j] - B.px[j]) - t * (B.x[j + 1] - B.px[j + 1]);
	const double vy = (1 - s) * (A.y[i] - A.py[i]) + s * (A.y[i + 1] - A.py[i + 1]) - (1 - t) * (B.y[j] - B.py[j]) - t * (B.y[j + 1] - B.py[j + 1]);
	const double vz = (1 - s) * (A.z[i] - A.pz[i]) + s * (A.z[i + 1] - A.pz[i + 1]) - (1 - t) * (B.z[j] - B.pz[j]) - t * (B.z[j + 1] - B.pz[j + 1]);
	const double vn = vx * nx + vy * ny + vz * nz;
	const double tx = vx - vn * nx, ty = vy - vn * ny, tz = vz - vn * nz;
	const double vt2 = tx * tx + ty * ty + tz * tz;
	const double stick = STICK * 2 * mu;
	const double keep = (vt2 < stick * stick) ? 1 : 0.7 * mu;
	const double g = keep / den2;
	A.px[i] += tx * wa0 * g; A.py[i] += ty * wa0 * g; A.pz[i] += tz * wa0 * g;
	A.px[i + 1] += tx * wa1 * g; A.py[i + 1] += ty * wa1 * g; A.pz[i + 1] += tz * wa1 * g;
	B.px[j] -= tx * wb0 * g; B.py[j] -= ty * wb0 * g; B.pz[j] -= tz * wb0 * g;
	B.px[j + 1] -= tx * wb1 * g; B.py[j + 1] -= ty * wb1 * g; B.pz[j + 1] -= tz * wb1 * g;
}


// ---------- solver ----------

void World::step(double dt) {
	const double h = dt / SUB;
	const double g = 981 * 0.9 * P.weight * P.weight + 6;
	const double th = P.tilt * M_PI / 180;
	const double gy = g * std::cos(th) * h * h, gz = -g * std::sin(th) * h * h;
	const double kb = 0.01 + 0.99 * P.stiff * P.stiff;
	// Heavy damping like a real cable's internal friction; it tightens further in the last frames before locking
	const double damp = dragging ? 0.9 : (awake < 20 ? 0.6 + 0.33 * awake / 20 : 0.93);
	for (int sub = 0; sub < SUB; sub++) {
		for (Cable* cp : cables) {
			Cable& c = *cp;
			for (int i = 0; i < c.n; i++) {
				if (c.w[i] == 0)
					continue;
				const double vx = (c.x[i] - c.px[i]) * damp, vy = (c.y[i] - c.py[i]) * damp, vz = (c.z[i] - c.pz[i]) * damp;
				c.px[i] = c.x[i]; c.py[i] = c.y[i]; c.pz[i] = c.z[i];
				c.x[i] += vx; c.y[i] += vy + gy; c.z[i] += vz + gz;
			}
		}
		if (dragging)
			applyDrag();
		for (int it = 0; it < ITER; it++) {
			for (Cable* cp : cables) {
				Cable& c = *cp;
				const int n = c.n;
				for (int i = 0; i < n - 1; i++)
					solveDist(c, i, i + 1, c.rest1[i], 1);
				for (int k = 0; k < 3; k++) {
					const int s = BEND_SPANS[k];
					for (int i = s; i < n - s; i++)
						solveBend(c, i, s, c.restH[k][i], kb * BEND_WEIGHT[k]);
				}
			}
			if (it % 4 == 3) {
				frictionPass = (it == ITER - 1);
				collideCables();
				collideStatic();
			}
			if (dragging)
				applyDrag();
		}
	}
	// Bending by hand leaves a set in the cable; it fades out after release
	for (Cable* c : cables) {
		if (dragging && drag.cable == c)
			c->plast = 1;
		if (c->plast > 0.01) {
			c->plastic(0.04 * P.mem * c->plast);
			c->plast *= 0.96;
		}
	}
}


bool World::frame() {
	if (!isAwake())
		return false;
	// After a release the settling runs several steps per frame, so the cable drops into place and locks
	const int k = dragging ? 1 : 4;
	for (Cable* c : cables) {
		c->fx = c->x;
		c->fy = c->y;
		c->fz = c->z;
	}
	for (int s = 0; s < k; s++)
		step(1.0 / 60);
	if (!dragging) {
		awake--;
		// Movement over the whole frame: contacts with friction can tremble in place by a few micrometres from substep to substep
		double v = 0;
		for (const Cable* c : cables)
			for (int i = 2; i < c->n - 2; i++)
				v = std::max(v, std::fabs(c->x[i] - c->fx[i]) + std::fabs(c->y[i] - c->fy[i]) + std::fabs(c->z[i] - c->fz[i]));
		still = v < 0.004 ? still + 1 : 0;
		if (still > 12)
			awake = 0;
	}
	for (Cable* c : cables)
		c->version++;
	return true;
}


void World::applyDrag() {
	Cable& c = *drag.cable;
	const int i = drag.i;
	c.x[i] = c.px[i] = drag.tx;
	c.y[i] = c.py[i] = drag.ty;
	c.z[i] = c.pz[i] = std::max(P.rc(), drag.tz);
}


void World::startDrag(Cable* c, int i, double tx, double ty, double tz) {
	if (dragging)
		endDrag();
	c->w[i] = 0;
	dragging = true;
	drag.cable = c;
	drag.i = i;
	drag.tx = tx;
	drag.ty = ty;
	drag.tz = tz;
}


void World::moveDrag(double tx, double ty, double tz) {
	drag.tx = tx;
	drag.ty = ty;
	drag.tz = tz;
}


void World::endDrag() {
	if (!dragging)
		return;
	drag.cable->w[drag.i] = 1;
	dragging = false;
	drag.cable = nullptr;
	wake(100);
}


} // namespace cablephysics
} // namespace app
} // namespace rack
