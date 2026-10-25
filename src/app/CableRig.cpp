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


/** A camera straight in front of the case's centre, like the prototype's (its "camera height 0°" still has perspective).
Only what stands off the panel moves on screen: points at z = 0 stay where they are, so the panels and their ports are untouched. */
struct Camera {
	bool on = false;
	/** cm */
	double cx = 0, cy = 0, D = 58;
	/** Bumped when the camera changes, for the draw cache */
	uint64_t serial = 0;

	double scale(double z) const {
		return on ? D / std::max(1.0, D - z) : 1.0;
	}
	/** World (cm) to rack px */
	void project(double x, double y, double z, float* sx, float* sy) const {
		double k = scale(z);
		*sx = (cx + (x - cx) * k) * PX_PER_CM;
		*sy = (cy + (y - cy) * k) * PX_PER_CM;
	}
	/** Rack px to the world point at height z (cm) */
	void unproject(float sx, float sy, double z, double* x, double* y) const {
		double k = scale(z);
		*x = cx + (sx / PX_PER_CM - cx) / k;
		*y = cy + (sy / PX_PER_CM - cy) / k;
	}
	void update() {
		Camera c;
		c.on = settings::cablePerspective;
		// The case starts at the rack's grid origin, where a patch's module at pos [0, 0] sits
		c.cx = (RACK_OFFSET.x + settings::caseWidth * RACK_GRID_WIDTH / 2) / PX_PER_CM;
		c.cy = (RACK_OFFSET.y + settings::caseHeight / 3 * RACK_GRID_HEIGHT / 2) / PX_PER_CM;
		c.D = settings::cameraDistance;
		if (c.on != on || c.cx != cx || c.cy != cy || c.D != D) {
			on = c.on;
			cx = c.cx;
			cy = c.cy;
			D = c.D;
			serial++;
		}
	}
};

static Camera camera;


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
	uint64_t denseCamera = UINT64_MAX;
	std::vector<Pt> dense;
	/** Mean perspective scale along the cable, for its width */
	double meanScale = 1;
	/** Opacity of the shadows other cables and boots cast on each dense point (round style) */
	std::vector<float> cast;

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
	/** What the cast shadows were worked out for */
	size_t castKey = 0;

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
	camera.update();
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
	if (e->denseVersion == c.version && e->denseCamera == camera.serial)
		return;
	e->denseVersion = c.version;
	e->denseCamera = camera.serial;
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
		camera.project(x, y, z, &p.x, &p.y);
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
	double scale = 0;
	for (const Pt& p : out)
		scale += camera.scale(p.wz);
	e->meanScale = scale / out.size();
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


template <typename T>
static T clampT(T x, T a, T b) {
	return std::max(a, std::min(b, x));
}


static bool isDark(NVGcolor c) {
	return (c.r + c.g + c.b) / 3 * 255 < look.darkBelow;
}


struct Shade {
	NVGcolor body, edge, side, rim;
	NVGcolor bands[5];

	Shade(NVGcolor c) {
		c.a = 1.f;
		bool dark = isDark(c);
		body = c;
		// Flat: one fill per part, a thin edge only to separate overlapping cables
		edge = dark ? mixColor(c, look.edgeToWhiteDark) : mixColor(c, -look.edgeToBlack);
		side = dark ? c : mixColor(c, -look.sideToBlack);
		// Round: a soft cylinder built from nested bands, lit from the upper left
		for (int k = 0; k < 5; k++) {
			bands[k] = mixColor(c, dark ? look.bands[k].mixDark : look.bands[k].mix);
			bands[k].a = look.bands[k].alpha;
		}
		rim = mixColor(c, dark ? look.rimMixDark : look.rimMix);
	}
};


/** Light in 3D, toward the light, normalized */
static void light3(double* lx, double* ly, double* lz) {
	double l = std::sqrt(look.light3[0] * look.light3[0] + look.light3[1] * look.light3[1] + look.light3[2] * look.light3[2]);
	*lx = look.light3[0] / l;
	*ly = look.light3[1] / l;
	*lz = look.light3[2] / l;
}


