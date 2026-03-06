"""
Audio Asset Inspector — displays audio clip metadata and import settings.

Shows the audio clip's file info (GUID, size, extension) and the single
editable import setting consumed by the engine: Force Mono.

Public entry point:
    ``render_audio_asset_inspector(ctx, panel, file_path)``
"""

from __future__ import annotations

import os
from typing import Optional

from InfEngine.lib import InfGUIContext
from InfEngine.core.asset_types import (
    AudioImportSettings,
    read_meta_file, read_audio_import_settings,
)
from .inspector_utils import max_label_w, field_label, render_apply_revert
from .theme import ImGuiCol
from .asset_execution_layer import AssetAccessMode, get_asset_execution_layer


# ---------------------------------------------------------------------------
# Module-level state
# ---------------------------------------------------------------------------

_current_path: str = ""
_meta: Optional[dict] = None
_settings: Optional[AudioImportSettings] = None
_original_settings: Optional[AudioImportSettings] = None
_dirty: bool = False
_exec_layer = None


def _reset():
    global _current_path, _meta, _settings, _original_settings, _dirty, _exec_layer
    _current_path = ""
    _meta = None
    _settings = None
    _original_settings = None
    _dirty = False
    _exec_layer = None


def _load(file_path: str) -> bool:
    global _current_path, _meta, _settings, _original_settings, _dirty
    if _current_path == file_path and _settings is not None:
        return True
    _reset()
    _current_path = file_path
    _meta = read_meta_file(file_path)
    _settings = read_audio_import_settings(file_path)
    _original_settings = _settings.copy()
    _dirty = False
    return True


def invalidate():
    """Called when selection changes — forces a reload next render."""
    _reset()


# ---------------------------------------------------------------------------
# Public entry
# ---------------------------------------------------------------------------


def render_audio_asset_inspector(ctx: InfGUIContext, panel, file_path: str):
    """Render the Audio asset Inspector for *file_path*."""
    if not _load(file_path):
        ctx.label("Failed to load audio info")
        ctx.label(file_path)
        return

    global _settings, _original_settings, _dirty, _exec_layer
    _exec_layer = get_asset_execution_layer(_exec_layer, "audio", file_path, AssetAccessMode.READ_ONLY_RESOURCE)

    filename = os.path.basename(file_path)

    # ── Header ─────────────────────────────────────────────────────────
    ctx.label(f"Audio: {filename}")
    ctx.separator()

    # ── Metadata (read-only) ───────────────────────────────────────────
    meta = _meta or {}
    labels = ["GUID", "File Size", "Extension", "Format"]
    lw = max_label_w(ctx, labels)

    field_label(ctx, "GUID", lw)
    ctx.push_style_color(ImGuiCol.Text, 0.55, 0.55, 0.55, 1.0)
    ctx.label(str(meta.get("guid", "(none)")))
    ctx.pop_style_color(1)

    file_size = meta.get("file_size", "")
    if file_size:
        field_label(ctx, "File Size", lw)
        try:
            size_bytes = int(file_size)
            if size_bytes >= 1048576:
                ctx.label(f"{size_bytes / 1048576:.2f} MB")
            elif size_bytes >= 1024:
                ctx.label(f"{size_bytes / 1024:.1f} KB")
            else:
                ctx.label(f"{size_bytes} bytes")
        except (ValueError, TypeError):
            ctx.label(str(file_size))

    ext = meta.get("extension", os.path.splitext(file_path)[1].lower())
    field_label(ctx, "Extension", lw)
    ctx.label(str(ext))

    ctx.separator()

    # ── Import Settings (editable) ─────────────────────────────────────
    ctx.label("Import Settings")

    # Force Mono  (consumed by C++ AudioClip::ApplyImportSettings)
    new_val = ctx.checkbox("Force Mono", _settings.force_mono)
    if new_val != _settings.force_mono:
        _settings.force_mono = new_val

    # ── Apply / Revert buttons ─────────────────────────────────────────
    _dirty = _settings != _original_settings

    render_apply_revert(
        ctx, _dirty,
        on_apply=lambda: _apply(panel, file_path),
        on_revert=lambda: _revert(file_path),
    )


def _apply(panel, file_path: str):
    """Write audio import settings and trigger reimport."""
    global _settings, _original_settings, _dirty, _exec_layer
    if _settings is None:
        return

    _exec_layer = get_asset_execution_layer(_exec_layer, "audio", file_path, AssetAccessMode.READ_ONLY_RESOURCE)
    ok = _exec_layer.apply_import_settings(_settings)
    if not ok:
        return

    _original_settings = _settings.copy()
    _dirty = False


def _revert(file_path: str):
    """Discard edits and reload settings from disk."""
    global _current_path
    _current_path = ""  # force full reload
    _load(file_path)
