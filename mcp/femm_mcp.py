# /// script
# requires-python = ">=3.10"
# dependencies = ["mcp>=1.2"]
# ///
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 David Meeker and contributors
"""
femm_mcp.py -- Model Context Protocol server for FEMM (femm-qt).

Lets an AI assistant (Claude Code, Claude Desktop, any MCP client) build,
solve and evaluate FEMM models: draw geometry, assign materials and
circuits, mesh and analyze, read field values, forces and inductances, and
look at the result as a picture.

It drives FEMM over the same localhost TCP interface the language bindings
use (port 48015 by default):

  * If a FEMM window is already open, the assistant works IN that window --
    you watch the model being drawn and can take over at any time. The
    connection is opened per tool call, so your own scripts can share it.
  * Otherwise the server starts FEMM itself (the GUI, or femm_headless with
    FEMM_MCP_HEADLESS=1) and keeps it for the session.

Run:   uv run mcp/femm_mcp.py        (uv installs the 'mcp' package)
Add to Claude Code:
       claude mcp add femm -- uv run /path/to/femm-qt/mcp/femm_mcp.py

Environment:
  FEMM_PORT          TCP port (default 48015)
  FEMM_DIR           folder containing femm_gui / femm_headless
  FEMM_MCP_HEADLESS  1 = start femm_headless instead of the GUI when needed
"""

from __future__ import annotations

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from typing import Any

try:                                    # mcp >= 2
    from mcp.server.mcpserver import MCPServer as FastMCP, Image
    from mcp.server.mcpserver.exceptions import ToolError
except ImportError:                     # mcp 1.x
    from mcp.server.fastmcp import FastMCP, Image
    from mcp.server.fastmcp.exceptions import ToolError

PORT = int(os.environ.get("FEMM_PORT", "48015"))
PREFIX = {"magnetics": "mi", "electrostatics": "ei", "heat": "hi", "current": "ci"}
POST = {"mi": "mo", "ei": "eo", "hi": "ho", "ci": "co"}
PHYSICS_INDEX = {"magnetics": 0, "electrostatics": 1, "heat": 2, "current": 3}
LIBS = {"magnetics": "matlib.dat", "electrostatics": "statlib.dat",
        "heat": "heatlib.dat", "current": "condlib.dat"}
POINT_FIELDS = {
    "mo": ["A", "B1", "B2", "Sig", "E", "H1", "H2", "Je", "Js", "Mu1", "Mu2", "Pe", "Ph", "ff"],
    "eo": ["V", "Dx", "Dy", "Ex", "Ey", "ex", "ey", "nrg"],
    "ho": ["T", "Fx", "Fy", "Gx", "Gy", "kx", "ky"],
    "co": ["V", "Jx", "Jy", "Kx", "Ky", "Ex", "Ey", "ex", "ey", "Jdx", "Jdy", "ox", "oy", "Jcx", "Jcy"],
}


# ---------------------------------------------------------------------------
# Locating and talking to FEMM
# ---------------------------------------------------------------------------

def _candidate_dirs() -> list[str]:
    here = os.path.dirname(os.path.abspath(__file__))
    dirs = []
    if os.environ.get("FEMM_DIR"):
        dirs.append(os.environ["FEMM_DIR"])
    dirs += [
        "/Applications/femm_gui.app/Contents/MacOS",
        os.path.join(here, "..", "build", "bin", "femm_gui.app", "Contents", "MacOS"),
        os.path.join(here, "..", "build", "bin"),
        r"C:\femm-qt\bin",
    ]
    for p in os.environ.get("PATH", "").split(os.pathsep):
        dirs.append(p)
    return dirs


def _find_exe(name: str) -> str | None:
    for d in _candidate_dirs():
        for n in (name, name + ".exe"):
            p = os.path.join(d, n)
            if os.path.isfile(p) and os.access(p, os.X_OK):
                return os.path.abspath(p)
    return None


def _resource_dirs() -> list[str]:
    out = []
    for d in _candidate_dirs():
        out += [d, os.path.join(d, "..", "Resources"), os.path.join(d, "..", "share", "femm")]
    return out


def _find_resource(name: str) -> str | None:
    for d in _resource_dirs():
        p = os.path.join(d, name)
        if os.path.isfile(p) or os.path.isdir(p):
            return os.path.abspath(p)
    return None


class FemmError(ToolError):
    """Reported to the assistant with its message (a plain exception would
    reach it only as 'error executing tool')."""