/** Path around two circles and the tangents between them: a tapered tube seen at a slant */
static void hull(NVGcontext* vg, float x0, float y0, float r0, float x1, float y1, float r1) {
	nvgBeginPath(vg);
	float dx = x1 - x0, dy = y1 - y0, d = std::hypot(dx, dy);
	if (d <= std::fabs(r0 - r1) + 1e-3f) {
		if (r0 >= r1)
			nvgCircle(vg, x0, y0, r0);
		else
			nvgCircle(vg, x1, y1, r1);
		return;
	}
	float a = std::atan2(dy, dx), b = std::acos((r0 - r1) / d);
	nvgArc(vg, x0, y0, r0, a + b, a + 2 * M_PI - b, NVG_CW);
	nvgArc(vg, x1, y1, r1, a - b, a + b, NVG_CW);
	nvgClosePath(vg);
}


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
		// Where the boots stand on the panel, a soft dark ring (the light cannot reach in there from any side)
		for (int end = 0; end < 2; end++) {
			float x = (end ? c.bx() : c.ax()) * PX_PER_CM, y = (end ? c.by() : c.ay()) * PX_PER_CM, r = RB * PX_PER_CM;
			nvgBeginPath(vg);
			nvgCircle(vg, x, y, r * look.contactReach);
			nvgFillPaint(vg, nvgRadialGradient(vg, x, y, r * 0.9f, r * look.contactReach, nvgRGBAf(0, 0, 0, look.contactAlpha * sa), nvgRGBAf(0, 0, 0, 0)));
			nvgFill(vg);
		}
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


/** Base (on the panel) and tip (end face) of a boot on screen, rack px */
static void bootEnds(const Cable& c, int end, double RB, float* bx, float* by, float* tx, float* ty, float* r0, float* r1) {
	double jx = end ? c.bx() : c.ax(), jy = end ? c.by() : c.ay();
	camera.project(jx, jy, 0, bx, by);
	camera.project(jx, jy, LB, tx, ty);
	*r0 = RB * PX_PER_CM * camera.scale(0);
	*r1 = RB * PX_PER_CM * camera.scale(LB);
}


/** Round style: the boot as the solid of revolution in Look::profile, part by part from the panel out,
in the cable's nested bands: along the sides, and around the shoulder that faces the viewer */
static void drawRoundBoot(NVGcontext* vg, Entry* e, int end, double RB) {
	const Cable& c = e->cable;
	const double jx = end ? c.bx() : c.ax(), jy = end ? c.by() : c.ay();
	NVGcolor col = e->color;
	col.a = 1.f;
	Shade sh(col);
	const float gx = look.lightX, gy = look.lightY;
	const int N = sizeof(look.profile) / sizeof(look.profile[0]);
	float X[N], Y[N], R[N];
	for (int k = 0; k < N; k++) {
		const double z = look.profile[k].z * LB;
		camera.project(jx, jy, z, &X[k], &Y[k]);
		R[k] = look.profile[k].r * RB * PX_PER_CM * camera.scale(z);
	}
	// A dark edge around the body, to stand out from the panel
	NVGcolor outline = sh.bands[0];
	outline.a = 0.85f;
	hull(vg, X[0], Y[0], R[0] + 0.6f, X[2], Y[2], R[2] + 0.6f);
	nvgFillColor(vg, outline);
	nvgFill(vg);
	for (int k = 0; k < N - 1; k++) {
		const double dz = (look.profile[k + 1].z - look.profile[k].z) * LB, dr = (look.profile[k].r - look.profile[k + 1].r) * RB;
		const double h = std::hypot(dz, dr);
		if (h <= 0)
			continue;
		// How much the surface faces the viewer
		const float sa = dr / h;
		// Bands shift across the part: the light's direction on screen without its share along the part's axis
		// (straight on, the axis is a point and the whole direction counts)
		const float ax = X[k + 1] - X[k], ay = Y[k + 1] - Y[k], al = std::hypot(ax, ay);
		const float rm = std::max(R[k], R[k + 1]);
		float px = gx, py = gy;
		if (al > 1e-3f) {
			const float ux = ax / al, uy = ay / al, along = gx * ux + gy * uy, t = std::min(1.f, al / rm);
			px -= ux * along * t;
			py -= uy * along * t;
		}
		const float pl = std::hypot(px, py);
		if (pl > 1e-3f) {
			px /= pl;
			py /= pl;
		}
		const float mw = look.minBandWidth / 2;
		if (sa < look.faceAbove) {
			// A side: the cable's own bands along the part, the lighter ones shifted toward the light
			for (int b = 0; b < 5; b++) {
				const float w = look.bands[b].width, o0 = 2 * R[k] * look.bands[b].shift * pl, o1 = 2 * R[k + 1] * look.bands[b].shift * pl;
				hull(vg, X[k] + px * o0, Y[k] + py * o0, std::max(mw, R[k] * w), X[k + 1] + px * o1, Y[k + 1] + py * o1, std::max(mw, R[k + 1] * w));
				nvgFillColor(vg, sh.bands[b]);
				nvgFill(vg);
			}
		}
		else {
			// A face toward the viewer (the shoulder): the bands wrap around it, as across a cable laid in a ring,
			// so it shows a lit crescent toward the light. What lies inside is covered by the next parts.
			const float T = R[k] - R[k + 1];
			for (int b = 0; b < 5; b++) {
				const float r = R[k + 1] + T * look.bands[b].width, o = T * look.bands[b].shift * pl;
				hull(vg, X[k] + px * o, Y[k] + py * o, r, X[k + 1] + px * o, Y[k + 1] + py * o, r);
				nvgFillColor(vg, sh.bands[b]);
				nvgFill(vg);
			}
		}
	}
	// A highlight on the shoulder, toward the light
	{
		const int k = 3;
		const float rs = (R[k - 1] + R[k]) / 2, sx = X[k] + gx * rs, sy = Y[k] + gy * rs, sr = look.shine * RB * PX_PER_CM;
		NVGcolor hi = mixColor(col, 0.85f), clear = hi;
		hi.a = look.shineAlpha;
		clear.a = 0.f;
		nvgBeginPath(vg);
		nvgCircle(vg, sx, sy, sr);
		nvgFillPaint(vg, nvgRadialGradient(vg, sx, sy, 0.f, sr, hi, clear));
		nvgFill(vg);
	}
	// Ribs on the strain relief, and the dark gap where the cable comes out
	const Look::Ring& r0 = look.profile[N - 2];
	const Look::Ring& r1 = look.profile[N - 1];
	NVGcolor groove = sh.bands[0];
	groove.a = 0.55f;
	nvgStrokeColor(vg, groove);
	nvgStrokeWidth(vg, std::max(0.6f, (float) (0.05 * RB * PX_PER_CM)));
	for (float u : look.reliefRibs) {
		const float t = (u - r0.z) / (r1.z - r0.z);
		const double z = u * LB;
		float x, y;
		camera.project(jx, jy, z, &x, &y);
		nvgBeginPath(vg);
		nvgCircle(vg, x, y, (r0.r + (r1.r - r0.r) * t) * RB * PX_PER_CM * camera.scale(z));
		nvgStroke(vg);
	}
	groove.a = 0.8f;
	nvgStrokeColor(vg, groove);
	nvgStrokeWidth(vg, 0.8f);
	nvgBeginPath(vg);
	nvgCircle(vg, X[N - 1], Y[N - 1], R[N - 1]);
	nvgStroke(vg);
}


