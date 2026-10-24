#pragma once
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cstddef>


namespace rack {
namespace app {
/** Patch cables as 3D elastic rods (position-based dynamics).
Ported from the browser prototype reference/cable-bench.html; the numbers follow reference/CABLE_PHYSICS_SPEC.md.
This part knows nothing about Rack (no widgets, no NanoVG) so it can be checked against the prototype on its own
(reference/physics_check/). Units are cm: x right, y down, z out of the panel toward the viewer (panel face z = 0).
*/
namespace cablephysics {


/** 1 HP = 15 px = 5.08 mm */
static const double PX_PER_CM = 15.0 / 0.508;
/** Plug + boot length sticking out of the panel */
static const double LB = 2.6;
/** Spacing of the physics points (target) */
static const double SEG = 1.0;
static const int SUB = 2;
static const int ITER = 16;
/** Bending is checked over neighbours (1) and wider spans, so the whole cable bends as one and settles quickly */
static const int BEND_SPANS[3] = {1, 2, 4};
static const double BEND_WEIGHT[3] = {1.0, 0.25, 0.0625};


struct Params {
	/** 0..1 */
	double stiff = 0.9;
	/** 0..1 */
	double weight = 0.5;
	/** Shape memory, 0..1 */
	double mem = 0.75;
	/** Case tilt in degrees. Tilting pushes the cables toward the panel. */
	double tilt = 12.0;
	/** Cable diameter in mm */
	double thickness = 3.8;
	/** Friction between cables, 0..1. 0.5 is the same as on the panel. */
	double friction = 0.5;
	/** Horizontal desk surface at deskY (the prototype's desk in front of the case). */
	bool desk = false;
	double deskY = 0.0;

	double rc() const {return thickness / 20.0;}
	double rb() const {return 0.13 + rc() * 1.25;}
};


struct Cable {
	/** Total length (cm), one of the standard lengths */
	double len = 0.0;
	int n = 0;
	double seg = 0.0;
	std::vector<double> x, y, z, px, py, pz, w;
	/** Positions at the start of the frame, for telling when the cable has come to rest */
	std::vector<double> fx, fy, fz;
	std::vector<double> rest1;
	/** Bending: rest offset of point i from the middle of points i - s and i + s (0 = straight), for each span s in BEND_SPANS.
	Shape memory moves it. */
	std::vector<double> restH[3];
	/** Hand-bending leaves a set that fades out after release */
	double plast = 0.0;
	/** Bumped whenever the points change, for caches that read them */
	uint64_t version = 0;

	double ax() const {return x[0];}
	double ay() const {return y[0];}
	double bx() const {return x[n - 1];}
	double by() const {return y[n - 1];}
	double span(int i, int j) const;
	/** How far point i sits off the middle of points i - s and i + s: (s * link)^2 * curvature / 2 for gentle bends */
	double offset(int i, int s) const;
	void plastic(double rate);
	/** Moves the fixed points (jack bases and boot tips) to new jack positions. */
	void moveEnds(double ax, double ay, double bx, double by);
};


/** Standard lengths, cm */
extern const double STD_LEN[7];
/** Shortest standard length that reaches comfortably (dist*1.1 + 2*LB + 3), else the longest */
double chooseLength(double dist);
/** Cable cannot reach between the jacks with this length */
bool tooShort(double len, double dist);
/** The arc a person leaves when plugging in (spec 3.2), with the plugging-in memory (3.3). */
void makeCable(Cable& c, double ax, double ay, double bx, double by, double len, double mem);


/** Upright cylinder: knob, switch, jack, or a plug boot (owner >= 0). */
struct Obstacle {
	double x, y, R, H;
	/** Index of the cable that owns this boot, or -1 */
	int owner;
	/** 0 = end A, 1 = end B */
	int end;
};


struct World {
	Params P;
	std::vector<Cable*> cables;
	/** Panel parts. rebuildObstacles() adds the plug boots of `cables`. */
	std::vector<Obstacle> parts;

	struct Drag {
		Cable* cable = nullptr;
		int i = 0;
		double tx = 0, ty = 0, tz = 0;
	};
	bool dragging = false;
	Drag drag;

	int awake = 0;
	int still = 0;

	/** Call after changing `parts`, `cables` or the thickness. */
	void rebuildObstacles();
	/** Something touched the cables: run until they settle. Velocities start at zero. */
	void wake(int frames = 100);
	void step(double dt);
	/** One screen frame as in the prototype. Returns true if the cables moved. */
	bool frame();
	bool isAwake() const {return awake > 0 || dragging;}

	void startDrag(Cable* c, int i, double tx, double ty, double tz);
	void moveDrag(double tx, double ty, double tz);
	void endDrag();

private:
	std::vector<Obstacle> obstacles;
	std::unordered_map<int64_t, std::vector<int>> obGrid;
	// Cable vs cable hash, rebuilt every few iterations. Cells are kept in insertion order like the prototype's Map.
	std::unordered_map<int64_t, int> shIndex;
	std::vector<std::vector<int64_t>> shCells;
	size_t shUsed = 0;
	std::unordered_set<int64_t> shDone;
	/** Cable friction runs in the last collision pass of a substep only: in every pass it keeps the contacts trembling */
	bool frictionPass = false;

	void applyDrag();
	void collideStatic();
	void collideCables();
	void segPush(Cable& A, int i, Cable& B, int j);
};


} // namespace cablephysics
} // namespace app
} // namespace rack
