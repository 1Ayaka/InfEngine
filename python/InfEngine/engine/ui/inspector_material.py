"""
Material Asset Inspector — body renderer for the unified asset inspector.

This module provides ``render_material_body(ctx, panel, state)`` which renders
the material-specific UI sections: shader selection, render settings,
dynamic properties, and auto-save scheduling.

State is managed by the unified ``asset_inspector`` module.
"""

from __future__ import annotations

import json
import os
from typing import Optional

from InfEngine.lib import InfGUIContext
from .inspector_utils import max_label_w, field_label, LABEL_PAD
from .theme import Theme, ImGuiCol
from . import inspector_shader_utils as shader_utils


# ═══════════════════════════════════════════════════════════════════════════
# Module-level shortcuts (set per render call from unified state)
# ═══════════════════════════════════════════════════════════════════════════

_native_mat = None
_cached_data: Optional[dict] = None
_shader_cache: dict = {".vert": None, ".frag": None}


# ═══════════════════════════════════════════════════════════════════════════
# Body renderer (called from asset_inspector)
# ═══════════════════════════════════════════════════════════════════════════


def render_material_body(ctx: InfGUIContext, panel, state):
    """Render the material-specific inspector body.

    *state* is the ``_State`` object from ``asset_inspector``.  Relevant
    fields: ``state.settings`` (Material wrapper), ``state.extra``
    (native_mat, cached_data, shader_cache), ``state.exec_layer``.
    """
    global _native_mat, _cached_data, _shader_cache

    _native_mat = state.extra["native_mat"]
    _cached_data = state.extra["cached_data"]
    _shader_cache = state.extra["shader_cache"]
    exec_layer = state.exec_layer

    mat_data = _cached_data
    is_builtin = mat_data.get("builtin", False)

    if is_builtin:
        ctx.label("(Built-in — Shader locked)")

    changed = False
    requires_deserialize = False
    requires_pipeline_refresh = False

    # Sync shader annotations
    frag_shader_id = mat_data.get("shaders", {}).get("fragment", "")
    if frag_shader_id:
        shader_utils.sync_properties_from_shader(mat_data, frag_shader_id, ".frag", remove_unknown=False)

    # ── Shader Section ─────────────────────────────────────────────────
    if is_builtin:
        ctx.begin_disabled(True)
    ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
    if ctx.collapsing_header("Shader"):
        shaders = mat_data.setdefault("shaders", {})
        vert_path = shaders.get("vertex", "")
        frag_path = shaders.get("fragment", "")
        s_lw = max_label_w(ctx, ["Vertex", "Fragment"])

        # Vertex shader
        field_label(ctx, "Vertex", s_lw)
        vert_items = shader_utils.get_shader_candidates(".vert", _shader_cache)
        vert_display = shader_utils.shader_display_from_value(vert_path, vert_items)
        if _render_obj_field(ctx, "mat_vert", vert_display, "Vert", "SHADER_FILE",
                             lambda p: _on_shader_drop(p, ".vert", shaders)):
            ctx.open_popup("mat_vert_popup")
        if ctx.begin_popup("mat_vert_popup"):
            for display, value in vert_items:
                if ctx.selectable(display, value == vert_path):
                    shaders["vertex"] = value
                    changed = True
                    requires_deserialize = True
                    requires_pipeline_refresh = True
            ctx.end_popup()

        # Fragment shader
        field_label(ctx, "Fragment", s_lw)
        frag_items = shader_utils.get_shader_candidates(".frag", _shader_cache)
        frag_display = shader_utils.shader_display_from_value(frag_path, frag_items)
        if _render_obj_field(ctx, "mat_frag", frag_display, "Frag", "SHADER_FILE",
                             lambda p: _on_shader_drop(p, ".frag", shaders)):
            ctx.open_popup("mat_frag_popup")
        if ctx.begin_popup("mat_frag_popup"):
            for display, value in frag_items:
                if ctx.selectable(display, value == frag_path):
                    old_frag = shaders.get("fragment", "")
                    shaders["fragment"] = value
                    changed = True
                    requires_deserialize = True
                    requires_pipeline_refresh = True
                    if value != old_frag:
                        shader_utils.sync_properties_from_shader(mat_data, value, ".frag", remove_unknown=True)
            ctx.end_popup()
    if is_builtin:
        ctx.end_disabled()

    ctx.separator()

    # ── Render Settings ────────────────────────────────────────────────
    if is_builtin:
        ctx.begin_disabled(True)
    ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
    if ctx.collapsing_header("Render Settings"):
        rs = mat_data.setdefault("renderState", {})
        rq = int(rs.get("renderQueue", 2000))
        lw = max_label_w(ctx, ["Queue"])
        field_label(ctx, "Queue", lw)
        new_rq = int(ctx.drag_int("##mat_render_queue", rq, 1.0, 0, 10000))
        if new_rq != rq:
            rs["renderQueue"] = new_rq
            changed = True
            requires_deserialize = True
            requires_pipeline_refresh = True
    if is_builtin:
        ctx.end_disabled()

    ctx.separator()

    # ── Properties ─────────────────────────────────────────────────────
    ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
    if ctx.collapsing_header("Properties"):
        props = mat_data.get("properties", {})
        if not props:
            ctx.label("(No properties)")
        else:
            plw = max_label_w(ctx, sorted(props.keys()))
            for prop_name in sorted(props.keys()):
                prop = props[prop_name]
                ptype = int(prop.get("type", 0))
                value = prop.get("value")
                prop_changed = _render_property(ctx, prop_name, prop, ptype, value, plw)
                if prop_changed:
                    changed = True

    ctx.separator()

    # ── Auto-save on change ─────────────────────────────────────────────
    if changed:
        try:
            if requires_deserialize:
                _native_mat.deserialize(json.dumps(mat_data))
            if requires_pipeline_refresh:
                _refresh_pipeline(panel)
            if exec_layer:
                exec_layer.schedule_rw_save(_native_mat)
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------


