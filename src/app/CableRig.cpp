#include <map>
#include <algorithm>
#include <app/CableRig.hpp>
#include <app/CablePhysics.hpp>
#include <app/RackWidget.hpp>
#include <app/RackScrollWidget.hpp>
#include <app/CableWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/ParamWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/Knob.hpp>
#include <app/SliderKnob.hpp>
#include <app/Scene.hpp>
#include <context.hpp>
#include <settings.hpp>
#include <color.hpp>


namespace rack {
namespace app {
namespace cablephysics {


Look look;

static Rig* currentRig = NULL;


bool enabled() {
	return settings::cablePhysics;
}


Rig* getRig() {
	return currentRig;
}


/** A point of the smoothed curve that is drawn */
struct Pt {
	/** Rack px */
	float x, y;
	/** cm */
	double wx, wy, wz;
	/** Shift toward the light, normal and tangent (screen) */
	float ox, oy, nx, ny, tx, ty;
};


struct Entry {
	Cable cable;
	/** Complete and in the world. Otherwise `cable` is the preview while a plug is in the hand. */
	bool live = false;
	PortWidget* outputPort = NULL;
	PortWidget* inputPort = NULL;
	/** Owned. From the patch, used when the cable is next laid out. */
	json_t* pendingJ = NULL;
	NVGcolor color;
	// Draw cache
	uint64_t denseVersion = UINT64_MAX;
	std::vector<Pt> dense;

	~Entry() {
		if (pendingJ)
			json_decref(pendingJ);
	}
};


struct Rig::Internal {
	World world;
	std::map<CableWidget*, Entry*> entries;
	/** Drawn this frame, in the rack's cable order */
	std::vector<Entry*> order;
	size_t partsHash = 0;
	bool partsValid = false;
	/** Grab offset, cm */
	double grabDx = 0, grabDy = 0;

	Entry* entry(CableWidget* cw) {
		auto it = entries.find(cw);
		if (it != entries.end())
			return it->second;
		Entry* e = new Entry;
		entries[cw] = e;
		return e;
	}

