// The C++ physics core run on a scenario file written by check_physics.py; prints the points per recorded frame.
#include <app/CablePhysics.hpp>
#include <cstdio>
#include <string>
#include <vector>

using namespace rack::app::cablephysics;

int main(int argc, char** argv) {
	FILE* f = fopen(argv[1], "r");
	World W;
	std::vector<double> jx, jy;
	std::vector<Cable*> cables;
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
			W.rebuildObstacles();
			W.wake();
		}
		else if (c == "frames") {
			int k; fscanf(f, "%d", &k);
			for (int i = 0; i < k; i++) W.frame();
		}
		else if (c == "settle") {
			int k = 0;
			while (W.isAwake() && k < 1000) { W.frame(); k++; }
			printf("settled %d\n", k);
		}
		else if (c == "drag") {
			int ci, i; double dx, dy; int frames;
			fscanf(f, "%d %d %lf %lf %d", &ci, &i, &dx, &dy, &frames);
			Cable* cb = W.cables[ci];
			double x0 = cb->x[i], y0 = cb->y[i], z0 = cb->z[i];
			W.startDrag(cb, i, x0, y0, z0);
			for (int k = 1; k <= frames; k++) {
				W.moveDrag(x0 + dx * k / frames, y0 + dy * k / frames, z0);
				W.frame();
			}
			W.endDrag();
		}
		else if (c == "dump") {
			char tag[64]; fscanf(f, "%63s", tag);
			printf("dump %s\n", tag);
			for (Cable* cb : W.cables) {
				for (int i = 0; i < cb->n; i++) printf("%.9f %.9f %.9f ", cb->x[i], cb->y[i], cb->z[i]);
				printf("\n");
			}
		}
	}
	return 0;
}
