#include <map>
#include <vector>
#include <random>
#include <algorithm>
#include <cmath>
#include <app/PanelLook.hpp>
#include <app/CableRig.hpp>
#include <app/CablePhysics.hpp>
#include <app/RackWidget.hpp>
#include <app/ModuleWidget.hpp>
#include <app/ParamWidget.hpp>
#include <app/PortWidget.hpp>
#include <app/Knob.hpp>
#include <app/SliderKnob.hpp>
#include <app/SvgKnob.hpp>
#include <app/SvgPort.hpp>
#include <widget/SvgWidget.hpp>
#include <window/Svg.hpp>
#include <engine/Module.hpp>
#include <plugin/Model.hpp>
#include <settings.hpp>


namespace rack {
namespace app {
namespace panellook {


using cablephysics::PX_PER_CM;

Look look;


bool enabled() {
	return settings::panelLook;
}

bool perspective() {
	return enabled() && settings::knobPerspective && cablephysics::enabled() && settings::cablePerspective;
}


// ---------- what the SVGs show ----------

/** A knob's or jack's size and colour, read from the SVGs it is drawn with */
struct PartInfo {
	/** Which SVGs it was read from, to notice a theme change */
	std::vector<const window::Svg*> svgs;
	math::Vec size;
	/** Radius (px) of the largest round shape about the centre: the knob's skirt, the jack's nut or washer */
	float outer = 0.f;
	/** Radius of the smallest dark round shape about the centre: the jack's hole. 0 if none. */
	float hole = 0.f;
	/** Colour of the outer shape */
	NVGcolor body = nvgRGBf(0.2f, 0.2f, 0.2f);
};

static std::map<const widget::Widget*, PartInfo> partInfos;


static NVGcolor svgColor(unsigned int c) {
	return nvgRGB(c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
}

/** A solid fill's colour, or a gradient's mean */
static bool paintColor(const NSVGpaint& paint, NVGcolor* color) {
	if (paint.type == NSVG_PAINT_COLOR) {
		*color = svgColor(paint.color);
		return true;
	}
	if ((paint.type == NSVG_PAINT_LINEAR_GRADIENT || paint.type == NSVG_PAINT_RADIAL_GRADIENT) && paint.gradient && paint.gradient->nstops > 0) {
		float r = 0, g = 0, b = 0;
		int n = paint.gradient->nstops;
		for (int i = 0; i < n; i++) {
			NVGcolor s = svgColor(paint.gradient->stops[i].color);
			r += s.r;
			g += s.g;
			b += s.b;
		}
		*color = nvgRGBf(r / n, g / n, b / n);
		return true;
	}
	return false;
}

static float luma(NVGcolor c) {
	return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b;
}

static void collectSvgs(widget::Widget* w, math::Vec offset, std::vector<std::pair<const window::Svg*, math::Vec>>& out) {
	for (widget::Widget* child : w->children) {
		math::Vec o = offset.plus(child->box.pos);
		widget::SvgWidget* sw = dynamic_cast<widget::SvgWidget*>(child);
		if (sw && sw->isVisible() && sw->svg && sw->svg->handle)
			out.push_back(std::make_pair(sw->svg.get(), o));
		collectSvgs(child, o, out);
	}
}

static const PartInfo& partInfo(widget::Widget* w) {
	std::vector<std::pair<const window::Svg*, math::Vec>> found;
	collectSvgs(w, math::Vec(), found);
	std::vector<const window::Svg*> svgs;
	for (auto& f : found)
		svgs.push_back(f.first);

	PartInfo& info = partInfos[w];
	if (info.svgs == svgs && info.size.equals(w->box.size) && info.outer > 0.f)
		return info;

	info = PartInfo();
	info.svgs = svgs;
	info.size = w->box.size;
	math::Vec c = w->box.size.div(2);
	float half = std::min(w->box.size.x, w->box.size.y) / 2;
	float bestOuter = 0.f, bestHole = 1e9f;
	for (auto& f : found) {
		for (NSVGshape* shape = f.first->handle->shapes; shape; shape = shape->next) {
			if (!(shape->flags & NSVG_FLAGS_VISIBLE) || shape->opacity < 0.3f)
				continue;
			NVGcolor color;
			if (!paintColor(shape->fill, &color))
				continue;
			float x0 = shape->bounds[0] + f.second.x, y0 = shape->bounds[1] + f.second.y;
			float x1 = shape->bounds[2] + f.second.x, y1 = shape->bounds[3] + f.second.y;
			float sw = x1 - x0, sh = y1 - y0;
			float r = std::max(sw, sh) / 2;
			math::Vec sc((x0 + x1) / 2, (y0 + y1) / 2);
			// Round, and about the centre (the knob turns about it, the plug goes in there)
			if (!(r > 0.f && std::fabs(sw - sh) < 0.15f * 2 * r && sc.minus(c).norm() < 0.08f * 2 * half + 0.5f))
				continue;
			if (r <= half * 1.05f && r > bestOuter) {
				bestOuter = r;
				info.body = color;
			}
			if (luma(color) < 0.2f && r >= 0.12f * 2 * half && r < bestHole)
				bestHole = r;
		}
	}
	info.outer = bestOuter > 0.f ? bestOuter : half;
	info.hole = bestHole < info.outer ? bestHole : 0.f;
	return info;
}


// ---------- shapes ----------

static math::Vec light2() {
	return math::Vec(cablephysics::look.lightX, cablephysics::look.lightY).normalize();
}

/** Shadow offset on the panel per cm of height, px */
static math::Vec shadowPerCm() {
	const float* l = cablephysics::look.light3;
	return math::Vec(-l[0] / l[2], -l[1] / l[2]).mult(PX_PER_CM);
}

/** The outline around two circles (a cylinder's or a shadow's silhouette) */
static void hull(NVGcontext* vg, math::Vec a, float ra, math::Vec b, float rb) {
	nvgBeginPath(vg);
	math::Vec d = b.minus(a);
	float dist = d.norm();
	if (dist <= std::fabs(ra - rb) + 1e-3f) {
		if (ra >= rb)
			nvgCircle(vg, a.x, a.y, ra);
		else
			nvgCircle(vg, b.x, b.y, rb);
		return;
	}
	float th = std::atan2(d.y, d.x);
	float ph = std::acos(math::clamp((ra - rb) / dist, -1.f, 1.f));
	nvgArc(vg, a.x, a.y, ra, th + ph, th + 2 * M_PI - ph, NVG_CW);
	nvgArc(vg, b.x, b.y, rb, th - ph, th + ph, NVG_CW);
	nvgClosePath(vg);
}

/** Where two discs overlap. False if they do not. */
static bool lens(NVGcontext* vg, math::Vec a, float ra, math::Vec b, float rb) {
	float dist = b.minus(a).norm();
	nvgBeginPath(vg);
	if (dist + rb <= ra) {
		nvgCircle(vg, b.x, b.y, rb);
		return true;
	}
	if (dist + ra <= rb) {
		nvgCircle(vg, a.x, a.y, ra);
		return true;
	}
	if (dist >= ra + rb)
		return false;
	std::vector<math::Vec> pts;
	const int n = 48;
	for (int i = 0; i < n; i++) {
		float t = 2 * M_PI * i / n;
		math::Vec u(std::cos(t), std::sin(t));
		math::Vec pa = a.plus(u.mult(ra)), pb = b.plus(u.mult(rb));
		if (pa.minus(b).norm() <= rb)
			pts.push_back(pa);
		if (pb.minus(a).norm() <= ra)
			pts.push_back(pb);
	}
	if (pts.size() < 3)
		return false;
	math::Vec m;
	for (math::Vec p : pts)
		m = m.plus(p);
	m = m.div(pts.size());
	std::sort(pts.begin(), pts.end(), [&](math::Vec p, math::Vec q) {
		return std::atan2(p.y - m.y, p.x - m.x) < std::atan2(q.y - m.y, q.x - m.x);
	});
	nvgMoveTo(vg, pts[0].x, pts[0].y);
	for (size_t i = 1; i < pts.size(); i++)
		nvgLineTo(vg, pts[i].x, pts[i].y);
	nvgClosePath(vg);
	return true;
}

static NVGcolor mixTo(NVGcolor c, float to, float amount) {
	return nvgRGBf(c.r + (to - c.r) * amount, c.g + (to - c.g) * amount, c.b + (to - c.b) * amount);
}

static NVGcolor gray(float v, float a) {
	return nvgRGBAf(v, v, v, a);
}


/** A part `H` cm tall standing at `p` with radius `R` (px): its shadow on the panel, and the dark ring where it meets the panel */
static void drawShadow(NVGcontext* vg, math::Vec p, float R, double H) {
	const cablephysics::Look& cl = cablephysics::look;
	float a0 = look.shadowAlpha * settings::cableShadow;
	if (a0 <= 0.f)
		return;
	math::Vec q = p.plus(shadowPerCm().mult(H));
	float fade = std::exp(-H / cl.shadowFade);
	float Rq = R * (1 + H * cl.shadowSpread);
	float blur = 1 + look.shadowBlur;
	hull(vg, p, R * blur, q, Rq * blur);
	nvgFillPaint(vg, nvgLinearGradient(vg, p.x, p.y, q.x, q.y, gray(0, a0 * look.shadowBlurAlpha), gray(0, a0 * look.shadowBlurAlpha * fade)));
	nvgFill(vg);
	hull(vg, p, R, q, Rq);
	nvgFillPaint(vg, nvgLinearGradient(vg, p.x, p.y, q.x, q.y, gray(0, a0), gray(0, a0 * fade)));
	nvgFill(vg);
	nvgBeginPath(vg);
	nvgCircle(vg, p.x, p.y, R * look.contactReach);
	nvgFillPaint(vg, nvgRadialGradient(vg, p.x, p.y, R, R * look.contactReach, gray(0, look.contactAlpha * settings::cableShadow), gray(0, 0)));
	nvgFill(vg);
}


/** Fixed light on a knob's face at `q`, radius `r`: it stays put while the knob turns */
static void drawFace(NVGcontext* vg, math::Vec q, float r) {
	math::Vec L = light2();
	math::Vec lit = q.plus(L.mult(r)), dark = q.minus(L.mult(r));
	nvgBeginPath(vg);
	nvgCircle(vg, q.x, q.y, r);
	nvgFillPaint(vg, nvgLinearGradient(vg, lit.x, lit.y, q.x, q.y, gray(1, look.faceLight), gray(1, 0)));
	nvgFill(vg);
	nvgFillPaint(vg, nvgLinearGradient(vg, q.x, q.y, dark.x, dark.y, gray(0, 0), gray(0, look.faceDark)));
	nvgFill(vg);
	// The edge catches the light on the lit side and falls dark on the other, fading toward the middle of each arc's ends
	float w = r * look.rimWidth;
	float a = std::atan2(L.y, L.x);
	nvgStrokeWidth(vg, w);
	nvgLineCap(vg, NVG_BUTT);
	nvgBeginPath(vg);
	nvgArc(vg, q.x, q.y, r - w / 2, a - look.rimArc, a + look.rimArc, NVG_CW);
	nvgStrokePaint(vg, nvgLinearGradient(vg, lit.x, lit.y, q.x, q.y, gray(1, look.rimAlpha), gray(1, 0)));
	nvgStroke(vg);
	nvgBeginPath(vg);
	nvgArc(vg, q.x, q.y, r - w / 2, a + M_PI - look.rimArc, a + M_PI + look.rimArc, NVG_CW);
	nvgStrokePaint(vg, nvgLinearGradient(vg, dark.x, dark.y, q.x, q.y, gray(0, look.rimAlpha), gray(0, 0)));
	nvgStroke(vg);
}


/** The jack's nut in relief and its hole sunk in, over the port's own SVG. `floorShift` moves the hole's floor (px) as the camera sees it, `floorScale` shrinks it. */
static void drawJack(NVGcontext* vg, PortWidget* pw, math::Vec floorShift, float floorScale) {
	const PartInfo& info = partInfo(pw);
	math::Vec c = pw->box.size.div(2);
	math::Vec L = light2();
	float R = info.outer;
	math::Vec lit = c.plus(L.mult(R)), dark = c.minus(L.mult(R));
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, R);
	nvgFillPaint(vg, nvgLinearGradient(vg, lit.x, lit.y, c.x, c.y, gray(1, look.nutLight), gray(1, 0)));
	nvgFill(vg);
	nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y, dark.x, dark.y, gray(0, 0), gray(0, look.nutDark)));
	nvgFill(vg);

