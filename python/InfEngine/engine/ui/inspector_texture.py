"""
Texture Asset Inspector — Texture Import Settings editor.

Renders a preview + editable import settings when a texture file (.png etc.)
is selected in the Project panel.  Only settings that are actually consumed
by the engine are shown.

Public entry point:
    ``render_texture_asset_inspector(ctx, panel, file_path)``
"""

from __future__ import annotations

import os
from typing import Optional

from InfEngine.lib import InfGUIContext
from InfEngine.core.asset_types import (
    TextureImportSettings, TextureType,
    read_texture_import_settings,
    read_meta_file,
)
from .inspector_utils import max_label_w, field_label, render_apply_revert
from .theme import Theme, ImGuiCol
from .asset_execution_layer import AssetAccessMode, get_asset_execution_layer


# ---------------------------------------------------------------------------
# Module-level state
# ---------------------------------------------------------------------------

_current_path: str = ""
_settings: Optional[TextureImportSettings] = None
_disk_settings: Optional[TextureImportSettings] = None  # snapshot from disk
_meta: Optional[dict] = None
_dirty: bool = False
_exec_layer = None
_preview_height: float = 200.0  # draggable preview height

_PREVIEW_MIN_H = 60.0
_PREVIEW_MAX_H = 800.0
_SPLITTER_H = 14.0


def _reset():
    global _current_path, _settings, _disk_settings, _meta, _dirty, _preview_height, _exec_layer
    _current_path = ""
    _settings = None
    _disk_settings = None
    _meta = None
    _dirty = False
    _exec_layer = None
    _preview_height = 200.0


def _load(file_path: str) -> bool:
    global _current_path, _settings, _disk_settings, _meta, _dirty
    if _current_path == file_path and _settings is not None:
        return True
    _reset()
    _current_path = file_path
    _settings = read_texture_import_settings(file_path)
    _disk_settings = _settings.copy()
    _meta = read_meta_file(file_path)
    _dirty = False
    return True


# ---------------------------------------------------------------------------
# Preview splitter
# ---------------------------------------------------------------------------

def _render_preview_splitter(ctx: InfGUIContext):
    """Horizontal drag-bar below the preview image to resize it."""
    global _preview_height
    ctx.separator()
    avail_w = ctx.get_content_region_avail_width()
    ctx.invisible_button("##TexPreviewSplitter", avail_w, _SPLITTER_H)

    is_hovered = ctx.is_item_hovered()
    is_active = ctx.is_item_active()

    if is_hovered or is_active:
        ctx.set_mouse_cursor(3)  # ResizeNS

    if is_active:
        dy = ctx.get_mouse_drag_delta_y(0)
        if abs(dy) > 1.0:
            _preview_height = max(_PREVIEW_MIN_H, min(_PREVIEW_MAX_H, _preview_height + dy))
            ctx.reset_mouse_drag_delta(0)

    ctx.separator()


# ---------------------------------------------------------------------------
# Size presets (Unity-style)
# ---------------------------------------------------------------------------

_SIZE_OPTIONS = [32, 64, 128, 256, 512, 1024, 2048, 4096, 8192]
_SIZE_LABELS = [str(s) for s in _SIZE_OPTIONS]


# ---------------------------------------------------------------------------
# Public entry
# ---------------------------------------------------------------------------


def render_texture_asset_inspector(ctx: InfGUIContext, panel, file_path: str):
    """Render the Texture asset Inspector for *file_path*."""
    if not _load(file_path):
        ctx.label("Failed to load texture metadata")
        ctx.label(file_path)
        return

    global _settings, _dirty, _exec_layer
    _exec_layer = get_asset_execution_layer(_exec_layer, "texture", file_path, AssetAccessMode.READ_ONLY_RESOURCE)

    filename = os.path.basename(file_path)

    # ── Header with file info ──────────────────────────────────────────
    ctx.label(f"Texture: {filename}")

    # GUID
    guid = (_meta or {}).get("guid", "")
    if guid:
        ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
        ctx.label(f"GUID: {guid}")
        ctx.pop_style_color(1)
    ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
    ctx.label(f"Path: {file_path}")
    ctx.pop_style_color(1)

    ctx.separator()

    # ── Preview (delegate to backend preview manager) ──────────────────
    if panel and hasattr(panel, '_load_preview') and hasattr(panel, '_InspectorPanel__preview_manager'):
        pm = panel._InspectorPanel__preview_manager
        if pm:
            if pm.load_preview(file_path):
                # Sync all preview settings so the image reflects current import options
                display_mode = 1 if _settings.texture_type == TextureType.NORMAL_MAP else 0
                pm.set_preview_settings(display_mode, _settings.max_size, _settings.srgb)
                avail_w = ctx.get_content_region_avail_width()
                preview_h = min(avail_w, _preview_height)
                if avail_w > 0 and preview_h > 0:
                    pm.render_preview(ctx, avail_w, preview_h)
                _render_preview_splitter(ctx)
                ctx.separator()

    # ── Import Settings ────────────────────────────────────────────────
    ctx.set_next_item_open(True, Theme.COND_FIRST_USE_EVER)
    if ctx.collapsing_header("Import Settings"):
        labels = ["Texture Type", "sRGB", "Max Size"]
        lw = max_label_w(ctx, labels)

        # Texture Type
        field_label(ctx, "Texture Type", lw)
        tt_names = ["Default", "NormalMap", "UI"]
        new_tt = ctx.combo("##tex_type", int(_settings.texture_type), tt_names)
        if new_tt != int(_settings.texture_type):
            _settings.texture_type = TextureType(new_tt)
            _dirty = True

        # sRGB
        new_srgb = ctx.checkbox("sRGB", _settings.srgb)
        if new_srgb != _settings.srgb:
            _settings.srgb = new_srgb
            _dirty = True

        # Max Size
        field_label(ctx, "Max Size", lw)
        current_idx = _SIZE_OPTIONS.index(_settings.max_size) if _settings.max_size in _SIZE_OPTIONS else 6
        new_idx = ctx.combo("##max_size", current_idx, _SIZE_LABELS)
        if new_idx != current_idx:
            _settings.max_size = _SIZE_OPTIONS[new_idx]
            _dirty = True

    # ── Dirty indicator + Apply / Revert ───────────────────────────────
    _dirty = _settings != _disk_settings

    render_apply_revert(
        ctx, _dirty,
        on_apply=lambda: _apply(panel, file_path),
        on_revert=lambda: _revert(file_path),
    )


# ---------------------------------------------------------------------------
# Actions
# ---------------------------------------------------------------------------


def _apply(panel, file_path: str):
    """Write import settings to .meta and trigger reimport."""
    global _dirty, _disk_settings, _exec_layer
    if _settings is None:
        return
    _exec_layer = get_asset_execution_layer(_exec_layer, "texture", file_path, AssetAccessMode.READ_ONLY_RESOURCE)
    ok = _exec_layer.apply_import_settings(_settings)
    if ok:
        _disk_settings = _settings.copy()
        _dirty = False


def _revert(file_path: str):
    """Reload settings from disk, discarding edits."""
    global _current_path
    _current_path = ""  # force full reload
    _load(file_path)


def invalidate():
    """Reset module state."""
    _reset()
