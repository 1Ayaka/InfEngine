"""Type stubs for InfEngine.core.resource_manager."""

from __future__ import annotations

from typing import Dict, List, Optional

from .material import Material
from .texture import Texture


class CoreResourceManager:
    """Unified resource manager with lifecycle tracking.

    Example::

        rm = CoreResourceManager(engine)
        mat = rm.create_material("Gold", shader="pbr_lit")
        mat.set_float("_Metallic", 1.0)
        tex = rm.load_texture("textures/albedo.png")
        rm.dispose()
    """

    def __init__(self, engine: Optional[object] = ...) -> None: ...

    # Material management
    def create_material(self, name: str, shader: str = ...) -> Material:
        """Create and register a new material.

        Args:
            name: Material name (must be unique).
            shader: ``"lit"`` (default PBR) or ``"unlit"``.
        """
        ...
    def load_material(self, file_path: str) -> Optional[Material]:
        """Load a material from a ``.mat`` file and register it."""
        ...
    def get_material(self, name: str) -> Optional[Material]:
        """Look up a tracked material by name."""
        ...

    # Texture management
    def load_texture(self, file_path: str) -> Optional[Texture]:
        """Load a texture from a file and track it."""
        ...
    def create_solid_texture(
        self,
        name: str,
        width: int,
        height: int,
        r: int = ...,
        g: int = ...,
        b: int = ...,
        a: int = ...,
    ) -> Optional[Texture]:
        """Create a solid color texture and track it."""
        ...

    # Shader management
    def reload_shader(self, shader_id: str) -> bool:
        """Hot-reload a shader and refresh all materials using it."""
        ...
    def is_shader_loaded(self, name: str, shader_type: str = ...) -> bool:
        """Check if a shader module is loaded."""
        ...

    # Lifecycle
    def get_tracked_materials(self) -> List[str]: ...
    def get_tracked_textures(self) -> List[str]: ...
    def dispose(self) -> None:
        """Release all tracked resources."""
        ...

    # Context manager
    def __enter__(self) -> CoreResourceManager: ...
    def __exit__(self, exc_type: object, exc_val: object, exc_tb: object) -> bool: ...
    def __repr__(self) -> str: ...