	void clear() {
		world.endDrag();
		world.cables.clear();
		world.rebuildObstacles();
		for (auto& it : entries)
			delete it.second;
		entries.clear();
		order.clear();
		partsValid = false;
	}
};


Rig::Rig() {
	internal = new Internal;
	currentRig = this;
}


Rig::~Rig() {
	internal->clear();
	delete internal;
	if (currentRig == this)
		currentRig = NULL;
}


// ---------- panel parts (spec 5.1) ----------

static size_t partsHash(RackWidget* rack) {
	size_t h = 0;
	auto mix = [&](size_t v) {
		h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
	};
	for (ModuleWidget* mw : rack->getModules()) {
		mix(std::hash<void*>()(mw));
		mix(std::hash<float>()(mw->box.pos.x));
		mix(std::hash<float>()(mw->box.pos.y));
	}
	return h;
}


/** Knobs, switches and jacks as upright cylinders, taken from the module widgets so that new modules need no work. */
static void buildParts(RackWidget* rack, std::vector<Obstacle>& parts) {
	parts.clear();
	for (ModuleWidget* mw : rack->getModules()) {
		math::Vec mpos = mw->box.pos;
		for (ParamWidget* pw : mw->getParams()) {
			if (!pw->isVisible())
				continue;
			double w = pw->box.size.x / PX_PER_CM, h = pw->box.size.y / PX_PER_CM;
			// Displays and touch surfaces made of params are not knobs
			if (!(w > 0 && h > 0 && w < 3.5 && h < 3.5))
				continue;
			math::Vec c = mpos.plus(pw->box.getCenter()).div(PX_PER_CM);
			double R, H;
			if (dynamic_cast<SliderKnob*>(pw)) {
				// Fader caps stand low; a cylinder is a rough fit for their long slot anyway
				R = std::min(w, h) / 2;
				H = 0.5;
			}
			else if (dynamic_cast<Knob*>(pw)) {
				// 1.6 cm for a trimpot up to 2.2 cm for a large knob
				R = (w + h) / 4;
				H = std::min(2.2, std::max(1.6, 1.6 + (2 * R - 0.6) / 2.1 * 0.6));
			}
			else {
				// Switches and buttons
				R = (w + h) / 4;
				H = 1.0;
			}
			parts.push_back(Obstacle{c.x, c.y, R, H, -1, -1});
		}
		for (PortWidget* pw : mw->getPorts()) {
			if (!pw->isVisible())
				continue;
			math::Vec c = mpos.plus(pw->box.getCenter()).div(PX_PER_CM);
			double R = (pw->box.size.x + pw->box.size.y) / 4 / PX_PER_CM;
			parts.push_back(Obstacle{c.x, c.y, R, 0.3, -1, -1});
		}
	}
}


// ---------- patch file ----------

static const double JSON_UNIT = 1e-4;


static json_t* intArray(const std::vector<double>& v) {
	json_t* a = json_array();
	for (double d : v)
		json_array_append_new(a, json_integer((json_int_t) std::llround(d / JSON_UNIT)));
	return a;
}


static bool readArray(json_t* a, std::vector<double>& v, size_t size, double unit) {
	if (!json_is_array(a) || json_array_size(a) != size)
		return false;
	v.resize(size);
	for (size_t i = 0; i < size; i++)
		v[i] = json_number_value(json_array_get(a, i)) * unit;
	return true;
}


/** Lays out `c` from the saved points. The patch may be pasted elsewhere: both ends must have moved by the same amount. */
static bool loadJson(Cable& c, json_t* j, double ax, double ay, double bx, double by, double mem) {
	json_t* lengthJ = json_object_get(j, "length");
	if (!lengthJ)
		return false;
	double len = json_number_value(lengthJ);
	if (!(len > 0) || tooShort(len, std::hypot(bx - ax, by - ay)))
		return false;
	makeCable(c, ax, ay, bx, by, len, mem);
	const int n = c.n;
	json_t* unitJ = json_object_get(j, "unit");
	double unit = unitJ ? json_number_value(unitJ) : JSON_UNIT;
	std::vector<double> p, rest[3];
	if (!readArray(json_object_get(j, "points"), p, 3 * n, unit))
		return false;
	// Bending memory per span (BEND_SPANS). Patches saved with the first, chord-distance bending have none: laid out afresh.
	json_t* bendJ = json_object_get(j, "bend");
	if (!json_is_array(bendJ) || json_array_size(bendJ) != 3)
		return false;
	for (int k = 0; k < 3; k++) {
		if (!readArray(json_array_get(bendJ, k), rest[k], n, unit))
			return false;
	}
	double dax = ax - p[0], day = ay - p[1];
	double dbx = bx - p[3 * (n - 1)], dby = by - p[3 * (n - 1) + 1];
	if (std::hypot(dax - dbx, day - dby) > 0.01)
		return false;
	for (int i = 0; i < n; i++) {
		// Keep the fixed points exactly on the jacks
		if (i <= 1 || i >= n - 2)
			continue;
		c.x[i] = c.px[i] = p[3 * i] + dax;
		c.y[i] = c.py[i] = p[3 * i + 1] + day;
		c.z[i] = c.pz[i] = p[3 * i + 2];
	}
	for (int k = 0; k < 3; k++)
		c.restH[k] = rest[k];
	c.version++;
	return true;
}


json_t* Rig::toJson(CableWidget* cw) {
	auto it = internal->entries.find(cw);
	if (it == internal->entries.end())
		return NULL;
	Entry* e = it->second;
	if (!e->live) {
		// Not laid out yet (saved right after loading): keep what the patch had
		return e->pendingJ ? json_deep_copy(e->pendingJ) : NULL;
	}
	const Cable& c = e->cable;
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "length", json_real(c.len));
	json_object_set_new(rootJ, "unit", json_real(JSON_UNIT));
	std::vector<double> p;
	p.reserve(3 * c.n);
	for (int i = 0; i < c.n; i++) {
		p.push_back(c.x[i]);
		p.push_back(c.y[i]);
		p.push_back(c.z[i]);
	}
	json_object_set_new(rootJ, "points", intArray(p));
	json_t* bendJ = json_array();
	for (int k = 0; k < 3; k++)
		json_array_append_new(bendJ, intArray(c.restH[k]));
	json_object_set_new(rootJ, "bend", bendJ);
	return rootJ;
}


void Rig::setJson(CableWidget* cw, json_t* physicsJ) {
	Entry* e = internal->entry(cw);
	if (e->pendingJ)
		json_decref(e->pendingJ);
	e->pendingJ = physicsJ ? json_deep_copy(physicsJ) : NULL;
	// Lay it out again from the patch
	e->live = false;
}


void Rig::forget(CableWidget* cw) {
	auto it = internal->entries.find(cw);
	if (it == internal->entries.end())
		return;
	Entry* e = it->second;
	World& W = internal->world;
	if (W.dragging && W.drag.cable == &e->cable)
		W.endDrag();
	auto ci = std::find(W.cables.begin(), W.cables.end(), &e->cable);
	if (ci != W.cables.end()) {
		W.cables.erase(ci);
		W.rebuildObstacles();
		// Cables lying on it fall
		W.wake();
	}
	internal->order.erase(std::remove(internal->order.begin(), internal->order.end(), e), internal->order.end());
	delete e;
	internal->entries.erase(it);
}


// ---------- stepping ----------

void Rig::step(RackWidget* rack) {
	Internal& I = *internal;
	World& W = I.world;
	if (!enabled()) {
		if (!I.entries.empty())
			I.clear();
		return;
	}

	Params p;
	p.stiff = settings::cableStiffness;
	p.weight = settings::cableWeight;
	p.mem = settings::cableMemory;
	p.tilt = settings::caseTilt;
	p.thickness = settings::cableThickness;
	p.friction = settings::cableFriction;
	// No desk in Rack (plan Q1): long cables hang down the rack
	p.desk = false;
	bool paramsChanged = p.stiff != W.P.stiff || p.weight != W.P.weight || p.mem != W.P.mem || p.tilt != W.P.tilt || p.thickness != W.P.thickness || p.friction != W.P.friction;
	W.P = p;

	size_t h = partsHash(rack);
	bool partsChanged = !I.partsValid || h != I.partsHash;
	if (partsChanged) {
		buildParts(rack, W.parts);
		I.partsHash = h;
		I.partsValid = true;
	}

	// Obstacles follow the plugs; waking makes the cables move
	bool cablesChanged = false;
	bool wake = false;
	/** Cables already lying there since the last frame */
	int resting = 0;
	std::vector<Cable*> live;
	I.order.clear();
	for (CableWidget* cw : rack->getCables()) {
		Entry* e = I.entry(cw);
		e->color = cw->color;
		math::Vec a = cw->getOutputPos().div(PX_PER_CM);
		math::Vec b = cw->getInputPos().div(PX_PER_CM);
		double dist = std::hypot(b.x - a.x, b.y - a.y);
		Cable& c = e->cable;
		if (cw->isComplete()) {
			if (!e->live || e->outputPort != cw->outputPort || e->inputPort != cw->inputPort) {
				// Plugged in
				Cable saved;
				if (e->pendingJ && loadJson(saved, e->pendingJ, a.x, a.y, b.x, b.y, p.mem)) {
					// Versions only go up, for the draw cache
					uint64_t version = c.version;
					c = saved;
					c.version = version + 1;
					// Already lying as it was saved: leave it (and the others) at rest
				}
				else {
					makeCable(c, a.x, a.y, b.x, b.y, chooseLength(dist), p.mem);
					wake = true;
				}
				if (e->pendingJ) {
					json_decref(e->pendingJ);
					e->pendingJ = NULL;
				}
				e->live = true;
				e->outputPort = cw->outputPort;
				e->inputPort = cw->inputPort;
				cablesChanged = true;
			}
			else if (std::fabs(c.ax() - a.x) > 1e-6 || std::fabs(c.ay() - a.y) > 1e-6 || std::fabs(c.bx() - b.x) > 1e-6 || std::fabs(c.by() - b.y) > 1e-6) {
				// A module moved
				if (tooShort(c.len, dist))
					makeCable(c, a.x, a.y, b.x, b.y, chooseLength(dist), p.mem);
				else
					c.moveEnds(a.x, a.y, b.x, b.y);
				cablesChanged = true;
				wake = true;
			}
			else {
				resting++;
			}
			live.push_back(&c);
		}
		else {
			if (e->live) {
				if (W.dragging && W.drag.cable == &c)
					W.endDrag();
				e->live = false;
				e->outputPort = NULL;
				e->inputPort = NULL;
			}
			// A plug in the hand: the plugging-in arc to the mouse, without physics
			makeCable(c, a.x, a.y, b.x, b.y, chooseLength(dist), p.mem);
		}
		I.order.push_back(e);
	}
	if (live.size() < W.cables.size())
		wake = true;
	if (live != W.cables) {
		W.cables = live;
		cablesChanged = true;
	}
	if (cablesChanged || partsChanged || paramsChanged)
		W.rebuildObstacles();
	// A module moved or a setting changed under cables that were lying there.
	// (Not when a patch has just loaded: its modules and cables arrive together, already at rest.)
	if ((partsChanged || paramsChanged) && resting > 0)
		wake = true;
	if (wake)
		W.wake();
	W.frame();
}


// ---------- drawing (spec chapter 4) ----------

static void densify(Entry* e) {
	const Cable& c = e->cable;
	if (e->denseVersion == c.version)
		return;
	e->denseVersion = c.version;
	const int n = c.n;
	// One light smoothing pass in 3D, then a Catmull-Rom spline through the points.
	// The jack base point steers the spline so the cable still leaves the boot straight toward the viewer.
	std::vector<double> X(c.x), Y(c.y), Z(c.z);
	for (int i = 2; i < n - 2; i++) {
		X[i] = 0.25 * c.x[i - 1] + 0.5 * c.x[i] + 0.25 * c.x[i + 1];
		Y[i] = 0.25 * c.y[i - 1] + 0.5 * c.y[i] + 0.25 * c.y[i + 1];
		Z[i] = 0.25 * c.z[i - 1] + 0.5 * c.z[i] + 0.25 * c.z[i + 1];
	}
	std::vector<Pt>& out = e->dense;
	out.clear();
	const int SUBD = look.subdivisions;
	auto push = [&](double x, double y, double z) {
		Pt p;
		p.wx = x;
		p.wy = y;
		p.wz = z;
		p.x = x * PX_PER_CM;
		p.y = y * PX_PER_CM;
		out.push_back(p);
	};
	for (int j = 1; j < n - 2; j++) {
		int a = j - 1, b = j, cc = j + 1, d = std::min(n - 1, j + 2);
		for (int k = 0; k < SUBD; k++) {
			double t = (double) k / SUBD, t2 = t * t, t3 = t2 * t;
			double f0 = -0.5 * t3 + t2 - 0.5 * t, f1 = 1.5 * t3 - 2.5 * t2 + 1, f2 = -1.5 * t3 + 2 * t2 + 0.5 * t, f3 = 0.5 * t3 - 0.5 * t2;
			push(f0 * X[a] + f1 * X[b] + f2 * X[cc] + f3 * X[d],
				f0 * Y[a] + f1 * Y[b] + f2 * Y[cc] + f3 * Y[d],
				f0 * Z[a] + f1 * Z[b] + f2 * Z[cc] + f3 * Z[d]);
		}
	}
	push(X[n - 2], Y[n - 2], Z[n - 2]);
	// Where the light falls across the tube: the light direction projected on the curve's normal.
	// It changes smoothly along the cable, so the highlight never jumps sides.
	const int m = out.size();
	for (int i = 0; i < m; i++) {
		const Pt& A = out[std::max(0, i - 1)];
		const Pt& B = out[std::min(m - 1, i + 1)];
		float tx = B.x - A.x, ty = B.y - A.y;
		float l = std::hypot(tx, ty);
		if (l == 0)
			l = 1;
		tx /= l;
		ty /= l;
		float nx = -ty, ny = tx, k = nx * look.lightX + ny * look.lightY;
		out[i].ox = nx * k;
		out[i].oy = ny * k;
		out[i].nx = nx;
		out[i].ny = ny;
		out[i].tx = tx;
		out[i].ty = ty;
	}
}


static NVGcolor mixColor(NVGcolor c, float t) {
	// t < 0 toward black, t > 0 toward white
	NVGcolor target = (t < 0) ? nvgRGBf(0, 0, 0) : nvgRGBf(1, 1, 1);
	t = std::fabs(t);
	NVGcolor r = c;
	r.r += (target.r - c.r) * t;
	r.g += (target.g - c.g) * t;
	r.b += (target.b - c.b) * t;
	r.a = 1.f;
	return r;
}


struct Shade {
	NVGcolor body, edge;
	NVGcolor bands[5];

