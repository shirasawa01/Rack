// How the cables come to rest (C++ core only): lock, wake again, see how far they still move.
// A good setting locks within ~100 frames and then barely moves when woken (touching one cable must not move far ones).
// Run through check_physics.py --settle [stiffness weight friction].
#include <app/CablePhysics.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace rack::app::cablephysics;

int main(int argc, char** argv) {
	FILE* f = fopen(argv[1], "r");
	World W;
	std::vector<double> jx, jy;
	char cmd[32];
	while (fscanf(f, "%31s", cmd) == 1) {
		std::string c = cmd;
		if (c == "params") {
			int desk;
			fscanf(f, "%lf %lf %lf %lf %lf %d %lf %lf", &W.P.stiff, &W.P.weight, &W.P.mem, &W.P.tilt, &W.P.thickness, &desk, &W.P.deskY, &W.P.friction);
			W.P.desk = desk;
		}
		else if (c == "jack") {
			double x, y;
			fscanf(f, "%lf %lf", &x, &y);
			jx.push_back(x); jy.push_back(y);
		}
		else if (c == "part") {
			Obstacle o;
			fscanf(f, "%lf %lf %lf %lf", &o.x, &o.y, &o.R, &o.H);
			o.owner = -1; o.end = -1;
			W.parts.push_back(o);
		}
		else if (c == "cable") {
			int a, b; double len;
			fscanf(f, "%d %d %lf", &a, &b, &len);
			Cable* cb = new Cable;
			makeCable(*cb, jx[a], jy[a], jx[b], jy[b], len, W.P.mem);
			W.cables.push_back(cb);
		}
		else {
			// The rest of the scenario (dumps, drags) is for check.cpp
			break;
		}
	}
	if (argc >= 5) {
		W.P.stiff = atof(argv[2]);
		W.P.weight = atof(argv[3]);
		W.P.friction = atof(argv[4]);
	}
	printf("stiffness %.2f  weight %.2f  friction %.2f\n", W.P.stiff, W.P.weight, W.P.friction);
	auto snap = [&]() {
		std::vector<double> s;
		for (Cable* c : W.cables)
			for (int i = 0; i < c->n; i++) {
				s.push_back(c->x[i]); s.push_back(c->y[i]); s.push_back(c->z[i]);
			}
		return s;
	};
	W.rebuildObstacles();
	W.wake(100);
	for (int round = 0; round < 4; round++) {
		std::vector<double> a = snap();
		int frames = 0;
		while (W.isAwake() && frames < 5000) {
			W.frame();
			frames++;
		}
		std::vector<double> b = snap();
		double moved = 0, zmax = 0;
		for (size_t k = 0; k < a.size(); k++)
			moved = std::max(moved, std::fabs(a[k] - b[k]));
		for (size_t k = 2; k < b.size(); k += 3)
			zmax = std::max(zmax, b[k]);
		printf("%s: locked after %3d frames, moved up to %6.3f cm, farthest from the panel %.1f cm\n",
			round == 0 ? "plugged in" : "woken again", frames, moved, zmax);
		W.wake(100);
	}
	return 0;
}
