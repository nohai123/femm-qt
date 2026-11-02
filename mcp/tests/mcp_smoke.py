# SPDX-License-Identifier: MIT
# End-to-end check of femm_mcp.py through a real MCP client session:
# builds a coil with an iron core, solves it and reads the results.
#
#   uv run --with mcp python mcp/tests/mcp_smoke.py [screenshot.png]
#
# Talks to a running FEMM (or lets the server start one).
import asyncio, base64, json, os, sys, tempfile

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, "..", "femm_mcp.py")


async def main():
    params = StdioServerParameters(command=sys.executable, args=[SERVER], env=dict(os.environ))
    async with stdio_client(params) as (r, w):
        async with ClientSession(r, w) as s:
            await s.initialize()
            tools = {t.name for t in (await s.list_tools()).tools}
            print("tools:", len(tools), sorted(tools))

            async def call(tool, **kw):
                res = await s.call_tool(tool, kw)
                if getattr(res, "is_error", None) or getattr(res, "isError", False):
                    raise RuntimeError(f"{tool}: {res.content[0].text}")
                out = []
                for c in res.content:
                    out.append(c.text if c.type == "text" else c)
                return out[0] if len(out) == 1 else out

            print(await call("new_problem", physics="magnetics", units="millimeters", type="axisymmetric"))
            print(await call("draw_rectangle", x0=0, y0=-20, x1=8, y1=20))       # iron core
            print(await call("draw_rectangle", x0=10, y0=-15, x1=20, y1=15))     # winding
            print(await call("draw_arc", x0=0, y0=-80, x1=0, y1=80, angle_deg=180))
            print(await call("draw_polyline", points=[[0, -80], [0, -20]]))
            print(await call("draw_polyline", points=[[0, 20], [0, 80]]))
            print(await call("search_materials", query="1010"))
            for m in ("Air", "ARMCO Pure Iron", "Copper"):
                try:
                    print(await call("add_library_material", name=m))
                except RuntimeError as e:
                    print("  (", e, ")")
            try:
                await call("assign_region", x=1, y=1, material="Unobtainium")
                raise AssertionError("unknown material accepted")
            except RuntimeError as e:
                assert "Unobtainium" in str(e), e    # the real message reaches the client
                print("  error reported:", str(e)[:90])
            print(await call("add_circuit", name="coil", current=2.0))
            print(await call("assign_region", x=4, y=0, material="ARMCO Pure Iron"))
            print(await call("assign_region", x=15, y=0, material="Copper", circuit="coil", turns=200))
            print(await call("assign_region", x=40, y=40, material="Air"))
            print(await call("add_boundary", name="A0"))
            print(await call("set_boundary", x=56.6, y=56.6, boundary="A0"))
            model = json.loads(await call("get_model"))
            # The outer boundary is the arc: the condition must be on it
            assert any(a.get("boundary") == "A0" for a in model["arcs"]), "A0 not on the outer arc"
            assert not any(sg.get("boundary") for sg in model["segments"]), "A0 landed on a segment"
            print("model:", len(model["nodes"]), "nodes", len(model["labels"]), "labels", model["materials"])
            print(await call("analyze", save_as=os.path.join(tempfile.gettempdir(), "mcp_coil.fem")))
            pv = json.loads(await call("point_values", x=4, y=0))
            print("B in core:", pv["B1"], pv["B2"])
            cp = json.loads(await call("circuit_properties", name="coil"))
            print("circuit:", cp)
            # Reference: same model solved by a plain script (A=0 on the arc,
            # linear core mu_r 500 gives 2.02 mH; the iron core is close)
            assert 1.5e-3 < cp["inductance"] < 2.6e-3, cp["inductance"]
            f = await call("block_integral", points=[[4, 0]], integral_type=19)
            print("Fz on core:", f)
            res = await call("run_python", code="result = 6*7")
            assert res == "42", res
            img = await call("screenshot", view="results")
            if len(sys.argv) > 1 and not isinstance(img, str):
                with open(sys.argv[1], "wb") as fh:
                    fh.write(base64.b64decode(img.data))
                print("screenshot ->", sys.argv[1])
            print("MCP SMOKE OK")


asyncio.run(main())