	Shade(NVGcolor c) {
		c.a = 1.f;
		bool dark = (c.r + c.g + c.b) / 3 * 255 < look.darkBelow;
		body = c;
		// Flat: one fill per part, a thin edge only to separate overlapping cables
		edge = dark ? mixColor(c, look.edgeToWhiteDark) : mixColor(c, -look.edgeToBlack);
		// Round: a soft cylinder built from nested bands, lit from the upper left
		for (int k = 0; k < 5; k++) {
			bands[k] = mixColor(c, dark ? look.bands[k].mixDark : look.bands[k].mix);
			bands[k].a = look.bands[k].alpha;
		}
	}
};


static void shadowPoint(double x, double y, double z, float* sx, float* sy) {
	double l = std::sqrt(look.light3[0] * look.light3[0] + look.light3[1] * look.light3[1] + look.light3[2] * look.light3[2]);
	double k = z / (look.light3[2] / l);
	*sx = (x - look.light3[0] / l * k) * PX_PER_CM;
	*sy = (y - look.light3[1] / l * k) * PX_PER_CM;
}


static void polyline(NVGcontext* vg, const std::vector<Pt>& a, int s, int e, float shift = 0.f) {
	nvgBeginPath(vg);
	nvgMoveTo(vg, a[s].x + a[s].ox * shift, a[s].y + a[s].oy * shift);
	for (int i = s + 1; i <= e; i++)
		nvgLineTo(vg, a[i].x + a[i].ox * shift, a[i].y + a[i].oy * shift);
}


static void drawShadows(NVGcontext* vg, const std::vector<Entry*>& order, double RC, double RB) {
	float sa = settings::cableShadow;
	if (sa <= 0)
		return;
	bool round = settings::cableStyle == 1;
	for (Entry* e : order) {
		const Cable& c = e->cable;
		// Plug boots
		nvgLineCap(vg, NVG_ROUND);
		nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, look.shadowAlpha * sa));
		nvgStrokeWidth(vg, 2 * RB * PX_PER_CM);
		for (int end = 0; end < 2; end++) {
			double jx = end ? c.bx() : c.ax(), jy = end ? c.by() : c.ay();
			float sx, sy;
			shadowPoint(jx, jy, LB, &sx, &sy);
			nvgBeginPath(vg);
			nvgMoveTo(vg, jx * PX_PER_CM, jy * PX_PER_CM);
			nvgLineTo(vg, sx, sy);
			nvgStroke(vg);
		}
		// Cable, in pieces that fade and widen with height. Butt ends: round ones make dark rings where pieces overlap.
		nvgLineCap(vg, NVG_BUTT);
		const std::vector<Pt>& D = e->dense;
		const int size = look.chunk;
		for (int s = 0; s < (int) D.size() - 1; s += size) {
			int en = std::min((int) D.size() - 1, s + size);
			double zm = (D[s].wz + D[en].wz) / 2;
			float alpha = look.shadowAlpha * sa * std::exp(-zm / look.shadowFade);
			if (alpha < 0.01f)
				continue;
			nvgBeginPath(vg);
			for (int i = s; i <= en; i++) {
				float sx, sy;
				shadowPoint(D[i].wx, D[i].wy, D[i].wz, &sx, &sy);
				if (i == s)
					nvgMoveTo(vg, sx, sy);
				else
					nvgLineTo(vg, sx, sy);
			}
			float lw = 2 * RC * PX_PER_CM * (1 + zm * look.shadowSpread);
			nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, alpha));
			nvgStrokeWidth(vg, lw);
			nvgStroke(vg);
			if (round) {
				nvgStrokeColor(vg, nvgRGBAf(0, 0, 0, alpha * look.shadowBlurAlpha));
				nvgStrokeWidth(vg, lw * look.shadowBlurWidth);
				nvgStroke(vg);
			}
		}
	}
}


