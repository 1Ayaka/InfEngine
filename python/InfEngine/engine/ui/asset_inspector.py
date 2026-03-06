"""
Unified Asset Inspector — data-driven inspector for all asset types.

One loader, one state machine, one renderer entry point.  Categories register
via ``AssetCategoryDef``, each specifying how to load data, which editable
fields to expose, and optional custom sections (preview, shader editing, etc.).

Read-only assets (texture, audio, shader) share an Apply / Revert bar.
Read-write assets (material) use automatic debounced save.

Public API::

    render_asset_inspector(ctx, panel, file_path, category)
    invalidate()
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, List, Optional, Tuple

from InfEngine.lib import InfGUIContext
from InfEngine.core.asset_types import (
    TextureType,
    ShaderAssetInfo,
    read_meta_file,
    read_texture_import_settings,
    read_audio_import_settings,
)
from .inspector_utils import max_label_w, field_label, render_apply_revert
from .theme import Theme, ImGuiCol
from .asset_execution_layer import AssetAccessMode, get_asset_execution_layer


# ═══════════════════════════════════════════════════════════════════════════
# Field descriptor
# ═══════════════════════════════════════════════════════════════════════════


class FieldType(Enum):
    """Widget type for an editable import-settings field."""
    CHECKBOX = "checkbox"
    COMBO = "combo"


@dataclass
class FieldDef:
    """Describes one editable field on an import-settings object.

    * *key* — attribute name on the settings dataclass.
    * *label* — display text in the Inspector.
    * *field_type* — which ImGui widget to render.
    * *combo_entries* — ``[(display_label, value), ...]`` for COMBO fields.
    """
    key: str
    label: str
    field_type: FieldType
    combo_entries: List[Tuple[str, Any]] = field(default_factory=list)


# ═══════════════════════════════════════════════════════════════════════════
# Category definition
# ═══════════════════════════════════════════════════════════════════════════


@dataclass
class AssetCategoryDef:
    """Registration for one asset category.

    * *load_fn* returns ``(settings_obj, extra_dict)`` or ``None`` on failure.
      For read-only assets the settings object must implement ``.copy()``
      and ``__eq__`` for dirty tracking.
    * *refresh_fn* is called every frame when the asset is already loaded
      (e.g. material re-serializes native data).
    * *custom_header_fn(ctx, panel, state)* renders after the standard header
      (e.g. texture preview).
    * *custom_body_fn(ctx, panel, state)* replaces the auto-generated
      import-settings field list (e.g. material properties, shader path editing).
    """
    display_name: str
    access_mode: AssetAccessMode
    load_fn: Callable[[str], Optional[Tuple[Any, dict]]]
    refresh_fn: Optional[Callable] = None
    editable_fields: List[FieldDef] = field(default_factory=list)
    extra_meta_keys: List[str] = field(default_factory=list)
    custom_header_fn: Optional[Callable] = None
    custom_body_fn: Optional[Callable] = None
    autosave_debounce: float = 0.35


# ═══════════════════════════════════════════════════════════════════════════
# Unified state
# ═══════════════════════════════════════════════════════════════════════════


class _State:
    """Per-asset inspector state (only one asset is inspected at a time)."""

    def __init__(self):
        self.reset()

    def reset(self):
        self.file_path: str = ""
        self.category: str = ""
        self.meta: Optional[dict] = None
        self.settings: Any = None
        self.disk_settings: Any = None   # snapshot for dirty check (read-only)
        self.exec_layer = None
        self.extra: dict = {}

    def load(self, file_path: str, category: str,
             cat_def: AssetCategoryDef) -> bool:
        # Already loaded — just refresh.
        if (self.file_path == file_path
                and self.category == category
                and self.settings is not None):
            if cat_def.refresh_fn:
                cat_def.refresh_fn(self)
            return True
        # Fresh load
        self.reset()
        self.file_path = file_path
        self.category = category
        self.meta = read_meta_file(file_path)
        result = cat_def.load_fn(file_path)
        if result is None:
            return False
        settings, extra = result
        if settings is None:
            return False
        self.settings = settings
        self.extra = extra
        if (cat_def.access_mode == AssetAccessMode.READ_ONLY_RESOURCE
                and hasattr(settings, "copy")):
            self.disk_settings = settings.copy()
        return True

    def is_dirty(self) -> bool:
        if self.disk_settings is None:
            return False
        return self.settings != self.disk_settings


_state = _State()


# ═══════════════════════════════════════════════════════════════════════════
# Category registry
# ═══════════════════════════════════════════════════════════════════════════

_categories: Dict[str, AssetCategoryDef] = {}
_initialized = False


def _ensure_categories():
    global _initialized
    if _initialized:
        return
    _initialized = True

    # ── Texture ────────────────────────────────────────────────────────
    _categories["texture"] = AssetCategoryDef(
        display_name="Texture",
        access_mode=AssetAccessMode.READ_ONLY_RESOURCE,
        load_fn=_load_texture,
        editable_fields=[
            FieldDef("texture_type", "Texture Type", FieldType.COMBO,
                     [("Default", TextureType.DEFAULT),
                      ("NormalMap", TextureType.NORMAL_MAP),
                      ("UI", TextureType.UI)]),
            FieldDef("srgb", "sRGB", FieldType.CHECKBOX),
            FieldDef("max_size", "Max Size", FieldType.COMBO,
                     [(str(s), s) for s in
                      (32, 64, 128, 256, 512, 1024, 2048, 4096, 8192)]),
        ],
        custom_header_fn=_render_texture_preview,
    )

    # ── Audio ──────────────────────────────────────────────────────────
    _categories["audio"] = AssetCategoryDef(
        display_name="Audio",
        access_mode=AssetAccessMode.READ_ONLY_RESOURCE,
        load_fn=_load_audio,
        editable_fields=[
            FieldDef("force_mono", "Force Mono", FieldType.CHECKBOX),
        ],
        extra_meta_keys=["file_size", "extension"],
    )

    # ── Shader ─────────────────────────────────────────────────────────
    _categories["shader"] = AssetCategoryDef(
        display_name="Shader",
        access_mode=AssetAccessMode.READ_ONLY_RESOURCE,
        load_fn=_load_shader,
        custom_body_fn=_render_shader_body,
    )

    # ── Material ───────────────────────────────────────────────────────
    _categories["material"] = AssetCategoryDef(
        display_name="Material",
        access_mode=AssetAccessMode.READ_WRITE_RESOURCE,
        load_fn=_load_material,
        refresh_fn=_refresh_material,
        custom_body_fn=_render_material_body,
        autosave_debounce=0.35,
    )


# ═══════════════════════════════════════════════════════════════════════════
# Per-category loaders
# ═══════════════════════════════════════════════════════════════════════════


def _load_texture(path: str):
    return read_texture_import_settings(path), {"preview_height": 200.0}


def _load_audio(path: str):
    return read_audio_import_settings(path), {}


def _load_shader(path: str):
    meta = read_meta_file(path)
    guid = (meta or {}).get("guid", "")
    return ShaderAssetInfo.from_path(path, guid=guid), {}


def _load_material(path: str):
    from InfEngine.core.material import Material
    mat = Material.load(path)
    if mat is None:
        return None
    native = mat.native
    try:
        cached = json.loads(native.serialize())
    except Exception:
        cached = {"name": mat.name, "properties": {}}
    return mat, {
        "native_mat": native,
        "cached_data": cached,
        "shader_cache": {".vert": None, ".frag": None},
    }


def _refresh_material(state: _State):
    native = state.extra.get("native_mat")
    if native:
        try:
            state.extra["cached_data"] = json.loads(native.serialize())
        except Exception:
            pass


# ═══════════════════════════════════════════════════════════════════════════
# Public API
# ═══════════════════════════════════════════════════════════════════════════


def render_asset_inspector(ctx: InfGUIContext, panel,
                           file_path: str, category: str):
    """Single entry point for all asset inspectors."""
    _ensure_categories()
    cat_def = _categories.get(category)
    if cat_def is None:
        ctx.label(f"Unknown asset type: {category}")
        return

    if not _state.load(file_path, category, cat_def):
        ctx.label(f"Failed to load {cat_def.display_name}")
        ctx.label(file_path)
        return

    _state.exec_layer = get_asset_execution_layer(
        _state.exec_layer, category, file_path, cat_def.access_mode,
        autosave_debounce_sec=cat_def.autosave_debounce,
    )

    # ── Header (shared for all categories) ─────────────────────────────
    _render_header(ctx, cat_def, _state)

    # ── Custom header additions (e.g. texture preview) ─────────────────
    if cat_def.custom_header_fn:
        cat_def.custom_header_fn(ctx, panel, _state)

    # ── Body (auto-generated fields or custom) ─────────────────────────
    if cat_def.custom_body_fn:
        cat_def.custom_body_fn(ctx, panel, _state)
    elif cat_def.editable_fields:
        _render_import_fields(ctx, cat_def, _state)

    # ── Footer ─────────────────────────────────────────────────────────
    if (cat_def.access_mode == AssetAccessMode.READ_ONLY_RESOURCE
            and cat_def.editable_fields):
        render_apply_revert(
            ctx, _state.is_dirty(),
            on_apply=lambda: _on_apply(),
            on_revert=_on_revert,
        )
    elif cat_def.access_mode == AssetAccessMode.READ_WRITE_RESOURCE:
        if _state.exec_layer:
            _state.exec_layer.flush_rw_autosave()


def invalidate():
    """Reset all inspector state (called on selection change)."""
    _state.reset()


# ═══════════════════════════════════════════════════════════════════════════
# Shared rendering helpers
# ═══════════════════════════════════════════════════════════════════════════


def _render_header(ctx: InfGUIContext, cat_def: AssetCategoryDef,
                   state: _State):
    """Render the standard asset header: name, GUID, path, extra meta."""
    filename = os.path.basename(state.file_path)
    ctx.label(f"{cat_def.display_name}: {filename}")

    # GUID — try .meta first, then serialized data (material stores it inside)
    guid = (state.meta or {}).get("guid", "")
    if not guid:
        cached = state.extra.get("cached_data")
        if cached:
            guid = cached.get("guid", "")
    if guid:
        ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
        ctx.label(f"GUID: {guid}")
        ctx.pop_style_color(1)

    # Path
    ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
    ctx.label(f"Path: {state.file_path}")
    ctx.pop_style_color(1)

    # Extra metadata from .meta (e.g. file_size, extension for audio)
    if cat_def.extra_meta_keys and state.meta:
        for key in cat_def.extra_meta_keys:
            val = state.meta.get(key, "")
            if not val:
                continue
            ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
            if key == "file_size":
                _render_file_size(ctx, val)
            else:
                ctx.label(f"{key.replace('_', ' ').title()}: {val}")
            ctx.pop_style_color(1)

    ctx.separator()


def _render_file_size(ctx: InfGUIContext, val):
    try:
        size = int(val)
        if size >= 1048576:
            ctx.label(f"Size: {size / 1048576:.2f} MB")
        elif size >= 1024:
            ctx.label(f"Size: {size / 1024:.1f} KB")
        else:
            ctx.label(f"Size: {size} bytes")
    except (ValueError, TypeError):
        ctx.label(f"Size: {val}")


def _render_import_fields(ctx: InfGUIContext, cat_def: AssetCategoryDef,
                          state: _State):
    """Auto-render editable import-settings fields from descriptors."""
    ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
    if ctx.collapsing_header("Import Settings"):
        labels = [f.label for f in cat_def.editable_fields]
        lw = max_label_w(ctx, labels)

        for fdef in cat_def.editable_fields:
            cur = getattr(state.settings, fdef.key)
            wid = f"##{fdef.key}"

            if fdef.field_type == FieldType.CHECKBOX:
                new_val = ctx.checkbox(fdef.label, cur)
                if new_val != cur:
                    setattr(state.settings, fdef.key, new_val)

            elif fdef.field_type == FieldType.COMBO:
                field_label(ctx, fdef.label, lw)
                display_labels = [e[0] for e in fdef.combo_entries]
                values = [e[1] for e in fdef.combo_entries]
                try:
                    idx = values.index(cur)
                except ValueError:
                    idx = 0
                new_idx = ctx.combo(wid, idx, display_labels)
                if new_idx != idx:
                    setattr(state.settings, fdef.key, values[new_idx])


# ── Apply / Revert actions ─────────────────────────────────────────────


def _on_apply():
    if _state.settings is None or _state.exec_layer is None:
        return
    ok = _state.exec_layer.apply_import_settings(_state.settings)
    if ok and hasattr(_state.settings, "copy"):
        _state.disk_settings = _state.settings.copy()


def _on_revert():
    _state.file_path = ""  # force full reload next frame


# ═══════════════════════════════════════════════════════════════════════════
# Texture — preview section (custom_header_fn)
# ═══════════════════════════════════════════════════════════════════════════

_PREVIEW_MIN_H = 60.0
_PREVIEW_MAX_H = 800.0
_SPLITTER_H = 14.0


def _render_texture_preview(ctx: InfGUIContext, panel, state: _State):
    """Render texture preview image + drag-to-resize splitter."""
    if not panel or not hasattr(panel, "_InspectorPanel__preview_manager"):
        return
    pm = panel._InspectorPanel__preview_manager
    if not pm or not pm.load_preview(state.file_path):
        return

    settings = state.settings
    display_mode = 1 if settings.texture_type == TextureType.NORMAL_MAP else 0
    pm.set_preview_settings(display_mode, settings.max_size, settings.srgb)

    avail_w = ctx.get_content_region_avail_width()
    preview_h = min(avail_w, state.extra.get("preview_height", 200.0))
    if avail_w > 0 and preview_h > 0:
        pm.render_preview(ctx, avail_w, preview_h)

    # ── Drag splitter ──────────────────────────────────────────────────
    ctx.separator()
    avail_w = ctx.get_content_region_avail_width()
    ctx.invisible_button("##TexPreviewSplitter", avail_w, _SPLITTER_H)
    if ctx.is_item_hovered() or ctx.is_item_active():
        ctx.set_mouse_cursor(3)  # ResizeNS
    if ctx.is_item_active():
        dy = ctx.get_mouse_drag_delta_y(0)
        if abs(dy) > 1.0:
            h = state.extra.get("preview_height", 200.0)
            state.extra["preview_height"] = max(
                _PREVIEW_MIN_H, min(_PREVIEW_MAX_H, h + dy))
            ctx.reset_mouse_drag_delta(0)
    ctx.separator()


# ═══════════════════════════════════════════════════════════════════════════
# Shader — custom body (path editing + source preview)
# ═══════════════════════════════════════════════════════════════════════════


def _render_shader_body(ctx: InfGUIContext, panel, state: _State):
    info = state.settings  # ShaderAssetInfo

    # Shader type (read-only)
    lw = max_label_w(ctx, ["Type"])
    field_label(ctx, "Type", lw)
    ctx.label(info.shader_type.capitalize() if info.shader_type else "Unknown")
    ctx.separator()

    # ── Path editing ───────────────────────────────────────────────────
    ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
    if ctx.collapsing_header("Path"):
        plw = max_label_w(ctx, ["Source Path"])
        field_label(ctx, "Source Path", plw)
        new_path = ctx.text_input("##shader_src_path", info.source_path, 512)

        if new_path != info.source_path:
            ext = os.path.splitext(new_path)[1].lower()
            valid = {".vert", ".frag", ".geom", ".comp", ".tesc", ".tese"}
            if ext not in valid:
                ctx.push_style_color(ImGuiCol.Text, 0.9, 0.3, 0.3, 1.0)
                ctx.label(f"Invalid shader extension: {ext}")
                ctx.pop_style_color(1)
            else:
                if not os.path.isfile(new_path):
                    ctx.push_style_color(ImGuiCol.Text, 0.9, 0.6, 0.2, 1.0)
                    ctx.label("Warning: file does not exist")
                    ctx.pop_style_color(1)
                ctx.button("Apply Path Change",
                           lambda np=new_path: _apply_shader_path(
                               state, np))

    ctx.separator()

    # ── Source preview ─────────────────────────────────────────────────
    ctx.set_next_item_open(False, Theme.COND_FIRST_USE_EVER)
    if ctx.collapsing_header("Source Preview"):
        _render_shader_source(ctx, state.file_path)


def _apply_shader_path(state: _State, new_path: str):
    info = state.settings
    old_path = info.source_path
    if state.exec_layer:
        state.exec_layer.move_asset_path(new_path)
    from InfEngine.core.shader import Shader
    shader_id = os.path.splitext(os.path.basename(old_path))[0]
    Shader.invalidate(shader_id)
    info.source_path = new_path
    info.shader_type = ShaderAssetInfo.from_path(new_path).shader_type


def _render_shader_source(ctx: InfGUIContext, file_path: str):
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


# ═══════════════════════════════════════════════════════════════════════════
# Material — custom body (delegates to inspector_material)
# ═══════════════════════════════════════════════════════════════════════════


def _render_material_body(ctx: InfGUIContext, panel, state: _State):
    from . import inspector_material as mat_ui
    mat_ui.render_material_body(ctx, panel, state)
