"""
Null-safe reference wrappers for GameObject and Material.

These wrappers track the validity of references so that accessing a
destroyed/missing object returns ``None`` instead of crashing with a
C++ exception.

``GameObjectRef`` stores a persistent scene-ID and lazily resolves the
live object via ``Scene.find_by_id``.  If the object has been destroyed
the wrapper evaluates to falsy and all attribute access returns ``None``.

``MaterialRef`` stores the asset GUID (from .meta) and lazily resolves
the Material via ``AssetManager.load_by_guid``.  Renaming or moving the
file does not break the reference as long as the .meta is preserved.
"""

from __future__ import annotations

import logging
from typing import Any, Optional, TYPE_CHECKING

if TYPE_CHECKING:
    pass

_log = logging.getLogger("InfEngine.ref")


def _get_asset_database():
    """Return the C++ AssetDatabase, trying AssetManager first then engine."""
    try:
        from InfEngine.core.assets import AssetManager
        if AssetManager._asset_database is not None:
            return AssetManager._asset_database
    except Exception:
        pass
    # Fallback: try the engine singleton directly
    try:
        from InfEngine.engine.play_mode import PlayModeManager
        pm = PlayModeManager.get_instance()
        if pm and pm._asset_database is not None:
            return pm._asset_database
    except Exception:
        pass
    return None


# ============================================================================
# GameObjectRef — Null-safe, persistent-ID based reference
# ============================================================================

class GameObjectRef:
    """Null-safe wrapper around a scene GameObject.

    Stores the persistent ``id`` (uint64, written into the .scene file) and
    lazily resolves the live C++ object each time it is accessed.  If the
    object has been destroyed or the scene was reloaded, the wrapper simply
    returns ``None`` instead of raising a pybind11 segfault.

    Supports truthiness check::

        if self.target:   # False when target is None or destroyed
            self.target.name
    """

    __slots__ = ("_persistent_id", "_cached_obj")

    def __init__(self, game_object=None, *, persistent_id: int = 0):
        if game_object is not None:
            self._persistent_id: int = int(game_object.id)
            self._cached_obj = game_object
        else:
            self._persistent_id = int(persistent_id)
            self._cached_obj = None

    # -- resolution --------------------------------------------------------

    def _resolve(self):
        """Try to resolve the live object from the current scene."""
        if self._persistent_id == 0:
            self._cached_obj = None
            return None
        try:
            from InfEngine.lib import SceneManager as _SM
            scene = _SM.instance().get_active_scene()
            if scene is not None:
                obj = scene.find_by_id(self._persistent_id)
                self._cached_obj = obj
                return obj
        except Exception:
            pass
        self._cached_obj = None
        return None

    # -- public API --------------------------------------------------------

    @property
    def persistent_id(self) -> int:
        """The persistent ID stored in the scene file."""
        return self._persistent_id

    def resolve(self):
        """Return the live GameObject, or ``None`` if destroyed/missing."""
        obj = self._cached_obj
        # Quick validity check: the C++ side exposes `.id`; if it throws
        # the wrapper has been invalidated.
        if obj is not None:
            try:
                _ = obj.id
                return obj
            except Exception:
                self._cached_obj = None
        return self._resolve()

    # -- convenience attribute forwarding ----------------------------------

    def __getattr__(self, name: str) -> Any:
        """Forward attribute access to the underlying GameObject."""
        # Avoid infinite recursion for our own slots
        if name.startswith("_"):
            raise AttributeError(name)
        obj = self.resolve()
        if obj is None:
            return None
        return getattr(obj, name)

    def __bool__(self) -> bool:
        return self.resolve() is not None

    def __eq__(self, other):
        if other is None:
            # Fast path: ID 0 means unset
            if self._persistent_id == 0:
                return True
            # Check cached object without full resolution
            if self._cached_obj is not None:
                try:
                    _ = self._cached_obj.id
                    return False  # alive → not None
                except Exception:
                    self._cached_obj = None
            return self._resolve() is None
        if isinstance(other, GameObjectRef):
            return self._persistent_id == other._persistent_id
        # Compare to raw GameObject
        if hasattr(other, "id"):
            return self._persistent_id == other.id
        return NotImplemented

    def __hash__(self):
        return hash(self._persistent_id)

    def __repr__(self):
        obj = self.resolve()
        if obj is not None:
            return f"GameObjectRef('{obj.name}', id={self._persistent_id})"
        return f"GameObjectRef(None, id={self._persistent_id})"