static void drawBoot(NVGcontext* vg, Entry* e, int end, double RB, bool round) {
	const Cable& c = e->cable;
	Shade sh(e->color);
	// Seen straight on, the boot is its end face where the cable leaves
	float x = (end ? c.bx() : c.ax()) * PX_PER_CM;
	float y = (end ? c.by() : c.ay()) * PX_PER_CM;
	float r = RB * PX_PER_CM;
	if (!round) {
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, r);
		nvgFillColor(vg, sh.body);
		nvgFill(vg);
		nvgStrokeColor(vg, sh.edge);
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
		return;
	}
	// The prototype's radial gradient from a highlight shifted toward the light (stops 0, 0.35, 0.75, 1),
	// built from NanoVG's two-color gradients
	float hx = x + look.lightX * r * 0.4f, hy = y + look.lightY * r * 0.4f;
	nvgBeginPath(vg);
	nvgCircle(vg, x, y, r);
	nvgFillPaint(vg, nvgRadialGradient(vg, hx, hy, r * 0.35f, r * 0.75f, sh.bands[3], sh.bands[2]));
	nvgFill(vg);
	nvgBeginPath(vg);
	nvgCircle(vg, hx, hy, r * 0.35f);
	nvgFillPaint(vg, nvgRadialGradient(vg, hx, hy, r * 0.05f, r * 0.35f, sh.bands[4], sh.bands[3]));
	nvgFill(vg);
	NVGcolor rim = sh.bands[0];
	NVGcolor rimClear = rim;
	rimClear.a = 0.f;
	nvgBeginPath(vg);
	nvgCircle(vg, x, y, r);
	nvgFillPaint(vg, nvgRadialGradient(vg, x, y, r * 0.75f, r, rimClear, rim));
	nvgFill(vg);
}


