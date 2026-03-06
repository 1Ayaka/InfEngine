"""Type stubs for InfEngine.components.ref_wrappers."""

from __future__ import annotations

from typing import Any, Optional


class GameObjectRef:
    """Null-safe wrapper around a scene GameObject.

    Stores the persistent ``id`` (uint64, written into the .scene file) and
    lazily resolves the live C++ object each time it is accessed.
    """

    _persistent_id: int
    _cached_obj: Any

    def __init__(
        self, game_object: Any = ..., *, persistent_id: int = ...
    ) -> None: ...

    @property
    def persistent_id(self) -> int:
        """The persistent ID stored in the scene file."""
        ...

    def resolve(self) -> Any:
        """Return the live GameObject, or ``None`` if destroyed/missing."""
        ...

    def __getattr__(self, name: str) -> Any: ...
    def __bool__(self) -> bool: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...
    def __repr__(self) -> str: ...


class MaterialRef:
    """Null-safe wrapper around a Material asset.

    Stores the asset GUID (from the ``.meta`` file).  The reference
    survives file moves/renames as long as the ``.meta`` is preserved.
    """

    _guid: str
    _cached_mat: Any
    _file_path: str

    def __init__(
        self, material: Any = ..., *, guid: str = ..., file_path: str = ...
    ) -> None: ...

    @staticmethod
    def _extract_guid(material: Any) -> str:
        """Extract the GUID for a Material wrapper or native InfMaterial."""
        ...

    @property
    def guid(self) -> str:
        """The stable GUID from the .meta file."""
        ...

    def resolve(self) -> Any:
        """Return the loaded Material, or ``None`` if missing."""
        ...

    def __getattr__(self, name: str) -> Any: ...
    def __bool__(self) -> bool: ...
    def __eq__(self, other: object) -> bool: ...
    def __hash__(self) -> int: ...
    def __repr__(self) -> str: ...
