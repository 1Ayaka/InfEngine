"""
Core Resource Manager (Phase 1)

Unified resource lifecycle management for the InfEngine core module.
Acts as the single entry point for creating, loading, and tracking all
engine resources (Materials, Textures, Meshes, Shaders).

Design: C++ owns the actual GPU memory; Python holds shared_ptr references.
When Python releases its reference, the C++ shared_ptr ref count decreases.
GPU resources are freed when the last C++ reference is released.

Usage::

    rm = CoreResourceManager(engine)

    # Materials
    mat = rm.create_material("Gold", shader="pbr_lit")
    mat.set_float("_Metallic", 1.0)
    mat.set_color("_BaseColor", 1.0, 0.843, 0.0)

    # Textures
    tex = rm.load_texture("textures/albedo.png")

    # Cleanup
    rm.dispose()  # releases all tracked resources
"""

from __future__ import annotations

from typing import Dict, List, Optional

from .material import Material
from .texture import Texture
from .shader import Shader


class CoreResourceManager:
    """Unified resource manager providing lifecycle tracking.

    Tracks all resources created through it, enabling bulk cleanup
    and leak detection. Integrates with the C++ MaterialManager
    and FileManager singletons.
    """

    def __init__(self, engine=None):
        """Initialize the resource manager.

        Args:
            engine: The Engine or native InfEngine instance.
                    Used for shader operations and pipeline refresh.
        """
        self._engine = engine
        self._materials: Dict[str, Material] = {}
        self._textures: Dict[str, Texture] = {}
        self._disposed = False

        # Bind the Shader utility class to the engine
        if engine is not None:
            native = getattr(engine, '_engine', engine)
            Shader._set_engine(native)

    # ==========================================================================
    # Material Management
    # ==========================================================================

    def create_material(self, name: str, shader: str = "lit") -> Material:
        """Create and register a new material.

        Args:
            name: Material name (must be unique).
            shader: Shader preset — "lit" (default PBR) or "unlit".

        Returns:
            A Pythonic Material wrapper.
        """
        if shader == "unlit":
            mat = Material.create_unlit(name)
        else:
            mat = Material.create_lit(name)

        mat.register(self._engine)
        self._materials[name] = mat
        return mat

    def load_material(self, file_path: str) -> Optional[Material]:
        """Load a material from a .mat file and register it.

        Returns:
            The loaded Material, or None if the file could not be parsed.
        """
        mat = Material.load(file_path)
        if mat is not None:
            mat.register(self._engine)
            self._materials[mat.name] = mat
        return mat

    def get_material(self, name: str) -> Optional[Material]:
        """Look up a tracked material by name."""
        return self._materials.get(name) or Material.get(name)

    # ==========================================================================
    # Texture Management
    # ==========================================================================

    def load_texture(self, file_path: str) -> Optional[Texture]:
        """Load a texture from a file and track it.

        Args:
            file_path: Path to image file (PNG, JPG, BMP, TGA).

        Returns:
            A Pythonic Texture wrapper, or None on failure.
        """
        tex = Texture.load(file_path)
        if tex is not None:
            self._textures[file_path] = tex
        return tex

    def create_solid_texture(self, name: str, width: int, height: int,
                             r: int = 255, g: int = 255, b: int = 255,
                             a: int = 255) -> Optional[Texture]:
        """Create a solid color texture and track it."""
        tex = Texture.solid_color(width, height, r, g, b, a)
        if tex is not None:
            self._textures[name] = tex
        return tex

    # ==========================================================================
    # Shader Management (delegates to Shader static class)
    # ==========================================================================

    def reload_shader(self, shader_id: str) -> bool:
        """Hot-reload a shader and refresh all materials using it."""
        return Shader.reload(shader_id)

    def is_shader_loaded(self, name: str, shader_type: str = "vertex") -> bool:
        """Check if a shader module is loaded."""
        return Shader.is_loaded(name, shader_type)

    # ==========================================================================
    # Lifecycle
    # ==========================================================================

    def get_tracked_materials(self) -> List[str]:
        """Get names of all tracked materials."""
        return list(self._materials.keys())

    def get_tracked_textures(self) -> List[str]:
        """Get keys of all tracked textures."""
        return list(self._textures.keys())

    def dispose(self):
        """Release all tracked resources.

        After calling dispose(), this manager should not be used again.
        The underlying GPU resources will be freed when the C++ engine
        releases its last shared_ptr reference.
        """
        if self._disposed:
            return

        for mat in self._materials.values():
            mat.dispose()
        self._materials.clear()
        self._textures.clear()

        self._disposed = True

    # ==========================================================================
    # Context Manager
    # ==========================================================================

    def __enter__(self) -> "CoreResourceManager":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.dispose()
        return False

    def __repr__(self):
        return (
            f"CoreResourceManager(materials={len(self._materials)}, "
            f"textures={len(self._textures)})"
        )