static void drawFlatChunk(NVGcontext* vg, Entry* e, int s, int en, float w) {
	const std::vector<Pt>& a = e->dense;
	Shade sh(e->color);
	const int last = a.size() - 1;
	nvgLineCap(vg, NVG_BUTT);
	nvgStrokeColor(vg, sh.edge);
	nvgStrokeWidth(vg, w + look.edgeExtra);
	polyline(vg, a, s, en);
	nvgStroke(vg);
	nvgStrokeColor(vg, sh.body);
	nvgStrokeWidth(vg, w);
	nvgBeginPath(vg);
	for (int i = s; i <= en; i++) {
		float t = (i == s && s > 0) ? -look.seam : (i == en && en < last) ? look.seam : 0.f;
		if (i == s)
			nvgMoveTo(vg, a[i].x + a[i].tx * t, a[i].y + a[i].ty * t);
		else
			nvgLineTo(vg, a[i].x + a[i].tx * t, a[i].y + a[i].ty * t);
	}
	nvgStroke(vg);
	// The cable's own ends stay round (under the boot face)
	for (int i : {s == 0 ? 0 : -1, en == last ? last : -1}) {
		if (i < 0)
			continue;
		nvgBeginPath(vg);
		nvgCircle(vg, a[i].x, a[i].y, (w + look.edgeExtra) / 2);
		nvgFillColor(vg, sh.edge);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, a[i].x, a[i].y, w / 2);
		nvgFillColor(vg, sh.body);
		nvgFill(vg);
	}
	nvgLineCap(vg, NVG_ROUND);
}


