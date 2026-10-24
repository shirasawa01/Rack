# Compare the C++ physics core (src/app/CablePhysics.cpp) with the prototype (reference/cable-bench.html).
#   python3 reference/physics_check/check_physics.py
#   python3 reference/physics_check/check_physics.py --settle [stiffness weight friction]   (how the cables come to rest)
# Same jacks and knobs as the prototype's VCV screenshot, cables like its sample patch (short loops, long drapes,
# crossings), then a drag. Both sides print every point; this prints the largest difference per dump.
# Needs g++, node and Playwright's Chromium (present in the cloud container).
import json, math, os, re, subprocess, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
RACK = os.path.normpath(os.path.join(HERE, "..", ".."))
html = open(os.path.join(RACK, "reference", "cable-bench.html"), encoding="utf-8").read()
SHOT = json.loads(re.search(r"const SHOT = (\{.*?\});\n", html).group(1))
LB = 2.6
STD = [15, 30, 45, 60, 90, 120, 150]
def auto(d): return next((l for l in STD if l >= d * 1.1 + 2 * LB + 3), 150)

jacks = SHOT["jacks"]
seed = 3
def rnd():
    global seed
    seed = (seed * 16807) % 2147483647
    return (seed - 1) / 2147483646
used = set()
cables = []
def pick(lo, hi, length=None):
    for _ in range(400):
        a, b = int(rnd() * len(jacks)), int(rnd() * len(jacks))
        if a == b or a in used or b in used: continue
        d = math.dist(jacks[a], jacks[b])
        if lo <= d <= hi:
            used.update((a, b)); cables.append((a, b, length or auto(d))); return
for _ in range(5): pick(1.5, 3.6, 15)
pick(9, 14, 45); pick(12, 19, 60); pick(14, 22, 60); pick(16, 26, 60); pick(5, 9, 30); pick(4, 8, 30); pick(6, 10)

# stiffness, weight, memory, tilt, thickness, desk, desk y, friction (the prototype's defaults)
lines = ["params 0.9 0.5 0.75 12 3.8 1 %r 0.5" % (SHOT["h"] + 1.1)]
lines += ["jack %r %r" % tuple(j) for j in jacks]
lines += ["part %r %r %r %r" % tuple(k) for k in SHOT["knobs"]]
lines += ["cable %d %d %r" % c for c in cables]
lines += ["dump made", "frames 1", "dump f1", "frames 9", "dump f10", "settle", "dump settled",
          "drag 6 12 4.0 -3.0 30", "dump dragged", "settle", "dump released"]
work = tempfile.mkdtemp()
scen = os.path.join(work, "scenario.txt")
open(scen, "w").write("\n".join(lines) + "\n")

if "--settle" in sys.argv:
    # How the cables come to rest with these numbers (C++ only): python3 check_physics.py --settle [stiffness weight friction]
    exe = os.path.join(work, "settle")
    subprocess.run(["g++", "-std=c++11", "-O2", "-w", "-I", os.path.join(RACK, "include"), os.path.join(HERE, "settle.cpp"),
                    os.path.join(RACK, "src", "app", "CablePhysics.cpp"), "-o", exe], check=True)
    extra = sys.argv[sys.argv.index("--settle") + 1:]
    sys.exit(subprocess.run([exe, scen] + extra).returncode)

exe = os.path.join(work, "check")
subprocess.run(["g++", "-std=c++11", "-O2", "-w", "-I", os.path.join(RACK, "include"), os.path.join(HERE, "check.cpp"),
                os.path.join(RACK, "src", "app", "CablePhysics.cpp"), "-o", exe], check=True)
cpp = subprocess.run([exe, scen], capture_output=True, text=True, check=True).stdout
js = subprocess.run(["node", os.path.join(HERE, "bench.js"), scen], capture_output=True, text=True, check=True).stdout

def parse(s):
    out, cur, notes = {}, None, []
    for line in s.splitlines():
        if line.startswith("dump "): cur = line[5:]; out[cur] = []
        elif line.startswith("settled"): notes.append(line)
        elif cur and line.strip(): out[cur].append([float(v) for v in line.split()])
    return out, notes
a, na = parse(cpp); b, nb = parse(js)
print("cables:", len(cables), " frames to settle  C++:", [n.split()[1] for n in na], " prototype:", [n.split()[1] for n in nb])
ok = True
for tag in a:
    worst = 0.0
    for ca, cb in zip(a[tag], b[tag]):
        if len(ca) != len(cb): print(tag, "point count differs"); ok = False; continue
        worst = max([worst] + [abs(p - q) for p, q in zip(ca, cb)])
    print("%-9s largest difference %.2e cm" % (tag, worst))
    if worst > 1e-3: ok = False
print("OK (within 0.001 cm)" if ok and na == nb else "DIFFERENT")
sys.exit(0 if ok and na == nb else 1)
