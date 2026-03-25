"""Shared canvas-discovery utilities for the UI system.

Avoids duplicating the recursive canvas-collection logic across
UIEditorPanel and GameViewPanel.
"""

from __future__ import annotations

from operator import attrgetter
import time
from typing import List, Tuple, TYPE_CHECKING

if TYPE_CHECKING:
    pass  # avoid circular imports at runtime

_sort_key = attrgetter('sort_order')

# ── Cached canvas collection ────────────────────────────────────────
# Avoids a full DFS every frame; rebuilt only when scene structure changes.
_canvas_cache: list = []
_canvas_sorted_cache: list = []
_canvas_with_go_cache: list = []
_canvas_cache_scene_id: int = 0
_canvas_cache_version: int = -1
_canvas_cache_rebuild_time: float = 0.0
_EMPTY_CACHE_INITIAL_RETRY_INTERVAL = 0.25
_EMPTY_CACHE_MAX_RETRY_INTERVAL = 8.0
_empty_cache_retry_interval: float = _EMPTY_CACHE_INITIAL_RETRY_INTERVAL


def _rebuild_cache(scene) -> None:
    global _canvas_cache, _canvas_sorted_cache, _canvas_with_go_cache
    global _canvas_cache_scene_id, _canvas_cache_version, _canvas_cache_rebuild_time
    global _empty_cache_retry_interval
    from InfEngine.ui import UICanvas

    result: list = []

    def _walk(go):
        for comp in go.get_py_components():
            if isinstance(comp, UICanvas):
                result.append((go, comp))
        for child in go.get_children():
            _walk(child)

    if scene is not None:
        for root in scene.get_root_objects():
            _walk(root)

    _canvas_with_go_cache = result
    _canvas_cache = [comp for _, comp in result]
    _canvas_sorted_cache = sorted(_canvas_cache, key=_sort_key)
    _canvas_cache_scene_id = id(scene) if scene is not None else 0
    _canvas_cache_version = scene.structure_version if scene is not None else -1
    _canvas_cache_rebuild_time = time.perf_counter()
    if _canvas_cache:
        _empty_cache_retry_interval = _EMPTY_CACHE_INITIAL_RETRY_INTERVAL
    else:
        _empty_cache_retry_interval = min(_EMPTY_CACHE_MAX_RETRY_INTERVAL,
                                          max(_EMPTY_CACHE_INITIAL_RETRY_INTERVAL,
                                              _empty_cache_retry_interval * 2.0))


def _ensure_cache(scene, *, allow_stale_empty: bool = False) -> None:
    global _canvas_cache_scene_id, _canvas_cache_version
    if scene is None:
        return

    scene_id = id(scene)
    ver = scene.structure_version
    if scene_id != _canvas_cache_scene_id:
        _rebuild_cache(scene)
        return
    if allow_stale_empty and not _canvas_cache:
        if (time.perf_counter() - _canvas_cache_rebuild_time) < _empty_cache_retry_interval:
            return
    if ver == _canvas_cache_version:
        return
    _rebuild_cache(scene)


def invalidate_canvas_cache() -> None:
    """Force cache invalidation (e.g. on scene load)."""
    global _canvas_cache_scene_id, _canvas_cache_version, _canvas_cache_rebuild_time
    global _empty_cache_retry_interval
    _canvas_cache_scene_id = 0
    _canvas_cache_version = -1
    _canvas_cache_rebuild_time = 0.0
    _empty_cache_retry_interval = _EMPTY_CACHE_INITIAL_RETRY_INTERVAL


def collect_canvases_with_go(scene) -> List[Tuple]:
    """Return ``[(GameObject, UICanvas), ...]`` for every canvas in *scene*.

    Walks the full scene hierarchy.  Used by UIEditorPanel which needs
    both the owning GameObject and the canvas component.
    """
    if scene is None:
        return []
    _ensure_cache(scene)
    return _canvas_with_go_cache


def collect_canvases(scene, *, allow_stale_empty: bool = False) -> list:
    """Return ``[UICanvas, ...]`` for every canvas in *scene*.

    Lighter variant used by GameViewPanel which only needs the component.
    """
    if scene is None:
        return []
    _ensure_cache(scene, allow_stale_empty=allow_stale_empty)
    return _canvas_cache


def collect_sorted_canvases(scene, *, allow_stale_empty: bool = False) -> list:
    """Return ``[UICanvas, ...]`` sorted by ``sort_order`` (cached)."""
    if scene is None:
        return []
    _ensure_cache(scene, allow_stale_empty=allow_stale_empty)
    return _canvas_sorted_cache