# ============================================================================
# MaterialRef — Null-safe, GUID-based reference
# ============================================================================

class MaterialRef:
    """Null-safe wrapper around a Material asset.

    Stores the asset GUID (from the ``.meta`` file).  The reference
    survives file moves/renames as long as the ``.meta`` is preserved.

    Lazily loads the Material through ``AssetManager.load_by_guid`` on
    first access and caches the result.

    Supports truthiness check::

        if self.material:   # False when the asset cannot be resolved
            self.material.set_color(...)
    """

    __slots__ = ("_guid", "_cached_mat", "_file_path")

    def __init__(self, material=None, *, guid: str = "", file_path: str = ""):
        if material is not None:
            self._guid: str = self._extract_guid(material)
            self._cached_mat = material
            # Keep file_path as last-resort fallback for resolution
            native = getattr(material, "native", material)
            self._file_path: str = file_path or getattr(native, "file_path", "") or ""
        else:
            self._guid = guid
            self._cached_mat = None
            self._file_path = file_path

    @staticmethod
    def _extract_guid(material) -> str:
        """Extract the GUID for a Material wrapper or native InfMaterial."""
        # Python Material wrapper
        if hasattr(material, "guid") and material.guid:
            return material.guid
        # Try native
        native = getattr(material, "native", material)
        if hasattr(native, "guid") and native.guid:
            return native.guid
        # Fallback: try AssetDatabase lookup by file_path
        file_path = getattr(native, "file_path", "") or ""
        if file_path:
            db = _get_asset_database()
            if db:
                try:
                    g = db.get_guid_from_path(file_path)
                    if g:
                        return g
                except Exception:
                    pass
        return ""

    # -- resolution --------------------------------------------------------

    def _resolve(self):
        """Try to load the Material from its GUID."""
        if not self._guid:
            self._cached_mat = None
            return None
        try:
            from InfEngine.core.assets import AssetManager
            mat = AssetManager.load_by_guid(self._guid)
            if mat is not None:
                self._cached_mat = mat
                return mat
        except Exception:
            pass
        # Fallback: resolve GUID → path via C++ AssetDatabase directly
        db = _get_asset_database()
        if db:
            try:
                path = db.get_path_from_guid(self._guid)
                if path:
                    from InfEngine.core.material import Material
                    mat = Material.load(path)
                    if mat is not None:
                        self._cached_mat = mat
                        return mat
            except Exception:
                pass
        # Last resort: load by file_path directly
        if self._file_path:
            try:
                from InfEngine.core.material import Material
                mat = Material.load(self._file_path)
                if mat is not None:
                    self._cached_mat = mat
                    # Opportunistically resolve GUID for future serialization
                    if not self._guid:
                        g = self._extract_guid(mat)
                        if g:
                            self._guid = g
                    return mat
            except Exception:
                pass
        self._cached_mat = None
        return None

    # -- public API --------------------------------------------------------

    @property
    def guid(self) -> str:
        """The stable GUID from the .meta file."""
        return self._guid

    def resolve(self):
        """Return the loaded Material, or ``None`` if missing."""
        mat = self._cached_mat
        if mat is not None:
            # Validate the cached material is still alive
            try:
                native = getattr(mat, "native", mat)
                _ = native.name  # probe validity
                return mat
            except Exception:
                self._cached_mat = None
        return self._resolve()

    # -- convenience attribute forwarding ----------------------------------

    def __getattr__(self, name: str) -> Any:
        """Forward attribute access to the underlying Material."""
        if name.startswith("_"):
            raise AttributeError(name)
        mat = self.resolve()
        if mat is None:
            return None
        return getattr(mat, name)

    def __bool__(self) -> bool:
        return self.resolve() is not None

    def __eq__(self, other):
        if other is None:
            # Fast path: empty GUID means unset
            if not self._guid and not self._file_path:
                return True
            # Check cached material without full resolution
            if self._cached_mat is not None:
                try:
                    native = getattr(self._cached_mat, "native", self._cached_mat)
                    _ = native.name
                    return False  # alive → not None
                except Exception:
                    self._cached_mat = None
            return self._resolve() is None
        if isinstance(other, MaterialRef):
            return self._guid == other._guid
        return NotImplemented

    def __hash__(self):
        return hash(self._guid)

    def __repr__(self):
        mat = self.resolve()
        if mat is not None:
            return f"MaterialRef('{mat.name}', guid={self._guid[:12]}…)"
        return f"MaterialRef(None, guid={self._guid[:12]}…)"
