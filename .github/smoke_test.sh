#!/usr/bin/env bash
# Smoke test for a release build: the engine solves a small axisymmetric coil
# with a linear iron core and the inductance must come out where a reference
# run puts it (2.02 mH). Catches a build whose solver or material handling is
# broken before anything is packaged.
#
#   bash .github/smoke_test.sh <path to femm_headless>
set -euo pipefail
headless="$1"
work="$(mktemp -d)"
model="$work/coil.fem"
script="$work/coil.py"
# Under Git Bash on Windows /tmp/... means nothing to a native program:
# hand it C:/... paths
if command -v cygpath >/dev/null 2>&1; then
    model_py="$(cygpath -m "$model")"
    script_arg="$(cygpath -m "$script")"
else
    model_py="$model"
    script_arg="$script"
fi
cat > "$script" <<EOF
newdocument(0)
mi_probdef(0, 'millimeters', 'axi', 1e-8, 1, 30)
mi_drawrectangle(0, -20, 8, 20)
mi_drawrectangle(10, -15, 20, 15)
mi_addnode(0, -80); mi_addnode(0, 80)
mi_addarc(0, -80, 0, 80, 180, 5)
mi_addsegment(0, -80, 0, -20); mi_addsegment(0, 20, 0, 80)
mi_addmaterial('Air', 1, 1)
mi_addmaterial('Core', 500, 500)
mi_addmaterial('Cu', 1, 1, 0, 0, 58)
mi_addcircprop('coil', 2, 1)
for x, y, m, c, t in ((4, 0, 'Core', '<None>', 0), (15, 0, 'Cu', 'coil', 200), (40, 40, 'Air', '<None>', 0)):
    mi_addblocklabel(x, y); mi_clearselected(); mi_selectlabel(x, y)
    mi_setblockprop(m, 1, 0, c, 0, 0, t); mi_clearselected()
mi_addboundprop('A0', 0, 0, 0, 0, 0, 0, 0, 0, 0)
mi_selectarcsegment(56.6, 56.6); mi_setarcsegmentprop(5, 'A0', 0, 0); mi_clearselected()
mi_saveas("$model_py")
mi_analyze(1)
mi_loadsolution()
i, u, phi = mo_getcircuitproperties('coil')
L = phi / i
if isinstance(L, complex):
    L = L.real
print("SMOKE L =", L)
if not (1.9e-3 < L < 2.15e-3):
    raise SystemExit("smoke test: inductance %g H outside 1.9-2.15 mH" % L)
print("SMOKE OK")
EOF
out="$("$headless" --port 48399 -run "$script_arg" 2>&1)" || true
echo "$out"
echo "$out" | grep -q "SMOKE OK"
