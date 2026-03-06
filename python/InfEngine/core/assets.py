"""
Asset Manager — Python-side unified asset loading & caching.

Provides a singleton interface for loading assets by path or GUID,
with WeakRef-based caching to avoid duplicate loads.

Usage::

    from InfEngine.core.assets import AssetManager

    # Load by path
    mat = AssetManager.load("Assets/Materials/gold.mat")

    # Load by GUID
    mat = AssetManager.load_by_guid("a1b2c3d4-e5f6-...")

    # Search
    mats = AssetManager.find_assets("*.mat")
"""

from __future__ import annotations

import fnmatch
import os
import time
import weakref
from typing import Any, Callable, Dict, List, Optional, Type

from InfEngine.core.material import Material
from InfEngine.core.texture import Texture
from InfEngine.core.shader import Shader
from InfEngine.core.audio_clip import AudioClip
from InfEngine.core.asset_types import (
    IMAGE_EXTENSIONS, SHADER_EXTENSIONS, MATERIAL_EXTENSIONS, AUDIO_EXTENSIONS,
    asset_category_from_extension,
)


class AssetManager:
    """Python-side asset loading & caching manager (singleton pattern).

    Integrates with the C++ AssetDatabase for GUID ↔ path resolution
    and caches loaded assets via weak references.
    """

    _instance: Optional["AssetManager"] = None

    # Weak-ref cache: guid → weakref to loaded Python wrapper
    _cache: Dict[str, weakref.ref] = {}

    # Reference to the C++ AssetDatabase (set during engine init)
    _asset_database = None

    # Reference to engine for resource pipeline
    _engine = None

    # Debounced save scheduler: key -> {deadline: float, save_fn: callable}
    _scheduled_saves: Dict[str, Dict[str, Any]] = {}

    # Category -> strategy callables
    _import_apply_handlers: Dict[str, Callable[[str, object], bool]] = {}
    _save_handlers: Dict[str, Callable[[object], object]] = {}
    _execution_strategies_initialized: bool = False

    @classmethod
    def initialize(cls, engine) -> None:
        """Initialize the AssetManager with the engine.

        Called once during engine startup. Sets up the C++ AssetDatabase
        reference for GUID resolution.
        """
        cls._engine = engine
        if hasattr(engine, "get_asset_database"):
            cls._asset_database = engine.get_asset_database()
        elif hasattr(engine, "_engine") and hasattr(engine._engine, "get_asset_database"):
            cls._asset_database = engine._engine.get_asset_database()

    @classmethod
    def load(cls, path: str, asset_type: Optional[Type] = None) -> Optional[Any]:
        """Load an asset by file path.

        Supports: .mat (Material)
        More types will be added as wrappers are implemented.

        Args:
            path: File path to the asset (relative or absolute).
            asset_type: Optional type hint. If None, inferred from extension.

        Returns:
            The loaded asset wrapper, or None if loading failed.
        """
        # Try GUID-based cache first
        guid = cls._get_guid_from_path(path)
        if guid:
            cached = cls._get_cached(guid)
            if cached is not None:
                return cached

        # Infer type from extension if not specified
        ext = os.path.splitext(path)[1].lower()
        resolved_type = asset_type or cls._type_from_extension(ext)

        asset = cls._load_by_type(path, resolved_type)
        if asset is not None and guid:
            cls._put_cache(guid, asset)
        return asset

    @classmethod
    def load_by_guid(cls, guid: str, asset_type: Optional[Type] = None) -> Optional[Any]:
        """Load an asset by its GUID.

        Args:
            guid: The asset GUID string.
            asset_type: Optional type hint.

        Returns:
            The loaded asset wrapper, or None.
        """
        # Check cache
        cached = cls._get_cached(guid)
        if cached is not None:
            return cached

        # Resolve path from GUID
        path = cls._get_path_from_guid(guid)
        if not path:
            return None

        ext = os.path.splitext(path)[1].lower()
        resolved_type = asset_type or cls._type_from_extension(ext)

        asset = cls._load_by_type(path, resolved_type)
        if asset is not None:
            cls._put_cache(guid, asset)
        return asset

    @classmethod
    def find_assets(cls, pattern: str, asset_type: Optional[Type] = None) -> List[str]:
        """Search for asset paths matching a glob pattern.

        Args:
            pattern: Glob pattern (e.g. "*.mat", "Assets/Textures/*.png").
            asset_type: If specified, filter by type.

        Returns:
            List of matching asset paths.
        """
        if not cls._asset_database:
            return []

        results = []
        try:
            guids = cls._asset_database.get_all_guids()
            for guid in guids:
                path = cls._asset_database.get_path_from_guid(guid)
                if path and fnmatch.fnmatch(os.path.basename(path), pattern):
                    if asset_type is not None:
                        ext = os.path.splitext(path)[1].lower()
                        if cls._type_from_extension(ext) != asset_type:
                            continue
                    results.append(path)
        except Exception as e:
            from InfEngine.debug import Debug
            Debug.log_warning(f"find_assets error: {e}")
        return results

    @classmethod
    def invalidate(cls, guid: str) -> None:
        """Invalidate a cached asset (e.g. on file change).

        Args:
            guid: GUID of the asset to invalidate.
        """
        cls._cache.pop(guid, None)

    @classmethod
    def invalidate_path(cls, path: str) -> None:
        """Invalidate a cached asset by path."""
        guid = cls._get_guid_from_path(path)
        if guid:
            cls.invalidate(guid)

    @classmethod
    def flush(cls) -> None:
        """Clear all cached assets."""
        cls._cache.clear()

    # ======================================================================
    # Unified execution APIs (Inspector-facing)
    # ======================================================================

    @classmethod
    def register_import_strategy(cls, asset_category: str, apply_fn: Callable[[str, object], bool]):
        """Register import-settings apply function for an asset category."""
        cls._import_apply_handlers[asset_category] = apply_fn

    @classmethod
    def register_save_strategy(cls, asset_category: str, save_fn: Callable[[object], object]):
        """Register save function for an editable asset category."""
        cls._save_handlers[asset_category] = save_fn

    @classmethod
    def _ensure_execution_strategies(cls):
        if cls._execution_strategies_initialized:
            return

        from InfEngine.core.asset_types import write_texture_import_settings, write_audio_import_settings

        cls.register_import_strategy("texture", write_texture_import_settings)
        cls.register_import_strategy("audio", write_audio_import_settings)
        cls.register_save_strategy("material", lambda resource: resource.save())

        cls._execution_strategies_initialized = True

    @classmethod
    def apply_import_settings(cls, asset_category: str, path: str, settings_obj) -> bool:
        """Apply import settings by category and trigger reimport in one unified step."""
        cls._ensure_execution_strategies()

        apply_fn = cls._import_apply_handlers.get(asset_category)
        if apply_fn is None:
            return False

        ok = apply_fn(path, settings_obj)
        if not ok:
            return False
        cls.reimport_asset(path)
        return True

    @classmethod
    def reimport_asset(cls, path: str) -> bool:
        """Reimport one asset through AssetDatabase."""
        adb = cls._asset_database
        if not adb or not hasattr(adb, "import_asset"):
            return False
        try:
            guid = adb.import_asset(path)
            return bool(guid)
        except Exception:
            return False

    @classmethod
    def move_asset(cls, old_path: str, new_path: str) -> bool:
        """Move asset path in AssetDatabase while preserving mapping/GUID."""
        adb = cls._asset_database
        if not adb or not hasattr(adb, "move_asset"):
            return False
        try:
            return bool(adb.move_asset(old_path, new_path))
        except Exception:
            return False

    @classmethod
    def schedule_save(cls, key: str, save_fn: Callable[[], object], debounce_sec: float = 0.35):
        """Schedule a debounced save callback for a resource key (usually file path)."""
        cls._scheduled_saves[key] = {
            "deadline": time.perf_counter() + max(0.0, float(debounce_sec)),
            "save_fn": save_fn,
        }

    @classmethod
    def schedule_asset_save(cls, asset_category: str, key: str, resource_obj, debounce_sec: float = 0.35):
        """Schedule a debounced save by category strategy, without exposing save callback to caller."""
        cls._ensure_execution_strategies()

        save_handler = cls._save_handlers.get(asset_category)
        if save_handler is None:
            return

        cls.schedule_save(key, lambda: save_handler(resource_obj), debounce_sec=debounce_sec)

    @classmethod
    def flush_scheduled_saves(cls, key: Optional[str] = None):
        """Execute due scheduled saves. If key is given, only flush that key."""
        now = time.perf_counter()

        if key is not None:
            record = cls._scheduled_saves.get(key)
            if not record:
                return
            if now < float(record.get("deadline", 0.0)):
                return
            try:
                save_fn = record.get("save_fn")
                if callable(save_fn):
                    save_fn()
            finally:
                cls._scheduled_saves.pop(key, None)
            return

        due_keys = [k for k, v in cls._scheduled_saves.items() if now >= float(v.get("deadline", 0.0))]
        for k in due_keys:
            record = cls._scheduled_saves.get(k)
            try:
                if record:
                    save_fn = record.get("save_fn")
                    if callable(save_fn):
                        save_fn()
            finally:
                cls._scheduled_saves.pop(k, None)

    # ==========================================================================
    # Internal helpers
    # ==========================================================================

    @classmethod
    def _get_guid_from_path(cls, path: str) -> Optional[str]:
        if not cls._asset_database:
            return None
        try:
            guid = cls._asset_database.get_guid_from_path(path)
            return guid if guid else None
        except Exception as e:
            from InfEngine.debug import Debug
            Debug.log_warning(f"_get_guid_from_path failed for '{path}': {e}")
            return None

    @classmethod
    def _get_path_from_guid(cls, guid: str) -> Optional[str]:
        if not cls._asset_database:
            return None
        try:
            path = cls._asset_database.get_path_from_guid(guid)
            return path if path else None
        except Exception as e:
            from InfEngine.debug import Debug
            Debug.log_warning(f"_get_path_from_guid failed for '{guid}': {e}")
            return None

    @classmethod
    def _get_cached(cls, guid: str) -> Optional[Any]:
        ref = cls._cache.get(guid)
        if ref is not None:
            obj = ref()
            if obj is not None:
                return obj
            # Dead reference — clean up
            del cls._cache[guid]
        return None

    @classmethod
    def _put_cache(cls, guid: str, asset) -> None:
        try:
            cls._cache[guid] = weakref.ref(asset)
        except TypeError:
            # Object doesn't support weakref — skip caching
            pass

    @classmethod
    def _type_from_extension(cls, ext: str) -> Optional[Type]:
        """Map file extension to Python asset type."""
        ext = ext.lower()
        if ext in MATERIAL_EXTENSIONS:
            return Material
        if ext in IMAGE_EXTENSIONS:
            return Texture
        if ext in SHADER_EXTENSIONS:
            return Shader
        if ext in AUDIO_EXTENSIONS:
            return AudioClip
        return None

    @classmethod
    def _load_by_type(cls, path: str, asset_type: Optional[Type]) -> Optional[Any]:
        """Load an asset given its path and resolved type."""
        if asset_type is Material or (asset_type is None and path.endswith(".mat")):
            return Material.load(path)
        if asset_type is Texture:
            return Texture.load(path)
        # Shader is a static utility — return a ShaderAssetInfo descriptor instead
        if asset_type is Shader:
            from InfEngine.core.asset_types import ShaderAssetInfo
            guid = cls._get_guid_from_path(path) or ""
            return ShaderAssetInfo.from_path(path, guid=guid)
        if asset_type is AudioClip:
            return AudioClip.load(path)
        return None