class Femm:
    """One FEMM instance. Attaches to a running one, or starts its own."""

    def __init__(self) -> None:
        self.proc: subprocess.Popen | None = None
        self.sock: socket.socket | None = None   # persistent only for our own instance
        self.buf = b""

    def _connect(self, timeout: float = 2.0) -> socket.socket:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=timeout)
        s.settimeout(None)
        return s

    def _ensure(self) -> socket.socket:
        if self.sock:
            return self.sock
        try:
            # Someone else's FEMM (usually the user's window): connect per call
            return self._connect()
        except OSError:
            pass
        headless = os.environ.get("FEMM_MCP_HEADLESS") == "1"
        exe = _find_exe("femm_headless" if headless else "femm_gui")
        if not exe:
            raise FemmError("FEMM is not running and femm_gui/femm_headless was not found. "
                            "Open FEMM, or set FEMM_DIR to the folder containing it.")
        # With --port FEMM ties its life to this connection, which we keep.
        self.proc = subprocess.Popen([exe, "--port", str(PORT)],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                self.sock = self._connect()
                return self.sock
            except OSError:
                time.sleep(0.3)
        raise FemmError("Started FEMM but could not connect to port %d." % PORT)

    def call(self, command: str) -> str:
        """Send one line of FEMM script, return the raw reply line."""
        if "\n" in command:
            raise FemmError("one command per call (use run_python for blocks of code)")
        # FEMM serves one client at a time and notices a closed connection only
        # when it next polls, so right after our previous per-call connection
        # it may still turn us away. Wait for it briefly.
        for attempt in range(100):
            try:
                return self._call_once(command)
            except FemmError as e:
                if "already connected" not in str(e) or attempt == 99:
                    raise
                time.sleep(0.03)
        raise FemmError("unreachable")

    def _call_once(self, command: str) -> str:
        s = self._ensure()
        own = s is self.sock
        try:
            s.sendall((command + "\n").encode("utf-8"))
            buf = self.buf if own else b""
            while b"\n" not in buf:
                chunk = s.recv(65536)
                if not chunk:
                    if own:
                        self.sock = None
                    raise FemmError("connection to FEMM lost")
                buf += chunk
            line, rest = buf.split(b"\n", 1)
            if own:
                self.buf = rest
        finally:
            if not own:
                s.close()
        reply = line.decode("utf-8", "replace")
        if reply.startswith("error:"):
            raise FemmError(reply[6:].strip())
        return reply

    def values(self, command: str) -> list[Any]:
        """Call and parse a tab-separated numeric reply."""
        reply = self.call(command)
        out: list[Any] = []
        for p in reply.split("\t"):
            p = p.strip()
            if not p:
                continue
            try:
                out.append(float(p))
                continue
            except ValueError:
                pass
            try:
                z = complex(p.replace("I", "j").replace("i", "j").replace(" ", ""))
                out.append(z.real if z.imag == 0 else {"re": z.real, "im": z.imag})
            except ValueError:
                out.append(p)
        return out


femm = Femm()
mcp = FastMCP("femm", instructions=(
    "FEMM: 2D planar and axisymmetric finite element simulation (magnetics, electrostatics, "
    "heat flow, current flow). Typical flow: new_problem -> draw_* -> add_library_material / "
    "add_circuit -> assign_region for every closed region -> open_boundary (or set_boundary) -> "
    "analyze -> point_values / circuit_properties / block_integral -> screenshot. "
    "Coordinates are in the problem's length units; in axisymmetric problems x is r >= 0 and y is z. "
    "Every closed region needs exactly one region assignment. Use get_model to inspect the model "
    "and femm_command / run_python for anything the other tools do not cover "
    "(read femm://docs/scripting for the command reference)."))


def q(s: str) -> str:
    """A Python string literal for FEMM's script parser."""
    return json.dumps(s)


def _model() -> dict:
    for p in ("mi", "ei", "hi", "ci"):
        try:
            return json.loads(femm.call(f"{p}_getgeometry()"))
        except FemmError:
            continue
        except json.JSONDecodeError:
            continue
    raise FemmError("no preprocessor document is open -- use new_problem or open_document first")


def _prefix() -> str:
    return PREFIX[_model()["physics"]]


# ---------------------------------------------------------------------------
# Problem / documents
# ---------------------------------------------------------------------------

@mcp.tool()
def new_problem(physics: str = "magnetics", units: str = "millimeters", type: str = "planar",
                depth: float = 1.0, frequency: float = 0.0, precision: float = 1e-8,
                min_angle: float = 30.0) -> str:
    """Create a new problem.

    physics: magnetics | electrostatics | heat | current
    units: inches | millimeters | centimeters | meters | mils | micrometers
    type: planar | axisymmetric
    depth: model depth into the page (planar), in the length units
    frequency: Hz; magnetics 0 = static/DC, current flow is always AC
    """
    if physics not in PHYSICS_INDEX:
        raise FemmError("physics must be one of " + ", ".join(PHYSICS_INDEX))
    if type not in ("planar", "axisymmetric", "axi"):
        raise FemmError("type must be planar or axisymmetric")
    ptype = "planar" if type == "planar" else "axi"
    femm.call(f"newdocument({PHYSICS_INDEX[physics]})")
    p = PREFIX[physics]
    if p == "mi":
        femm.call(f"mi_probdef({frequency}, {q(units)}, {q(ptype)}, {precision}, {depth}, {min_angle})")
    elif p == "ci":
        femm.call(f"ci_probdef({q(units)}, {q(ptype)}, {frequency or 60}, {precision}, {depth}, {min_angle})")
    else:
        femm.call(f"{p}_probdef({q(units)}, {q(ptype)}, {precision}, {depth}, {min_angle})")
    return f"New {type} {physics} problem in {units}."


@mcp.tool()
def open_document(path: str) -> str:
    """Open a FEMM model (.fem .fee .feh .fec) or solution file."""
    femm.call(f"opendocument({q(os.path.abspath(os.path.expanduser(path)))})")
    return "Opened " + path


@mcp.tool()
def save_document(path: str = "") -> str:
    """Save the model. Without a path it is saved where it was loaded from
    (a new model must be given a path the first time)."""
    m = _model()
    p = PREFIX[m["physics"]]
    target = os.path.abspath(os.path.expanduser(path)) if path else m["file"]
    if not target:
        raise FemmError("the model has never been saved -- give a path ending in ."
                        + {"mi": "fem", "ei": "fee", "hi": "feh", "ci": "fec"}[p])
    femm.call(f"{p}_saveas({q(target)})")
    return "Saved " + target


@mcp.tool()
def get_model() -> dict:
    """The whole preprocessor model: problem settings, nodes, segments, arcs (by node index),
    region labels with material/circuit/turns, and the names of materials, boundary
    conditions, circuits and point properties."""
    return _model()


# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------

@mcp.tool()
def draw_polyline(points: list[list[float]], closed: bool = False) -> str:
    """Draw connected straight segments through [[x, y], ...]. closed=True joins
    the last point back to the first. Crossings with existing geometry are split
    automatically."""
    p = _prefix()
    pts = [tuple(map(float, pt)) for pt in points]
    if len(pts) < 2:
        raise FemmError("need at least two points")
    for x, y in pts:
        femm.call(f"{p}_addnode({x}, {y})")
    pairs = list(zip(pts, pts[1:])) + ([(pts[-1], pts[0])] if closed else [])
    for (x0, y0), (x1, y1) in pairs:
        femm.call(f"{p}_addsegment({x0}, {y0}, {x1}, {y1})")
    return f"Drew {len(pairs)} segment(s)."


@mcp.tool()
def draw_rectangle(x0: float, y0: float, x1: float, y1: float) -> str:
    """Draw a closed rectangle with opposite corners (x0, y0) and (x1, y1)."""
    return draw_polyline([[x0, y0], [x1, y0], [x1, y1], [x0, y1]], closed=True)


@mcp.tool()
def draw_circle(cx: float, cy: float, r: float, max_segment_deg: float = 5.0) -> str:
    """Draw a full circle (two 180-degree arcs) around (cx, cy) with radius r.
    max_segment_deg controls how finely the arc is meshed."""
    p = _prefix()
    femm.call(f"{p}_addnode({cx + r}, {cy})")
    femm.call(f"{p}_addnode({cx - r}, {cy})")
    femm.call(f"{p}_addarc({cx + r}, {cy}, {cx - r}, {cy}, 180, {max_segment_deg})")
    femm.call(f"{p}_addarc({cx - r}, {cy}, {cx + r}, {cy}, 180, {max_segment_deg})")
    return "Drew circle."


@mcp.tool()
def draw_arc(x0: float, y0: float, x1: float, y1: float, angle_deg: float,
             max_segment_deg: float = 5.0) -> str:
    """Draw an arc from (x0, y0) to (x1, y1) turning counter-clockwise through angle_deg (0..180]."""
    p = _prefix()
    femm.call(f"{p}_addnode({x0}, {y0})")
    femm.call(f"{p}_addnode({x1}, {y1})")
    femm.call(f"{p}_addarc({x0}, {y0}, {x1}, {y1}, {angle_deg}, {max_segment_deg})")
    return "Drew arc."


@mcp.tool()
def delete_all_geometry() -> str:
    """Delete every node, segment, arc and region label (materials etc. are kept)."""
    p = _prefix()
    m = _model()
    if m["nodes"] or m["labels"]:
        xs = [n[0] for n in m["nodes"]] + [l["x"] for l in m["labels"]]
        ys = [n[1] for n in m["nodes"]] + [l["y"] for l in m["labels"]]
        pad = 1 + max(max(xs) - min(xs), max(ys) - min(ys))
        femm.call(f"{p}_selectrectangle({min(xs) - pad}, {min(ys) - pad}, {max(xs) + pad}, {max(ys) + pad}, 4)")
        femm.call(f"{p}_deleteselected()")
    return "Geometry deleted."


# ---------------------------------------------------------------------------
# Materials, regions, circuits, boundaries
# ---------------------------------------------------------------------------

def _library(physics: str) -> list[dict]:
    path = _find_resource(LIBS[physics])
    if not path:
        return []
    out, folders = [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r'\s*<(\w+)>\s*=?\s*"?([^"]*)"?', line)
            if not m:
                continue
            key, val = m.group(1).lower(), m.group(2).strip()
            if key == "beginfolder":
                folders.append("")
            elif key == "endfolder" and folders:
                folders.pop()
            elif key == "foldername" and folders:
                folders[-1] = val
            elif key == "blockname":
                out.append({"name": val, "folder": " / ".join(x for x in folders if x)})
    return out


@mcp.tool()
def search_materials(query: str = "", physics: str = "") -> list[dict]:
    """Search the material library (all words must match the name or folder).
    physics defaults to the open model's. E.g. 'N42', 'copper', 'M-19', 'steel 1010'."""
    ph = physics or _model()["physics"]
    words = query.lower().split()
    hits = [m for m in _library(ph)
            if all(w in (m["name"] + " " + m["folder"]).lower() for w in words)]
    return hits[:60]


@mcp.tool()
def add_library_material(name: str) -> str:
    """Copy a material from the library into the model (exact name, see search_materials)."""
    p = _prefix()
    if name in _model()["materials"]:
        return f"'{name}' is already in the model."
    femm.call(f"{p}_getmaterial({q(name)})")
    if name not in _model()["materials"]:
        raise FemmError(f"'{name}' is not in the library")
    return f"Added '{name}'."


@mcp.tool()
def assign_region(x: float, y: float, material: str, circuit: str = "", turns: int = 1,
                  magnetization_deg: float = 0.0, mesh_size: float = 0.0, group: int = 0) -> str:
    """Fill the closed region containing (x, y) with a material by placing a region
    (block) label there. The material must be in the model (add_library_material).
    Magnetics only: circuit + turns make it a coil (negative turns reverse the
    current), magnetization_deg is the direction of a permanent magnet.
    mesh_size 0 = automatic."""
    m = _model()
    p = PREFIX[m["physics"]]
    if material not in m["materials"] and material != "<No Mesh>":
        if any(lm["name"] == material for lm in _library(m["physics"])):
            femm.call(f"{p}_getmaterial({q(material)})")
        else:
            words = material.lower().split()
            close = [lm["name"] for lm in _library(m["physics"])
                     if any(w in lm["name"].lower() for w in words)][:8]
            raise FemmError(f"material '{material}' is neither in the model nor in the library"
                            + (f"; similar: {close}" if close else "")
                            + ". Use search_materials, or femm_command with "
                            + f"{p}_addmaterial(...) to define it.")
    if circuit and circuit not in m["circuits"]:
        raise FemmError(f"circuit '{circuit}' does not exist -- add_circuit first")
    femm.call(f"{p}_addblocklabel({x}, {y})")
    femm.call(f"{p}_clearselected()")
    femm.call(f"{p}_selectlabel({x}, {y})")
    auto = 0 if mesh_size > 0 else 1
    if p == "mi":
        femm.call(f"mi_setblockprop({q(material)}, {auto}, {mesh_size}, {q(circuit or '<None>')}, "
                  f"{magnetization_deg}, {group}, {turns})")
    else:
        femm.call(f"{p}_setblockprop({q(material)}, {auto}, {mesh_size}, {group})")
    femm.call(f"{p}_clearselected()")
    return f"Region at ({x}, {y}) is {material}" + (f", circuit {circuit} x{turns}" if circuit else "") + "."


@mcp.tool()
def add_circuit(name: str, current: float, series: bool = True) -> str:
    """Magnetics: define a circuit carrying `current` amps. series=True: every turn
    carries the current (coils); False: regions in parallel share it."""
    femm.call(f"mi_addcircprop({q(name)}, {current}, {1 if series else 0})")
    return f"Circuit {name}: {current} A ({'series' if series else 'parallel'})."


@mcp.tool()
def add_boundary(name: str, kind: str = "A=0", value: float = 0.0) -> str:
    """Magnetics: define a boundary condition.
    kind: 'A=0' (flux cannot cross; fixed vector potential), 'periodic', 'antiperiodic'.
    For other kinds or physics use femm_command with {pfx}_addboundprop (see the docs)."""
    fmt = {"A=0": 0, "periodic": 4, "antiperiodic": 5}
    if kind not in fmt:
        raise FemmError("kind must be one of " + ", ".join(fmt))
    femm.call(f"mi_addboundprop({q(name)}, {value}, 0, 0, 0, 0, 0, 0, 0, {fmt[kind]})")
    return f"Boundary {name} ({kind})."


@mcp.tool()
def set_boundary(x: float, y: float, boundary: str) -> str:
    """Apply a boundary condition to the segment or arc nearest (x, y)."""
    p = _prefix()
    femm.call(f"{p}_clearselected()")
    femm.call(f"{p}_selectsegment({x}, {y})")
    try:
        femm.call(f"{p}_setsegmentprop({q(boundary)}, 0, 1, 0, 0)")
    finally:
        femm.call(f"{p}_clearselected()")
    return f"Boundary {boundary} set near ({x}, {y})."


@mcp.tool()
def open_boundary(layers: int = 7) -> str:
    """Surround the model with an open (infinite space) boundary: concentric shells
    that make the field behave as if it extended to infinity. Typical for a coil or
    magnet in free space. Fill the new outer region(s) with air afterwards."""
    p = _prefix()
    femm.call(f"{p}_makeABC({layers})")
    return "Open boundary created; remember to assign air to the regions between the model and it."


# ---------------------------------------------------------------------------
# Solve and evaluate
# ---------------------------------------------------------------------------

@mcp.tool()
def analyze(save_as: str = "") -> str:
    """Mesh and solve, then load the solution. A model that was never saved needs
    save_as (a file path); FEMM writes the solution next to it."""
    m = _model()
    p = PREFIX[m["physics"]]
    if save_as:
        femm.call(f"{p}_saveas({q(os.path.abspath(os.path.expanduser(save_as)))})")
    elif not m["file"]:
        tmp = os.path.join(tempfile.gettempdir(), "femm_mcp_model." +
                           {"mi": "fem", "ei": "fee", "hi": "feh", "ci": "fec"}[p])
        femm.call(f"{p}_saveas({q(tmp)})")
    femm.call(f"{p}_analyze(1)")
    femm.call(f"{p}_loadsolution()")
    return "Solved."


@mcp.tool()
def point_values(x: float, y: float) -> dict:
    """Field values at (x, y) of the loaded solution (magnetics: A, B1/B2 [T], H1/H2 [A/m],
    Je/Js [A/m^2], mu, ...; electrostatics: V, D, E; heat: T, F, G; current: V, J, E).
    AC quantities come as {re, im}."""
    p = _prefix()
    po = POST[p]
    vals = femm.values(f"{po}_getpointvalues({x}, {y})")
    if not vals:
        raise FemmError(f"({x}, {y}) is outside the mesh")
    return dict(zip(POINT_FIELDS[po], vals))


@mcp.tool()
def circuit_properties(name: str) -> dict:
    """Magnetics: current [A], voltage drop [V], flux linkage [Wb] of a circuit, plus
    inductance L = flux/current [H] and resistance R = V/I [Ohm]."""
    i, v, phi = (femm.values(f"mo_getcircuitproperties({q(name)})") + [0, 0, 0])[:3]

    def c(z):
        return complex(z["re"], z["im"]) if isinstance(z, dict) else complex(z)
    out = {"current": i, "voltage": v, "flux_linkage": phi}
    if abs(c(i)) > 0:
        out["inductance"] = (c(phi) / c(i)).real
        out["resistance"] = (c(v) / c(i)).real
    return out


BLOCK_TYPES = ("magnetics block integral types: 0 A.J, 1 A, 2 field energy, 3 hysteresis/lamination "
               "losses, 4 resistive losses, 5 area, 6 total losses, 7 total current, 8/9 integral of "
               "Bx/By, 10 volume, 11/12 steady-state Lorentz force x/y, 15 Lorentz torque, 17 coenergy, "
               "18/19 weighted stress tensor force x/y, 22 weighted stress tensor torque, 24 R^2")


@mcp.tool(description="Integrate over the regions containing the given [[x, y], ...] points. "
                      "Forces and torques on a part: select all its regions and use type 18/19 "
                      "(force) or 22 (torque). " + BLOCK_TYPES)
def block_integral(points: list[list[float]], integral_type: int) -> Any:
    p = POST[_prefix()]
    femm.call(f"{p}_clearblock()")
    for x, y in points:
        femm.call(f"{p}_selectblock({x}, {y})")
    try:
        r = femm.values(f"{p}_blockintegral({integral_type})")
    finally:
        femm.call(f"{p}_clearblock()")
    return r[0] if len(r) == 1 else r


@mcp.tool()
def line_integral(points: list[list[float]], integral_type: int) -> Any:
    """Integrate along the contour through [[x, y], ...]. Magnetics types: 0 B.n (flux),
    1 H.t, 2 contour length/area, 3 stress tensor force, 4 stress tensor torque, 5 (B.n)^2."""
    p = POST[_prefix()]
    femm.call(f"{p}_clearcontour()")
    for x, y in points:
        femm.call(f"{p}_addcontour({x}, {y})")
    try:
        r = femm.values(f"{p}_lineintegral({integral_type})")
    finally:
        femm.call(f"{p}_clearcontour()")
    return r


@mcp.tool()
def screenshot(view: str = "results") -> Image:
    """A picture of the current view, to check what has been drawn or solved.
    view: 'model' (preprocessor drawing) or 'results' (solution). Needs the FEMM GUI."""
    p = _prefix()
    cmd = p if view == "model" else POST[p]
    fd, path = tempfile.mkstemp(suffix=".png")
    os.close(fd)
    try:
        femm.call(f"{cmd}_zoomnatural()")
    except FemmError:
        pass
    femm.call(f"{cmd}_savebitmap({q(path)})")
    for _ in range(20):
        if os.path.getsize(path) > 0:
            break
        time.sleep(0.1)
    with open(path, "rb") as f:
        data = f.read()
    os.unlink(path)
    if not data:
        raise FemmError("no picture (screenshots need the FEMM GUI, not femm_headless)")
    return Image(data=data, format="png")


# ---------------------------------------------------------------------------
# Escape hatches
# ---------------------------------------------------------------------------

@mcp.tool()
def femm_command(command: str) -> str:
    """Run one FEMM script command (Python syntax), e.g. mi_addmaterial("Steel", 1000, 1000)
    or mo_getb(1, 2), and return its reply. See femm://docs/scripting."""
    return femm.call(command)


@mcp.tool()
def run_python(code: str) -> str:
    """Run a block of Python inside FEMM's interpreter (all FEMM commands available,
    e.g. a loop for a parameter sweep). Assign what you want back to `result`;
    it is returned as text."""
    femm.call("exec(" + q(code) + ", globals())")
    return femm.call("str(globals().get('result', ''))")


@mcp.resource("femm://docs/scripting")
def scripting_docs() -> str:
    """FEMM scripting command reference (Python flavour)."""
    for name in ("pyfemm.md", "luafemm.md", "femm-lua.md"):
        p = _find_resource(name)
        if p:
            with open(p, encoding="utf-8", errors="replace") as f:
                return f.read()
    return "Scripting reference not found; see the FEMM manual (Help > Manual)."


def main() -> None:
    try:
        mcp.run()
    finally:
        if femm.proc:
            femm.proc.terminate()


if __name__ == "__main__":
    main()
