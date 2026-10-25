#pragma once
#include <app/common.hpp>
#include <widget/Widget.hpp>


namespace rack {
namespace app {


struct ModuleWidget;
struct RackWidget;
struct ParamWidget;


/** Panel look (this fork): knobs and jack holes in relief, their shadows, and the panels' surface, for every module.

Nothing here knows a module. It works from the widget types (a direct child Knob of a ModuleWidget, any PortWidget, the panel)
and reads the sizes and colours from their SVGs, so a new module needs no work as long as it uses Rack's widgets.
Light, shadows and the camera are the cables' (CableRig.hpp), so knobs, plugs and cables agree.

In the rack, modules are drawn in passes (drawModules()): every panel, then every shadow, then every module's parts,
then every knob. Shadows and knob tops (in perspective, off their widgets) can then cross from one module onto the next.
Elsewhere (the module browser) a module draws everything itself, straight on.
*/
namespace panellook {


/** Every drawing number is here. */
struct Look {
	// ---- panel surface (scaled by settings::panelTexture) ----
	/** Grain: texel size (mm) and opacity. Faded out when a texel shrinks below about a screen pixel. */
	float grainTexel = 0.12f, grainAlpha = 0.16f;
	/** How much of the grain is fine speckle, vertical brushing, and its streak length (texels) */
	float speckle = 0.6f, brush = 0.5f;
	int brushLength = 14;
	/** Broad sheen across the case, light toward the upper left: opacity of the lighter and of the darker end */
	float sheenLight = 0.07f, sheenDark = 0.1f;
	/** Bevelled panel edges: width (mm), light on the top and left, dark on the bottom and right */
	float bevel = 0.35f, bevelLight = 0.28f, bevelDark = 0.4f;

	// ---- knobs ----
	/** The face's height as a fraction of the knob's height (cablephysics::partSize, which also counts the room a cable needs above it) */
	float faceHeight = 0.5f;
	/** Fixed light on the face (it does not turn with the knob): opacity toward white on the lit side, toward black on the other */
	float faceLight = 0.1f, faceDark = 0.24f;
	/** Rim catching the light on the face's edge: width (of R), opacity, half the arc (radians) */
	float rimWidth = 0.07f, rimAlpha = 0.35f, rimArc = 1.1f;
	/** Side of the knob, seen in perspective: its colour toward white on the lit side, toward black on the other */
	float sideLight = 0.2f, sideDark = 0.6f;
	/** Dark ring on the panel around a knob's base: out to this many R */
	float contactReach = 1.3f, contactAlpha = 0.35f;

	// ---- jacks ----
	/** Height of the nut and bushing above the panel, cm, for its shadow */
	float jackHeight = 0.3f;
	/** Fixed light on the nut */
	float nutLight = 0.12f, nutDark = 0.18f;
	/** The hole: depth (cm) for the perspective, the floor's radius (of the hole's) where the wall ends */
	float holeDepth = 0.6f, holeFloor = 0.72f;
	/** The hole's wall: grey on the lit side (lower right) and the shaded side */
	float wallLit = 0.3f, wallShade = 0.02f;
	/** The hole's lip catching the light on the lower right: width (mm) and opacity */
	float lipWidth = 0.2f, lipAlpha = 0.4f;
	/** Hole radius (mm) when the SVG shows none */
	float holeRadius = 1.8f;

	// ---- shadows (with the cables' light3, shadowFade, shadowSpread and settings::cableShadow) ----
	float shadowAlpha = 0.26f;
	/** A second, wider and fainter shadow for a soft edge: grown by this many R, opacity */
	float shadowBlur = 0.35f, shadowBlurAlpha = 0.4f;
};

extern Look look;


/** settings::panelLook */
bool enabled();
/** Knob tops through the cables' camera */
bool perspective();

/** ModuleContainer::draw(): draws its ModuleWidget children in the passes above. */
void drawModules(widget::Widget* container, const widget::Widget::DrawArgs& args);
/** ModuleWidget::draw(): this module's share of the current pass, or all of it outside the rack. */
void drawModule(ModuleWidget* mw, const widget::Widget::DrawArgs& args);
/** ModuleWidget::draw() puts the meter and the selection only on top of the parts, not on the panel pass. */
bool panelPass();
/** With the panel look off: gives the SVG knobs and jacks back their own shadows. */
void restoreShadows(ModuleWidget* mw);

/** The knob whose top (drawn in perspective) is under `pos` (rack coordinates), or NULL. Presses there belong to the knob. */
ParamWidget* knobAt(RackWidget* rack, math::Vec pos);


} // namespace panellook
} // namespace app
} // namespace rack
