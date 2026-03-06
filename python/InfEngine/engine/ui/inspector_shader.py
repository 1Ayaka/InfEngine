"""
Shader Asset Inspector — path-only editing for .vert / .frag / etc.

Displays the shader type, GUID, and source path.  Path editing writes
the updated path back to the AssetDatabase and invalidates
dependent shader caches + material pipelines.

Public entry point:
    ``render_shader_asset_inspector(ctx, panel, file_path)``
"""

from __future__ import annotations

import os
from typing import Optional

from InfEngine.lib import InfGUIContext
from InfEngine.core.asset_types import ShaderAssetInfo, read_meta_file
from .inspector_utils import max_label_w, field_label
from .theme import Theme, ImGuiCol
from .asset_execution_layer import AssetAccessMode, get_asset_execution_layer


# ---------------------------------------------------------------------------
# Module-level state
# ---------------------------------------------------------------------------

_current_path: str = ""
_info: Optional[ShaderAssetInfo] = None
_meta: Optional[dict] = None
_exec_layer = None


def _reset():
    global _current_path, _info, _meta, _exec_layer
    _current_path = ""
    _info = None
    _meta = None
    _exec_layer = None


def _load(file_path: str) -> bool:
    global _current_path, _info, _meta
    if _current_path == file_path and _info is not None:
        return True
    _reset()
    _current_path = file_path
    _meta = read_meta_file(file_path)
    guid = (_meta or {}).get("guid", "")
    _info = ShaderAssetInfo.from_path(file_path, guid=guid)
    return True


# ---------------------------------------------------------------------------
# Public entry
# ---------------------------------------------------------------------------


def render_shader_asset_inspector(ctx: InfGUIContext, panel, file_path: str):
    """Render the Shader asset Inspector for *file_path*."""
    if not _load(file_path):
        ctx.label("Failed to load shader info")
        ctx.label(file_path)
        return

    global _exec_layer
    _exec_layer = get_asset_execution_layer(_exec_layer, "shader", file_path, AssetAccessMode.READ_ONLY_RESOURCE)

    filename = os.path.basename(file_path)

    # ── Header ─────────────────────────────────────────────────────────
    ctx.label(f"Shader: {filename}")

    labels = ["Type", "GUID", "Source Path"]
    lw = max_label_w(ctx, labels)

    # Type (read-only)
    field_label(ctx, "Type", lw)
    ctx.label(_info.shader_type.capitalize() if _info.shader_type else "Unknown")

    # GUID (read-only)
    field_label(ctx, "GUID", lw)
    ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
    ctx.label(_info.guid if _info.guid else "(none)")
    ctx.pop_style_color(1)

    ctx.separator()

    # ── Source Path (editable) ─────────────────────────────────────────
    ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
    if ctx.collapsing_header("Path"):
        field_label(ctx, "Source Path", lw)
        new_path = ctx.text_input("##shader_src_path", _info.source_path, 512)

        if new_path != _info.source_path:
            # Validate extension
            ext = os.path.splitext(new_path)[1].lower()
            valid_exts = {".vert", ".frag", ".geom", ".comp", ".tesc", ".tese"}
            if ext not in valid_exts:
                ctx.push_style_color(ImGuiCol.Text, 0.9, 0.3, 0.3, 1.0)
                ctx.label(f"Invalid shader extension: {ext}")
                ctx.pop_style_color(1)
            elif not os.path.isfile(new_path):
                ctx.push_style_color(ImGuiCol.Text, 0.9, 0.6, 0.2, 1.0)
                ctx.label("Warning: file does not exist")
                ctx.pop_style_color(1)
                # Still allow the change (user might create the file later)
                ctx.button("Apply Path Change", lambda: _apply_path_change(panel, new_path))
            else:
                ctx.button("Apply Path Change", lambda: _apply_path_change(panel, new_path))

    ctx.separator()

    # ── Shader preview (source code snippet) ───────────────────────────
    ctx.set_next_item_open(False, Theme.COND_FIRST_USE_EVER)
    if ctx.collapsing_header("Source Preview"):
        _render_source_preview(ctx, file_path)


# ---------------------------------------------------------------------------
# Internal
# ---------------------------------------------------------------------------


def _apply_path_change(panel, new_path: str):
    """Commit the path change: move asset in AssetDatabase, invalidate caches."""
    global _info, _exec_layer
    if _info is None:
        return
    old_path = _info.source_path

    _exec_layer = get_asset_execution_layer(_exec_layer, "shader", old_path, AssetAccessMode.READ_ONLY_RESOURCE)
    _exec_layer.move_asset_path(new_path)

    # Invalidate shader cache
    from InfEngine.core.shader import Shader
    # Guess shader_id from filename stem
    shader_id = os.path.splitext(os.path.basename(old_path))[0]
    Shader.invalidate(shader_id)

    _info.source_path = new_path
    _info.shader_type = ShaderAssetInfo.from_path(new_path).shader_type


def _render_source_preview(ctx: InfGUIContext, file_path: str):
    """Show the first ~40 lines of the shader source as read-only text."""
    if not os.path.isfile(file_path):
        ctx.label("(file not found)")
        return
    try:
        with open(file_path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()[:40]
        text = "".join(lines)
        if len(lines) == 40:
            text += "\n... (truncated)"
        ctx.push_style_color(ImGuiCol.Text, 0.7, 0.8, 0.7, 1.0)
        ctx.label(text)
        ctx.pop_style_color(1)
    except Exception:
        ctx.label("(failed to read source)")


def invalidate():
    """Reset module state."""
    _reset()