static void drawBoot(NVGcontext* vg, Entry* e, int end, double RB, bool round) {
	if (round) {
		drawRoundBoot(vg, e, end, RB);
		return;
	}
	const Cable& c = e->cable;
	Shade sh(e->color);
	float bx, by, x, y, r0, r;
	bootEnds(c, end, RB, &bx, &by, &x, &y, &r0, &r);
	// In perspective, boots away from the case's centre show their side (straight on, only the end face)
	float dx = x - bx, dy = y - by, L = std::hypot(dx, dy);
	if (L > 0.5f) {
		dx /= L;
		dy /= L;
		const float nx = -dy, ny = dx;
		nvgBeginPath(vg);
		nvgMoveTo(vg, bx + nx * r0, by + ny * r0);
		nvgLineTo(vg, x + nx * r, y + ny * r);
		nvgLineTo(vg, x - nx * r, y - ny * r);
		nvgLineTo(vg, bx - nx * r0, by - ny * r0);
		nvgClosePath(vg);
		nvgFillColor(vg, sh.side);
		nvgFill(vg);
		nvgBeginPath(vg);
		nvgCircle(vg, bx, by, r0);
		nvgFill(vg);
		nvgStrokeColor(vg, sh.edge);
		nvgStrokeWidth(vg, 1.f);
		nvgBeginPath(vg);
		nvgMoveTo(vg, bx + nx * r0, by + ny * r0);
		nvgLineTo(vg, x + nx * r, y + ny * r);
		nvgMoveTo(vg, bx - nx * r0, by - ny * r0);
		nvgLineTo(vg, x - nx * r, y - ny * r);
		nvgStroke(vg);
		// Ribs near the cable end
		nvgStrokeWidth(vg, std::max(0.7f, r * 0.09f));
		for (float u : look.ribs) {
			float mx = bx + (x - bx) * u, my = by + (y - by) * u, rr = r0 + (r - r0) * u;
			nvgBeginPath(vg);
			nvgMoveTo(vg, mx + nx * rr, my + ny * rr);
			nvgLineTo(vg, mx - nx * rr, my - ny * rr);
			nvgStroke(vg);
		}
	}
	// End face where the cable leaves
	nvgBeginPath(vg);
	nvgCircle(vg, x, y, r);
	nvgFillColor(vg, sh.body);
	nvgFill(vg);
	nvgStrokeColor(vg, sh.edge);
	nvgStrokeWidth(vg, 1.f);
	nvgStroke(vg);
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
		if (k == 0 && w * look.rimWidth >= 0.5f) {
			// Along the dark side, away from the light. Where the cable runs along the light it slides under band 1.
			nvgBeginPath(vg);
			for (size_t i = 0; i < a.size(); i++) {
				float l = a[i].ox * a[i].nx + a[i].oy * a[i].ny;
				float off = -clampT(l / 0.35f, -1.f, 1.f) * w * look.rimShift;
				float x = a[i].x + a[i].nx * off, y = a[i].y + a[i].ny * off;
				if (i == 0)
					nvgMoveTo(vg, x, y);
				else
					nvgLineTo(vg, x, y);
			}
			nvgStrokeColor(vg, sh.rim);
			nvgStrokeWidth(vg, w * look.rimWidth);
			nvgStroke(vg);
		}
	}
}