	float r = info.hole > 0.f ? info.hole : std::min(window::mm2px(look.holeRadius), 0.4f * R);
	// The wall: in shade under the upper left lip, lit on the lower right
	nvgBeginPath(vg);
	nvgCircle(vg, c.x, c.y, r);
	nvgFillPaint(vg, nvgLinearGradient(vg, c.x + L.x * r, c.y + L.y * r, c.x - L.x * r, c.y - L.y * r, gray(look.wallShade, 1), gray(look.wallLit, 1)));
	nvgFill(vg);
	// What of the floor shows through the opening
	if (lens(vg, c, r, c.plus(floorShift), r * look.holeFloor * floorScale)) {
		nvgFillColor(vg, gray(0.01f, 1));
		nvgFill(vg);
	}
	// The lip catches the light on the lower right
	float a = std::atan2(-L.y, -L.x);
	float w = window::mm2px(look.lipWidth);
	nvgBeginPath(vg);
	nvgArc(vg, c.x, c.y, r + w / 2, a - 1.2f, a + 1.2f, NVG_CW);
	nvgStrokeWidth(vg, w);
	nvgLineCap(vg, NVG_BUTT);
	nvgStrokePaint(vg, nvgLinearGradient(vg, c.x - L.x * r, c.y - L.y * r, c.x, c.y, gray(1, look.lipAlpha), gray(1, 0)));
	nvgStroke(vg);
}


// ---------- the panel's surface ----------

/** A tile of grain: fine speckle and vertical brushing, as white or black of varying opacity (premultiplied) */
static int grainImage(NVGcontext* vg) {
	static std::map<NVGcontext*, int> images;
	auto it = images.find(vg);
	if (it != images.end())
		return it->second;
	const int N = 256;
	std::mt19937 rng(20261025);
	std::uniform_real_distribution<float> uni(-1.f, 1.f);
	std::vector<float> fine(N * N), streak(N * N);
	for (float& v : fine)
		v = uni(rng);
	std::vector<float> col(N * N);
	for (float& v : col)
		v = uni(rng);
	int len = std::max(1, look.brushLength);
	for (int x = 0; x < N; x++) {
		for (int y = 0; y < N; y++) {
			float s = 0;
			for (int k = -len; k <= len; k++)
				s += col[((y + k + N) % N) * N + x];
			streak[y * N + x] = s / std::sqrt(2.f * len + 1);
		}
	}
	std::vector<unsigned char> rgba(N * N * 4);
	for (int i = 0; i < N * N; i++) {
		float v = math::clamp(look.speckle * fine[i] + look.brush * streak[i] * 0.6f, -1.f, 1.f);
		unsigned char a = (unsigned char) std::round(std::fabs(v) * 255);
		unsigned char w = v > 0 ? a : 0;
		rgba[i * 4 + 0] = w;
		rgba[i * 4 + 1] = w;
		rgba[i * 4 + 2] = w;
		rgba[i * 4 + 3] = a;
	}
	int image = nvgCreateImageRGBA(vg, N, N, NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY | NVG_IMAGE_GENERATE_MIPMAPS | NVG_IMAGE_PREMULTIPLIED, rgba.data());
	images[vg] = image;
	return image;
}

/** Grain, a broad sheen across the case, and bevelled edges, over the panel */
static void drawSurface(ModuleWidget* mw, widget::Widget* panel, NVGcontext* vg, bool inRack) {
	float t = settings::panelTexture;
	if (t <= 0.f)
		return;
	math::Vec size = panel->box.size;
	math::Vec o = panel->box.pos;
	math::Vec L = light2();

	// Sheen: the case is lit from the upper left, so is every panel in it
	math::Vec c, e;
	if (inRack) {
		math::Vec caseSize(settings::caseWidth * RACK_GRID_WIDTH, settings::caseHeight / 3 * RACK_GRID_HEIGHT);
		c = RACK_OFFSET.plus(caseSize.div(2)).minus(mw->box.pos);
		e = L.mult(caseSize.norm() / 2);
	}
	else {
		c = o.plus(size.div(2));
		e = L.mult(size.norm() / 2);
	}
	nvgBeginPath(vg);
	nvgRect(vg, o.x, o.y, size.x, size.y);
	nvgFillPaint(vg, nvgLinearGradient(vg, c.x + e.x, c.y + e.y, c.x, c.y, gray(1, look.sheenLight * t), gray(1, 0)));
	nvgFill(vg);
	nvgFillPaint(vg, nvgLinearGradient(vg, c.x, c.y, c.x - e.x, c.y - e.y, gray(0, 0), gray(0, look.sheenDark * t)));
	nvgFill(vg);

	// Grain, faded out when its texels get smaller than a screen pixel (they would only shimmer)
	float xf[6];
	nvgCurrentTransform(vg, xf);
	float texel = window::mm2px(look.grainTexel);
	float fade = math::clamp((texel * xf[0] - 0.4f) / 0.8f, 0.f, 1.f);
	if (fade > 0.f) {
		// Each model's panels start the tile somewhere else
		size_t h = mw->model ? std::hash<std::string>()(mw->model->slug) : 0;
		float tile = texel * 256;
		float ox = o.x - (h % 997) / 997.f * tile, oy = o.y - (h / 997 % 991) / 991.f * tile;
		nvgBeginPath(vg);
		nvgRect(vg, o.x, o.y, size.x, size.y);
		nvgFillPaint(vg, nvgImagePattern(vg, ox, oy, tile, tile, 0.f, grainImage(vg), look.grainAlpha * t * fade));
		nvgFill(vg);
	}

	// Edges: the plate's bevel catches the light on the top and left
	float b = window::mm2px(look.bevel);
	struct Edge {
		float x, y, w, h, x0, y0, x1, y1;
		bool lit;
	};
	Edge edges[4] = {
		{o.x, o.y, size.x, b, 0, o.y, 0, o.y + b, true},
		{o.x, o.y, b, size.y, o.x, 0, o.x + b, 0, true},
		{o.x, o.y + size.y - b, size.x, b, 0, o.y + size.y, 0, o.y + size.y - b, false},
		{o.x + size.x - b, o.y, b, size.y, o.x + size.x, 0, o.x + size.x - b, 0, false},
	};
	for (const Edge& g : edges) {
		float v = g.lit ? 1.f : 0.f;
		float a = (g.lit ? look.bevelLight : look.bevelDark) * t;
		nvgBeginPath(vg);
		nvgRect(vg, g.x, g.y, g.w, g.h);
		nvgFillPaint(vg, nvgLinearGradient(vg, g.x0, g.y0, g.x1, g.y1, gray(v, a), gray(v, 0)));
		nvgFill(vg);
	}
}


// ---------- passes ----------

enum Pass {
	/** Outside the rack: a module draws everything itself */
	ALL,
	PANELS,
	PARTS,
};
static Pass pass = ALL;
static uint64_t frame = 0;
/** The frame in which a module's parts were last drawn here. A module whose own draw() skips ModuleWidget::draw() never is: it keeps its knobs. */
static std::map<const ModuleWidget*, uint64_t> handled;


bool panelPass() {
	return pass == PANELS;
}


/** A knob standing on the panel: a Knob child of the module, not a fader */
static bool raisedKnob(widget::Widget* w, double* R, double* H) {
	if (!w->isVisible())
		return false;
	Knob* k = dynamic_cast<Knob*>(w);
	if (!k || dynamic_cast<SliderKnob*>(w))
		return false;
	return cablephysics::partSize(k, R, H);
}

/** SVG knobs and jacks bring a round shadow of their own, straight down; the panel look draws its own */
static void ownShadow(widget::Widget* w, bool show) {
	CircularShadow* shadow = NULL;
	widget::FramebufferWidget* fb = NULL;
	if (SvgKnob* k = dynamic_cast<SvgKnob*>(w)) {
		shadow = k->shadow;
		fb = k->fb;
	}
	else if (SvgPort* p = dynamic_cast<SvgPort*>(w)) {
		shadow = p->shadow;
		fb = p->fb;
	}
	if (shadow && shadow->isVisible() != show) {
		shadow->setVisible(show);
		fb->setDirty();
	}
}

void restoreShadows(ModuleWidget* mw) {
	for (widget::Widget* child : mw->children)
		ownShadow(child, true);
}


/** Shadows of the module's knobs and jacks, in its own coordinates */
static void drawShadows(ModuleWidget* mw, NVGcontext* vg) {
	for (widget::Widget* child : mw->children) {
		double R, H;
		if (raisedKnob(child, &R, &H)) {
			drawShadow(vg, child->box.getCenter(), partInfo(child).outer, H * look.faceHeight);
		}
		else if (PortWidget* pw = dynamic_cast<PortWidget*>(child)) {
			if (pw->isVisible())
				drawShadow(vg, pw->box.getCenter(), partInfo(pw).outer, look.jackHeight);
		}
	}
}


void drawModule(ModuleWidget* mw, const widget::Widget::DrawArgs& args) {
	NVGcontext* vg = args.vg;
	widget::Widget* panel = mw->getPanel();
	bool inRack = pass != ALL;
	if (pass != PARTS && panel && panel->isVisible()) {
		mw->drawChild(panel, args);
		drawSurface(mw, panel, vg, inRack);
	}
	if (pass == PANELS)
		return;
	if (pass == ALL)
		drawShadows(mw, vg);
	bool persp = inRack && perspective();
	for (widget::Widget* child : mw->children) {
		if (child == panel || !child->isVisible())
			continue;
		double R, H;
		bool knob = raisedKnob(child, &R, &H);
		if (knob || dynamic_cast<PortWidget*>(child))
			ownShadow(child, false);
		// Knobs stand above everything on the panel: the rack draws them after every module's parts
		if (knob && inRack)
			continue;
		if (!args.clipBox.intersects(child->box))
			continue;
		mw->drawChild(child, args);
		if (knob) {
			drawFace(vg, child->box.getCenter(), partInfo(child).outer);
		}
		else if (PortWidget* pw = dynamic_cast<PortWidget*>(child)) {
			math::Vec shift;
			double k = 1;
			if (persp) {
				math::Vec p = mw->box.pos.plus(pw->box.getCenter());
				shift = cablephysics::project(p, -look.holeDepth, &k).minus(p);
			}
			nvgSave(vg);
			nvgTranslate(vg, pw->box.pos.x, pw->box.pos.y);
			drawJack(vg, pw, shift, k);
			nvgRestore(vg);
		}
	}
	if (inRack)
		handled[mw] = frame;
}


/** The module's knobs, after every module's parts: the side (in perspective) and the face, raised to the knob's height */
static void drawKnobs(ModuleWidget* mw, const widget::Widget::DrawArgs& args) {
	NVGcontext* vg = args.vg;
	bool persp = perspective();
	math::Vec L = light2();
	for (widget::Widget* child : mw->children) {
		double R, H;
		if (!raisedKnob(child, &R, &H))
			continue;
		const PartInfo& info = partInfo(child);
		math::Vec p = mw->box.pos.plus(child->box.getCenter());
		double k = 1;
		math::Vec q = persp ? cablephysics::project(p, H * look.faceHeight, &k) : p;
		float r = info.outer * k;
		math::Rect bounds = math::Rect(q.minus(math::Vec(r, r)), math::Vec(2 * r, 2 * r)).expand(math::Rect(p.minus(math::Vec(info.outer, info.outer)), math::Vec(2 * info.outer, 2 * info.outer)));
		if (!args.clipBox.intersects(bounds))
			continue;
		// The side shows when the face has moved off the base
		math::Vec d = q.minus(p);
		if (d.norm() > 0.3f) {
			math::Vec n = math::Vec(-d.y, d.x).normalize();
			if (n.dot(L) < 0)
				n = n.neg();
			hull(vg, p, info.outer, q, r);
			math::Vec a = p.plus(d.div(2)).plus(n.mult(info.outer)), b = p.plus(d.div(2)).minus(n.mult(info.outer));
			nvgFillPaint(vg, nvgLinearGradient(vg, a.x, a.y, b.x, b.y, mixTo(info.body, 1, look.sideLight), mixTo(info.body, 0, look.sideDark)));
			nvgFill(vg);
		}
		// The knob itself, magnified as its height brings it closer
		widget::Widget::DrawArgs childArgs = args;
		childArgs.clipBox = child->box.zeroPos();
		nvgSave(vg);
		nvgTranslate(vg, q.x, q.y);
		nvgScale(vg, k, k);
		nvgTranslate(vg, -child->box.size.x / 2, -child->box.size.y / 2);
		child->draw(childArgs);
		nvgRestore(vg);
		drawFace(vg, q, r);
	}
}


void drawModules(widget::Widget* container, const widget::Widget::DrawArgs& args) {
	NVGcontext* vg = args.vg;
	frame++;
	// Shadows and knob tops reach this far beyond their module
	float margin = 3 * PX_PER_CM;
	auto shown = [&](widget::Widget* w, float m) {
		return w->isVisible() && args.clipBox.intersects(w->box.grow(math::Vec(m, m)));
	};
	auto faded = [&](ModuleWidget* mw) {
		return mw->module && mw->module->isBypassed();
	};

	pass = PANELS;
	for (widget::Widget* child : container->children) {
		if (dynamic_cast<ModuleWidget*>(child) && shown(child, 0))
			container->drawChild(child, args);
	}
	pass = ALL;

	for (widget::Widget* child : container->children) {
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(child);
		if (!mw || !shown(mw, margin) || handled[mw] + 1 < frame)
			continue;
		nvgSave(vg);
		nvgTranslate(vg, mw->box.pos.x, mw->box.pos.y);
		if (faded(mw))
			nvgAlpha(vg, 0.33);
		drawShadows(mw, vg);
		nvgRestore(vg);
	}

	pass = PARTS;
	for (widget::Widget* child : container->children) {
		if (shown(child, 0))
			container->drawChild(child, args);
	}
	pass = ALL;

	for (widget::Widget* child : container->children) {
		ModuleWidget* mw = dynamic_cast<ModuleWidget*>(child);
		if (!mw || !shown(mw, margin) || handled[mw] != frame)
			continue;
		nvgSave(vg);
		if (faded(mw))
			nvgAlpha(vg, 0.33);
		drawKnobs(mw, args);
		nvgRestore(vg);
	}

	// Forget modules that have gone
	if (handled.size() > 2 * container->children.size() + 16) {
		for (auto it = handled.begin(); it != handled.end();) {
			if (it->second + 2 < frame)
				it = handled.erase(it);
			else
				++it;
		}
	}
}


ParamWidget* knobAt(RackWidget* rack, math::Vec pos) {
	if (!perspective())
		return NULL;
	ParamWidget* hit = NULL;
	for (ModuleWidget* mw : rack->getModules()) {
		for (widget::Widget* child : mw->children) {
			double R, H;
			if (!raisedKnob(child, &R, &H))
				continue;
			math::Vec p = mw->box.pos.plus(child->box.getCenter());
			double k;
			math::Vec q = cablephysics::project(p, H * look.faceHeight, &k);
			// Knob::onButton() takes presses within half its width
			if (pos.minus(q).norm() <= child->box.size.x / 2 * k)
				hit = dynamic_cast<ParamWidget*>(child);
		}
	}
	return hit;
}


} // namespace panellook
} // namespace app
} // namespace rack