static void drawRoundCable(NVGcontext* vg, Entry* e, float w) {
	// One continuous stroke per band: NanoVG cannot clip to the pieces' outlines, so pieces would leave dark rings at the seams
	const std::vector<Pt>& a = e->dense;
	Shade sh(e->color);
	for (int k = 0; k < 5; k++) {
		nvgStrokeColor(vg, sh.bands[k]);
		nvgStrokeWidth(vg, std::max(look.minBandWidth, w * look.bands[k].width));
		polyline(vg, a, 0, a.size() - 1, w * look.bands[k].shift);
		nvgStroke(vg);
	}
}


void Rig::draw(const widget::Widget::DrawArgs& args) {
	Internal& I = *internal;
	if (!enabled() || I.order.empty())
		return;
	NVGcontext* vg = args.vg;
	const double RC = I.world.P.rc(), RB = I.world.P.rb();
	const bool round = settings::cableStyle == 1;
	const float w = std::max((double) look.minWidth, 2 * RC * PX_PER_CM);
	for (Entry* e : I.order)
		densify(e);

	nvgSave(vg);
	nvgLineJoin(vg, NVG_ROUND);
	drawShadows(vg, I.order, RC, RB);
	nvgLineCap(vg, NVG_ROUND);

	// Straight on, depth is the height above the panel. Boots count at their middle.
	struct Item {
		/** 0 boot, 1 flat piece, 2 whole round cable */
		int type;
		Entry* e;
		int a, b;
		double z;
	};
	std::vector<Item> items;
	for (Entry* e : I.order) {
		items.push_back(Item{0, e, 0, 0, LB * 0.5});
		items.push_back(Item{0, e, 1, 0, LB * 0.5});
		const std::vector<Pt>& D = e->dense;
		if (round) {
			double z = 0;
			for (const Pt& p : D)
				z += p.wz;
			items.push_back(Item{2, e, 0, 0, z / D.size()});
			continue;
		}
		const int size = look.chunk;
		for (int s = 0; s < (int) D.size() - 1; s += size) {
			int en = std::min((int) D.size() - 1, s + size);
			double z = 0;
			for (int i = s; i <= en; i++)
				z += D[i].wz;
			items.push_back(Item{1, e, s, en, z / (en - s + 1)});
		}
	}
	std::stable_sort(items.begin(), items.end(), [](const Item& p, const Item& q) {
		return p.z < q.z;
	});
	// A round cable is drawn whole, so it can sort below its own plugs; draw those first then
	std::map<std::pair<Entry*, int>, bool> bootDone;
	for (const Item& it : items) {
		if (it.type == 0) {
			if (bootDone[std::make_pair(it.e, it.a)])
				continue;
			bootDone[std::make_pair(it.e, it.a)] = true;
			drawBoot(vg, it.e, it.a, RB, round);
		}
		else if (it.type == 1) {
			drawFlatChunk(vg, it.e, it.a, it.b, w);
		}
		else {
			for (int end = 0; end < 2; end++) {
				if (!bootDone[std::make_pair(it.e, end)]) {
					bootDone[std::make_pair(it.e, end)] = true;
					drawBoot(vg, it.e, end, RB, round);
				}
			}
			drawRoundCable(vg, it.e, w);
		}
	}
	nvgRestore(vg);
}