/** The shadows that cables and boots cast on each cable's dense points (Entry::cast), in the 3D light like the panel shadows.
With `meanZ` (round style, whole cables drawn in that order), only a cable drawn above casts on one below, at least as if lying on it,
so a shadow never falls on the cable that covers its caster. */
static void castShadows(const std::vector<Entry*>& order, double RC, double RB, const std::vector<double>* meanZ) {
	double lx, ly, lz;
	light3(&lx, &ly, &lz);
	// Shadow shift on a lower surface per cm of height (toward the lower right)
	const double sx = -lx / lz, sy = -ly / lz;
	const float sa = settings::cableShadow;
	const int CH = 8;
	struct Box {
		double x0, y0, x1, y1, z0, z1;
	};
	std::vector<std::vector<Box>> boxes(order.size());
	for (size_t o = 0; o < order.size(); o++) {
		const std::vector<Pt>& D = order[o]->dense;
		order[o]->cast.assign(D.size(), 0.f);
		for (int s = 0; s + 1 < (int) D.size(); s += CH) {
			int en = std::min((int) D.size() - 1, s + CH);
			Box b = {1e9, 1e9, -1e9, -1e9, 1e9, -1e9};
			for (int i = s; i <= en; i++) {
				b.x0 = std::min(b.x0, D[i].wx);
				b.y0 = std::min(b.y0, D[i].wy);
				b.x1 = std::max(b.x1, D[i].wx);
				b.y1 = std::max(b.y1, D[i].wy);
				b.z0 = std::min(b.z0, D[i].wz);
				b.z1 = std::max(b.z1, D[i].wz);
			}
			boxes[o].push_back(b);
		}
	}
	// A soft-edged stripe of radius `r` at `dist` from the shadow's middle, `dz` above the receiver
	auto strength = [&](double dist, double r, double dz) {
		const double p = r * look.castSoft + dz * look.castSpread;
		const double f = clampT((r + p - dist) / (2 * p), 0.0, 1.0);
		// Cables lying on each other still part a little; nothing from the same height
		const double g = clampT((dz - 0.5 * RC) / RC, 0.0, 1.0);
		return (float) (look.castAlpha * sa * std::exp(-dz / look.shadowFade) * f * g);
	};
	auto segDist = [](double px, double py, double ax, double ay, double bx, double by, double* t) {
		double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
		*t = l2 > 0 ? clampT(((px - ax) * dx + (py - ay) * dy) / l2, 0.0, 1.0) : 0.0;
		return std::hypot(px - ax - dx * *t, py - ay - dy * *t);
	};
	for (size_t r = 0; r < order.size(); r++) {
		std::vector<Pt>& RD = order[r]->dense;
		std::vector<float>& out = order[r]->cast;
		// Boots: upright cylinders from the panel to LB
		for (size_t o = 0; o < order.size(); o++) {
			const Cable& c = order[o]->cable;
			for (int end = 0; end < 2; end++) {
				double jx = end ? c.bx() : c.ax(), jy = end ? c.by() : c.ay();
				for (size_t i = 0; i < RD.size(); i++) {
					const double top = LB - RD[i].wz;
					if (top <= 0 || std::fabs(RD[i].wx - jx) > RB * 2 + top * sx + 1 || std::fabs(RD[i].wy - jy) > RB * 2 + top * sy + 1)
						continue;
					double t;
					double d = segDist(RD[i].wx, RD[i].wy, jx, jy, jx + sx * top, jy + sy * top, &t);
					out[i] = std::max(out[i], strength(d, RB, std::max(top * t, 0.5 * RC + RC)));
				}
			}
		}
		// Other cables, piece against piece first
		for (size_t o = 0; o < order.size(); o++) {
			if (o == r)
				continue;
			if (meanZ && !((*meanZ)[o] > (*meanZ)[r] || ((*meanZ)[o] == (*meanZ)[r] && o > r)))
				continue;
			const double floor = meanZ ? 2 * RC : -1e9;
			const std::vector<Pt>& CD = order[o]->dense;
			for (size_t rb = 0; rb < boxes[r].size(); rb++) {
				const Box& B = boxes[r][rb];
				for (size_t cb = 0; cb < boxes[o].size(); cb++) {
					const Box& C = boxes[o][cb];
					const double dzMax = std::max(floor, C.z1 - B.z0), dzMin = std::max(floor, std::max(0.0, C.z0 - B.z1));
					if (dzMax <= 0.5 * RC)
						continue;
					const double reach = RC * (1 + look.castSoft) + dzMax * look.castSpread;
					if (C.x0 + sx * dzMin - reach > B.x1 || C.x1 + sx * dzMax + reach < B.x0 || C.y0 + sy * dzMin - reach > B.y1 || C.y1 + sy * dzMax + reach < B.y0)
						continue;
					const int s = rb * CH, en = std::min((int) RD.size() - 1, s + CH);
					const int cs = cb * CH, cen = std::min((int) CD.size() - 1, cs + CH);
					for (int i = s; i <= en; i++) {
						const Pt& P = RD[i];
						for (int j = cs; j < cen; j++) {
							const Pt& Q0 = CD[j];
							const Pt& Q1 = CD[j + 1];
							const double d0 = std::max(floor, Q0.wz - P.wz), d1 = std::max(floor, Q1.wz - P.wz);
							if (d0 <= 0.5 * RC && d1 <= 0.5 * RC)
								continue;
							double t;
							double d = segDist(P.wx, P.wy, Q0.wx + sx * std::max(d0, 0.0), Q0.wy + sy * std::max(d0, 0.0), Q1.wx + sx * std::max(d1, 0.0), Q1.wy + sy * std::max(d1, 0.0), &t);
							out[i] = std::max(out[i], strength(d, RC, d0 + (d1 - d0) * t));
						}
					}
				}
			}
		}
	}
}


