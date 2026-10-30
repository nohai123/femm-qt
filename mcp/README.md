# FEMM MCP server

`femm_mcp.py` connects AI assistants that speak the
[Model Context Protocol](https://modelcontextprotocol.io) (Claude Code, Claude
Desktop, …) to FEMM. The assistant can build a model, solve it and evaluate
it, and you can watch it happen in the FEMM window.

## Setup

Requires Python 3.10+ and [uv](https://docs.astral.sh/uv/) (uv installs the
`mcp` package on first run).

Claude Code:

```bash
claude mcp add femm -- uv run /path/to/femm-qt/mcp/femm_mcp.py
```

Claude Desktop (`claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "femm": { "command": "uv", "args": ["run", "/path/to/femm-qt/mcp/femm_mcp.py"] }
  }
}
```

## How it connects

* **FEMM already open** (you started it): the assistant works in that window.
  The server connects per tool call, so the window stays yours: it is not
  closed when the assistant disconnects, and your own scripts can use the
  same port in between.
* **FEMM not running**: the server starts `femm_gui` (or `femm_headless` with
  `FEMM_MCP_HEADLESS=1`) and closes it again at the end of the session.

Environment: `FEMM_PORT` (default 48015), `FEMM_DIR` (folder with
`femm_gui`/`femm_headless`; the macOS app and a build tree next to this folder
are found automatically).

## Tools

| Area | Tools |
|---|---|
| Problem | `new_problem`, `open_document`, `save_document`, `get_model` |
| Geometry | `draw_polyline`, `draw_rectangle`, `draw_circle`, `draw_arc`, `delete_all_geometry` |
| Physics | `search_materials`, `add_library_material`, `assign_region`, `add_circuit`, `add_boundary`, `set_boundary`, `open_boundary` |
| Solve | `analyze` |
| Results | `point_values`, `circuit_properties` (incl. inductance and resistance), `block_integral` (forces, torque, energy, losses), `line_integral`, `screenshot` |
| Anything else | `femm_command` (one script command), `run_python` (a block of Python inside FEMM, e.g. a parameter sweep) |

Resource `femm://docs/scripting` holds the scripting reference.

## Test

With FEMM open (or not), `uv run --with mcp python mcp/tests/mcp_smoke.py`
builds a coil with an iron core through a real MCP session, solves it and
checks inductance, force and a screenshot.