// ---------- shaping a cable by hand (spec 5.2) ----------

static float distToSegment(float x, float y, const Pt& a, const Pt& b, float* u) {
	float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
	float t = l2 > 0 ? math::clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.f, 1.f) : 0.f;
	*u = t;
	return std::hypot(x - a.x - dx * t, y - a.y - dy * t);
}


bool Rig::grab(math::Vec pos) {
	Internal& I = *internal;
	if (!enabled())
		return false;
	const double RB = I.world.P.rb();
	const float w = std::max((double) look.minWidth, 2 * I.world.P.rc() * PX_PER_CM);
	float zoom = APP->scene->rackScroll->getZoom();
	if (!(zoom > 0))
		zoom = 1;
	// The plugs belong to the ports (plugging, unplugging, moving a plug)
	for (Entry* e : I.order) {
		if (!e->live)
			continue;
		const Cable& c = e->cable;
		for (int end = 0; end < 2; end++) {
			float jx = (end ? c.bx() : c.ax()) * PX_PER_CM, jy = (end ? c.by() : c.ay()) * PX_PER_CM;
			if (std::hypot(pos.x - jx, pos.y - jy) < RB * PX_PER_CM + 1.f / zoom)
				return false;
		}
	}
	// The topmost cable drawn under the mouse
	Entry* best = NULL;
	double bestZ = -1e9;
	const float tol = w / 2 + 3.f / zoom;
	for (Entry* e : I.order) {
		if (!e->live)
			continue;
		densify(e);
		const std::vector<Pt>& D = e->dense;
		for (int i = 0; i + 1 < (int) D.size(); i++) {
			float u;
			if (distToSegment(pos.x, pos.y, D[i], D[i + 1], &u) < tol) {
				double z = D[i].wz + (D[i + 1].wz - D[i].wz) * u;
				if (z > bestZ) {
					bestZ = z;
					best = e;
				}
			}
		}
	}
	if (!best)
		return false;
	// Hold the nearest free physics point, away from the boots
	Cable& c = best->cable;
	double mx = pos.x / PX_PER_CM, my = pos.y / PX_PER_CM;
	int lo = 3, hi = c.n - 4;
	if (hi < lo) {
		lo = 2;
		hi = c.n - 3;
	}
	int bi = lo;
	double bd = 1e18;
	for (int i = lo; i <= hi; i++) {
		double d = std::hypot(c.x[i] - mx, c.y[i] - my);
		if (d < bd) {
			bd = d;
			bi = i;
		}
	}
	I.grabDx = c.x[bi] - mx;
	I.grabDy = c.y[bi] - my;
	I.world.startDrag(&c, bi, c.x[bi], c.y[bi], c.z[bi]);
	return true;
}


void Rig::dragTo(math::Vec pos) {
	World& W = internal->world;
	if (!W.dragging)
		return;
	W.moveDrag(pos.x / PX_PER_CM + internal->grabDx, pos.y / PX_PER_CM + internal->grabDy, W.drag.tz);
}


void Rig::release() {
	internal->world.endDrag();
}


} // namespace cablephysics
} // namespace app
} // namespace rack