/** The cast shadows on dense points s..en of a cable: black along it, no wider than the cable,
each step fading from its start's opacity to its end's */
static void drawCast(NVGcontext* vg, Entry* e, int s, int en, float w) {
	const std::vector<Pt>& a = e->dense;
	const std::vector<float>& k = e->cast;
	if (k.size() != a.size())
		return;
	nvgLineCap(vg, NVG_BUTT);
	nvgStrokeWidth(vg, w);
	for (int i = s; i < en; i++) {
		if (k[i] < 0.005f && k[i + 1] < 0.005f)
			continue;
		nvgBeginPath(vg);
		nvgMoveTo(vg, a[i].x, a[i].y);
		nvgLineTo(vg, a[i + 1].x, a[i + 1].y);
		nvgStrokePaint(vg, nvgLinearGradient(vg, a[i].x, a[i].y, a[i + 1].x, a[i + 1].y, nvgRGBAf(0, 0, 0, k[i]), nvgRGBAf(0, 0, 0, k[i + 1])));
		nvgStroke(vg);
	}
	nvgLineCap(vg, NVG_ROUND);
}


void Rig::draw(const widget::Widget::DrawArgs& args) {
	Internal& I = *internal;
	if (!enabled() || I.order.empty())
		return;
	NVGcontext* vg = args.vg;
	const double RC = I.world.P.rc(), RB = I.world.P.rb();
	const bool round = settings::cableStyle == 1;
	for (Entry* e : I.order)
		densify(e);
	// Cast shadows only change when a cable moves, the camera or the shadow setting changes
	size_t key = std::hash<double>()(settings::cableShadow) ^ (camera.serial * 0x9e3779b97f4a7c15ULL) ^ round;
	for (Entry* e : I.order)
		key = key * 1000003 ^ std::hash<const void*>()(e) ^ e->cable.version;
	if (key != I.castKey) {
		I.castKey = key;
		if (settings::cableShadow > 0) {
			// Round cables are drawn whole by their mean height (below)
			std::vector<double> meanZ;
			for (Entry* e : I.order) {
				double z = 0;
				for (const Pt& p : e->dense)
					z += p.wz;
				meanZ.push_back(z / std::max<size_t>(1, e->dense.size()));
			}
			castShadows(I.order, RC, RB, round ? &meanZ : NULL);
		}
		else
			for (Entry* e : I.order)
				e->cast.clear();
	}
	// Width: nearer the camera is wider (one width per cable, as the prototype)
	auto widthOf = [&](Entry* e) {
		return (float) std::max((double) look.minWidth, 2 * RC * PX_PER_CM * e->meanScale);
	};

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
			drawFlatChunk(vg, it.e, it.a, it.b, widthOf(it.e));
			drawCast(vg, it.e, it.a, it.b, widthOf(it.e));
		}
		else {
			for (int end = 0; end < 2; end++) {
				if (!bootDone[std::make_pair(it.e, end)]) {
					bootDone[std::make_pair(it.e, end)] = true;
					drawBoot(vg, it.e, end, RB, round);
				}
			}
			drawRoundCable(vg, it.e, widthOf(it.e));
			drawCast(vg, it.e, 0, it.e->dense.size() - 1, widthOf(it.e));
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


PortWidget* Rig::plugAt(math::Vec pos) {
	Internal& I = *internal;
	if (!enabled())
		return NULL;
	const double RB = I.world.P.rb();
	float zoom = APP->scene->rackScroll->getZoom();
	if (!(zoom > 0))
		zoom = 1;
	PortWidget* best = NULL;
	float bestD = 1e9;
	for (Entry* e : I.order) {
		if (!e->live)
			continue;
		for (int end = 0; end < 2; end++) {
			Pt b, t;
			float r0, r1;
			bootEnds(e->cable, end, RB, &b.x, &b.y, &t.x, &t.y, &r0, &r1);
			float u;
			float d = distToSegment(pos.x, pos.y, b, t, &u);
			if (d < r1 + 1.f / zoom && d < bestD) {
				bestD = d;
				// End A is the output
				best = end ? e->inputPort : e->outputPort;
			}
		}
	}
	return best;
}


bool Rig::grab(math::Vec pos) {
	Internal& I = *internal;
	if (!enabled())
		return false;
	float zoom = APP->scene->rackScroll->getZoom();
	if (!(zoom > 0))
		zoom = 1;
	// The plugs belong to the ports (plugging, unplugging, moving a plug)
	if (plugAt(pos))
		return false;
	// The topmost cable drawn under the mouse
	Entry* best = NULL;
	double bestZ = -1e9;
	for (Entry* e : I.order) {
		if (!e->live)
			continue;
		densify(e);
		const float tol = std::max((double) look.minWidth, 2 * I.world.P.rc() * PX_PER_CM * e->meanScale) / 2 + 3.f / zoom;
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
	// Hold the nearest free physics point on screen, away from the boots
	Cable& c = best->cable;
	int lo = 3, hi = c.n - 4;
	if (hi < lo) {
		lo = 2;
		hi = c.n - 3;
	}
	int bi = lo;
	double bd = 1e18;
	for (int i = lo; i <= hi; i++) {
		float sx, sy;
		camera.project(c.x[i], c.y[i], c.z[i], &sx, &sy);
		double d = std::hypot(sx - pos.x, sy - pos.y);
		if (d < bd) {
			bd = d;
			bi = i;
		}
	}
	// The mouse moves the point at the height it was grabbed
	double mx, my;
	camera.unproject(pos.x, pos.y, c.z[bi], &mx, &my);
	I.grabDx = c.x[bi] - mx;
	I.grabDy = c.y[bi] - my;
	I.world.startDrag(&c, bi, c.x[bi], c.y[bi], c.z[bi]);
	return true;
}


void Rig::dragTo(math::Vec pos) {
	World& W = internal->world;
	if (!W.dragging)
		return;
	double mx, my;
	camera.unproject(pos.x, pos.y, W.drag.tz, &mx, &my);
	W.moveDrag(mx + internal->grabDx, my + internal->grabDy, W.drag.tz);
}


void Rig::release() {
	internal->world.endDrag();
}


} // namespace cablephysics
} // namespace app
} // namespace rack