def _on_shader_drop(path: str, required_ext: str, shaders_dict: dict):
    if path.lower().endswith(required_ext):
        key = "vertex" if required_ext == ".vert" else "fragment"
        old = shaders_dict.get(key, "")
        shaders_dict[key] = path
        if key == "fragment" and path != old and _cached_data:
            shader_utils.sync_properties_from_shader(_cached_data, path, ".frag", remove_unknown=True)


def _render_obj_field(ctx: InfGUIContext, fid: str, display: str, type_hint: str,
                      drag_type: str, on_drop) -> bool:
    """Simplified object-field renderer accepting drag-drop."""
    from . import inspector_components as comp_ui
    return comp_ui.render_object_field(ctx, fid, display, type_hint,
                                       clickable=True,
                                       accept_drag_type=drag_type,
                                       on_drop_callback=on_drop)


def _render_property(ctx, prop_name, prop, ptype, value, plw) -> bool:
    """Render one material property row. Returns True if changed."""
    changed = False
    if ptype == 0:  # Float
        field_label(ctx, prop_name, plw)
        nv = float(ctx.drag_float(f"##mp_{prop_name}", float(value), 0.1, 0.0, 100.0))
        if nv != float(value):
            prop["value"] = nv
            _apply_native_prop(prop_name, nv, ptype)
            changed = True
    elif ptype == 1:  # Float2
        x, y = value[0], value[1]
        nx, ny = ctx.vector2(prop_name, float(x), float(y), 0.1, plw)
        if [nx, ny] != [x, y]:
            prop["value"] = [nx, ny]
            _apply_native_prop(prop_name, [nx, ny], ptype)
            changed = True
    elif ptype == 2:  # Float3
        x, y, z = value[0], value[1], value[2]
        nx, ny, nz = ctx.vector3(prop_name, float(x), float(y), float(z), 0.1, plw)
        if [nx, ny, nz] != [x, y, z]:
            prop["value"] = [nx, ny, nz]
            _apply_native_prop(prop_name, [nx, ny, nz], ptype)
            changed = True
    elif ptype == 3:  # Float4
        x, y, z, w = value[0], value[1], value[2], value[3]
        nx, ny, nz, nw = ctx.vector4(prop_name, float(x), float(y), float(z), float(w), 0.1, plw)
        if [nx, ny, nz, nw] != [x, y, z, w]:
            prop["value"] = [nx, ny, nz, nw]
            _apply_native_prop(prop_name, [nx, ny, nz, nw], ptype)
            changed = True
    elif ptype == 4:  # Int
        field_label(ctx, prop_name, plw)
        nv = int(ctx.drag_int(f"##mp_{prop_name}", int(value), 1.0, 0, 0))
        if nv != int(value):
            prop["value"] = nv
            _apply_native_prop(prop_name, nv, ptype)
            changed = True
    elif ptype == 5:  # Mat4
        arr = list(value)
        mat_changed = False
        for row in range(4):
            base = row * 4
            nx, ny, nz, nw = ctx.vector4(f"{prop_name}[{row}]",
                                         float(arr[base]), float(arr[base+1]),
                                         float(arr[base+2]), float(arr[base+3]))
            nr = [nx, ny, nz, nw]
            if nr != arr[base:base+4]:
                arr[base:base+4] = nr
                mat_changed = True
        if mat_changed:
            prop["value"] = arr
            _apply_native_prop(prop_name, arr, ptype)
            changed = True
    elif ptype == 6:  # Texture2D
        tex_path = value if isinstance(value, str) else ""
        field_label(ctx, prop_name, plw)
        nt = ctx.text_input(f"##mp_{prop_name}_tex", tex_path, 256)
        if nt != tex_path:
            prop["value"] = nt
            _apply_native_prop(prop_name, nt, ptype)
            changed = True
    else:
        ctx.label(f"{prop_name}: (type {ptype})")
    return changed


def _apply_native_prop(prop_name: str, value, ptype: int):
    """Forward a property change to the C++ material."""
    if not _native_mat:
        return
    if ptype == 0:
        _native_mat.set_float(prop_name, float(value))
    elif ptype == 1:
        _native_mat.set_vector2(prop_name, (float(value[0]), float(value[1])))
    elif ptype == 2:
        _native_mat.set_vector3(prop_name, (float(value[0]), float(value[1]), float(value[2])))
    elif ptype == 3:
        _native_mat.set_vector4(prop_name, (float(value[0]), float(value[1]), float(value[2]), float(value[3])))
    elif ptype == 4:
        _native_mat.set_int(prop_name, int(value))
    elif ptype == 5:
        _native_mat.set_matrix(prop_name, [float(v) for v in value])
    elif ptype == 6:
        _native_mat.set_texture(prop_name, str(value))


def _refresh_pipeline(panel):
    """Ask the engine to rebuild the material pipeline."""
    engine = panel._get_native_engine() if panel else None
    if engine and _native_mat and hasattr(engine, 'refresh_material_pipeline'):
        engine.refresh_material_pipeline(_native_mat)
