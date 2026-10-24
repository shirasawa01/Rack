#pragma once
#include <jansson.h>
#include <app/common.hpp>
#include <widget/Widget.hpp>


namespace rack {
namespace app {


struct RackWidget;
struct CableWidget;


namespace cablephysics {


/** How the cables look (spec chapter 4). Every drawing number is here, so the look can be swapped out
without touching the physics. LB, RB and RC are not here: they also change how the cables hang (CablePhysics.hpp, settings::cableThickness).
*/
struct Look {
	/** Catmull-Rom subdivisions per physics link */
	int subdivisions = 5;
	/** Spline points per depth-sorted piece (flat) and per shadow piece */
	int chunk = 10;
	/** Flat pieces meet square across the tube; the body reaches this far (px) into the next piece to hide the antialiasing hairline.
	Longer overlaps paint over other cables lying between the two pieces in depth (cables seemed to sink into each other). */
	float seam = 1.f;
	float minWidth = 1.4f;
	/** Light on the screen (upper left) for the round style's highlights */
	float lightX = -0.45f, lightY = -0.89f;
	/** Light in 3D for the shadows, normalized in shadowPoint() */
	float light3[3] = {-0.35f, -0.8f, 1.f};
	float shadowAlpha = 0.38f;
	/** Shadows fade with height: exp(-z / shadowFade) */
	float shadowFade = 7.f;
	/** Shadows widen with height: * (1 + z * shadowSpread) */
	float shadowSpread = 0.12f;
	/** Round style: a second, wider and fainter shadow stroke */
	float shadowBlurWidth = 2.2f, shadowBlurAlpha = 0.35f;
	/** Mean channel (0..255) below which a color counts as dark */
	float darkBelow = 70.f;
	/** Flat edge: toward black (light colors) or white (dark colors) */
	float edgeToBlack = 0.32f, edgeToWhiteDark = 0.16f;
	/** Flat edge width beyond the body, px */
	float edgeExtra = 1.f;

	/** Round style: nested bands, darkest and widest first. */
	struct Band {
		/** Width relative to the cable */
		float width;
		/** < 0 toward black, > 0 toward white, for light and for dark cable colors */
		float mix, mixDark;
		float alpha;
		/** Shift toward the light, relative to the width */
		float shift;
	};
	Band bands[5] = {
		{1.0f, -0.5f, -0.35f, 1.f, 0.f},
		{0.84f, -0.26f, -0.1f, 1.f, 0.07f},
		{0.64f, 0.f, 0.f, 1.f, 0.13f},
		{0.4f, 0.2f, 0.12f, 1.f, 0.19f},
		{0.17f, 0.7f, 0.4f, 0.9f, 0.25f},
	};
	float minBandWidth = 0.6f;
};

extern Look look;


/** The Rack side of cable physics: a physics cable for every CableWidget, the panel parts as obstacles (spec 5.1),
drawing (chapter 4), and grabbing a cable in the middle (5.2). One per RackWidget.
*/
struct Rig {
	struct Internal;
	Internal* internal;

	Rig();
	~Rig();
	/** Once per frame, after the CableWidgets have stepped. */
	void step(RackWidget* rack);
	/** Shadows, cables and plugs, in place of the original cable and plug layers. */
	void draw(const widget::Widget::DrawArgs& args);

	/** Starts shaping the cable under `pos` (rack coordinates). False if there is none. */
	bool grab(math::Vec pos);
	void dragTo(math::Vec pos);
	void release();

	/** The cable's points and memory for the patch, or NULL. */
	json_t* toJson(CableWidget* cw);
	/** Points and memory from the patch, used when the cable is next laid out. */
	void setJson(CableWidget* cw, json_t* physicsJ);
	/** The CableWidget is going away. */
	void forget(CableWidget* cw);
};


/** settings::cablePhysics */
bool enabled();
/** The rack's Rig, or NULL */
Rig* getRig();


} // namespace cablephysics
} // namespace app
} // namespace rack
