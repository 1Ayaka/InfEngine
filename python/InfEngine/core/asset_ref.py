"""
Asset reference types for InfComponent serialized fields.

Provides ``TextureRef`` and ``ShaderRef`` — lightweight GUID-based references
that lazily resolve to loaded assets.  They mirror the C++ ``AssetRef<T>``
pattern and integrate with the Inspector field rendering.

Usage in an InfComponent::

    class MyRenderer(InfComponent):
        albedo: TextureRef = serialized_field(
            default=TextureRef(),
            field_type=FieldType.TEXTURE,
        )
        custom_shader: ShaderRef = serialized_field(
            default=ShaderRef(),
            field_type=FieldType.SHADER,
        )
"""

from __future__ import annotations

import os
from typing import Any, Optional


class _AssetRefBase:
    """Base class for GUID-based asset references.

    Stores a GUID string and lazily resolves to the loaded asset via
    ``AssetManager``.
    """

    __slots__ = ("_guid", "_cached", "_path_hint")

    def __init__(self, guid: str = "", path_hint: str = ""):
        self._guid = guid
        self._cached = None
        self._path_hint = path_hint  # optional human-readable path for display

    # ── GUID ───────────────────────────────────────────────────────────

    @property
    def guid(self) -> str:
        return self._guid

    @guid.setter
    def guid(self, value: str):
        if value != self._guid:
            self._guid = value
            self._cached = None

    @property
    def path_hint(self) -> str:
        """Best-effort human-readable path (may be stale)."""
        return self._path_hint

    @path_hint.setter
    def path_hint(self, value: str):
        self._path_hint = value

    # ── Resolution ─────────────────────────────────────────────────────

    def resolve(self):
        """Attempt to resolve the GUID to a loaded asset.

        Returns the asset object, or ``None`` if not found.
        Subclasses override ``_do_resolve``.
        """
        if self._cached is not None:
            return self._cached
        if not self._guid:
            return None
        self._cached = self._do_resolve()
        return self._cached

    def _do_resolve(self):
        """Override in subclass to call the appropriate AssetManager loader."""
        return None

    def invalidate(self):
        """Clear the cached resolved object (GUID is kept)."""
        self._cached = None

    # ── Serialization ──────────────────────────────────────────────────

    def to_dict(self) -> dict:
        return {"guid": self._guid, "path_hint": self._path_hint}

    @classmethod
    def from_dict(cls, d: dict) -> "_AssetRefBase":
        if d is None:
            return cls()
        return cls(guid=d.get("guid", ""), path_hint=d.get("path_hint", ""))

    # ── Display ────────────────────────────────────────────────────────

    @property
    def display_name(self) -> str:
        if self._path_hint:
            return os.path.basename(self._path_hint)
        if self._guid:
            return f"GUID:{self._guid[:8]}…"
        return "None"

    @property
    def is_missing(self) -> bool:
        """True if we have a GUID but resolution failed."""
        if not self._guid:
            return False
        return self.resolve() is None

    def __bool__(self):
        return bool(self._guid)

    def __eq__(self, other):
        if isinstance(other, _AssetRefBase):
            return self._guid == other._guid
        return NotImplemented

    def __hash__(self):
        return hash(self._guid)

    def __repr__(self):
        cls_name = type(self).__name__
        return f"{cls_name}(guid='{self._guid}', path_hint='{self._path_hint}')"


class TextureRef(_AssetRefBase):
    """Reference to a Texture asset."""

    def _do_resolve(self):
        from InfEngine.core.assets import AssetManager
        from InfEngine.core.texture import Texture
        return AssetManager.load_by_guid(self._guid, asset_type=Texture)


class ShaderRef(_AssetRefBase):
    """Reference to a Shader asset (resolves to ShaderAssetInfo)."""

    def _do_resolve(self):
        from InfEngine.core.assets import AssetManager
        from InfEngine.core.shader import Shader
        return AssetManager.load_by_guid(self._guid, asset_type=Shader)


class AudioClipRef(_AssetRefBase):
    """Reference to an AudioClip asset."""

    def _do_resolve(self):
        from InfEngine.core.assets import AssetManager
        from InfEngine.core.audio_clip import AudioClip
        return AssetManager.load_by_guid(self._guid, asset_type=AudioClip)
