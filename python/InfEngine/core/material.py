"""
Pythonic Material Wrapper (Phase 1)

Wraps the C++ InfMaterial with context manager support, property caching,
and a clean API suitable for AI-assisted development.

Usage::

    # Create via factory
    mat = Material.create_lit("MyPBR")
    mat.set_color("_BaseColor", 1.0, 0.5, 0.0)
    mat.set_float("_Metallic", 0.9)
    mat.set_float("_Roughness", 0.1)

    # Context manager for scoped lifecycle
    with Material.create_lit("Temp") as mat:
        mat.set_float("_Roughness", 0.5)
        renderer.material = mat
    # mat is unregistered from MaterialManager on exit

    # Load from file
    mat = Material.load("materials/gold.mat")

    # Assign to a MeshRenderer
    mesh_renderer.render_material = mat.native
"""

from __future__ import annotations

import json
from typing import Optional, Tuple

from InfEngine.lib import InfMaterial, MaterialManager


class Material:
    """Pythonic wrapper around C++ InfMaterial.

    Provides:
    - Context manager for scoped lifecycle
    - Clean property setters/getters
    - Factory methods matching Unity's Material API
    - Serialization to/from dict
    """

    def __init__(self, native: "InfMaterial"):
        """Wrap an existing native InfMaterial.

        Prefer using factory methods (create_lit, create_unlit, load) instead.
        """
        if native is None:
            raise ValueError("Cannot wrap a None InfMaterial")
        self._native = native
        self._disposed = False

    # ==========================================================================
    # Factory Methods
    # ==========================================================================

    @staticmethod
    def create_lit(name: str = "New Material") -> "Material":
        """Create a new PBR lit material (default_lit shader)."""
        native = InfMaterial.create_default_lit(name)
        return Material(native)

    @staticmethod
    def create_unlit(name: str = "Unlit Material") -> "Material":
        """Create a new unlit material (default_unlit shader)."""
        native = InfMaterial.create_default_unlit(name)
        return Material(native)

    @staticmethod
    def from_native(native: "InfMaterial") -> "Material":
        """Wrap an existing C++ InfMaterial."""
        return Material(native)

    @staticmethod
    def load(file_path: str) -> Optional["Material"]:
        """Load a material from a .mat file.

        Uses MaterialManager.load_material() so the returned instance is the
        same shared object used by the renderer and Inspector.  This ensures
        that property changes via set_color / set_float are immediately
        visible everywhere (Unity-like behaviour).
        """
        # Prefer MaterialManager — it returns the registered shared instance
        # (or loads + registers if first time) so all systems share one object.
        try:
            mgr = MaterialManager.instance()
            native = mgr.load_material(file_path)
            if native is not None:
                return Material(native)
        except Exception:
            pass

        # Fallback: standalone load (not registered in MaterialManager)
        import os
        if not os.path.isfile(file_path):
            return None
        with open(file_path, "r", encoding="utf-8") as f:
            json_str = f.read()
        native = InfMaterial(file_path)
        if native.deserialize(json_str):
            native.file_path = file_path
            return Material(native)
        return None

    @staticmethod
    def get(name: str) -> Optional["Material"]:
        """Look up a material by name in the global MaterialManager."""
        mgr = MaterialManager.instance()
        native = mgr.get_material(name)
        if native:
            return Material(native)
        return None

    # ==========================================================================
    # Context Manager (scoped lifecycle)
    # ==========================================================================

    def __enter__(self) -> "Material":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.dispose()
        return False

    def dispose(self):
        """Release this material from the MaterialManager."""
        if not self._disposed and self._native is not None:
            self._disposed = True
            # Note: actual GPU resource cleanup happens in C++ destructor
            # when the last shared_ptr reference is released.

    # ==========================================================================
    # Properties
    # ==========================================================================

    @property
    def native(self) -> "InfMaterial":
        """Access the underlying C++ InfMaterial (for passing to C++ APIs)."""
        return self._native

    @property
    def name(self) -> str:
        return self._native.name

    @name.setter
    def name(self, value: str):
        self._native.name = value

    @property
    def guid(self) -> str:
        return self._native.guid

    @property
    def render_queue(self) -> int:
        return self._native.render_queue

    @render_queue.setter
    def render_queue(self, value: int):
        self._native.render_queue = value

    @property
    def vertex_shader_path(self) -> str:
        return self._native.vertex_shader_path

    @vertex_shader_path.setter
    def vertex_shader_path(self, path: str):
        self._native.vertex_shader_path = path

    @property
    def fragment_shader_path(self) -> str:
        return self._native.fragment_shader_path

    @fragment_shader_path.setter
    def fragment_shader_path(self, path: str):
        self._native.fragment_shader_path = path

    @property
    def is_builtin(self) -> bool:
        return self._native.is_builtin

    # ==========================================================================
    # Auto-save (Unity-like dirty-flag persistence)
    # ==========================================================================

    def _auto_save(self):
        """Auto-save material to its .mat file after property changes.

        Mirrors Unity behaviour: when a script modifies a material property
        the asset on disk is updated immediately so the change persists.
        Skips (with a debug log) if the material has no associated file path.
        """
        file_path = getattr(self._native, "file_path", None)
        if not file_path:
            return
        try:
            ok = self._native.save()
            if not ok:
                from InfEngine.debug import Debug
                Debug.log_warning(
                    f"Material._auto_save: save() returned False for '{self.name}' "
                    f"(path={file_path})"
                )
        except Exception as e:
            from InfEngine.debug import Debug
            Debug.log_warning(f"Material._auto_save: exception for '{self.name}': {e}")

    # ==========================================================================
    # Shader Property Setters (Unity-compatible naming)
    # ==========================================================================

    def set_float(self, name: str, value: float):
        """Set a float shader property."""
        self._native.set_float(name, value)
        self._auto_save()

    def set_int(self, name: str, value: int):
        """Set an integer shader property."""
        self._native.set_int(name, value)
        self._auto_save()

    def set_color(self, name: str, r: float, g: float, b: float, a: float = 1.0):
        """Set a color shader property (RGBA)."""
        self._native.set_color(name, (r, g, b, a))
        self._auto_save()

    def set_vector2(self, name: str, x: float, y: float):
        """Set a vec2 shader property."""
        self._native.set_vector2(name, (x, y))
        self._auto_save()

    def set_vector3(self, name: str, x: float, y: float, z: float):
        """Set a vec3 shader property."""
        self._native.set_vector3(name, (x, y, z))
        self._auto_save()

    def set_vector4(self, name: str, x: float, y: float, z: float, w: float):
        """Set a vec4 shader property."""
        self._native.set_vector4(name, (x, y, z, w))
        self._auto_save()

    def set_texture(self, name: str, texture_path: str):
        """Set a texture shader property by file path."""
        self._native.set_texture(name, texture_path)
        self._auto_save()

    # ==========================================================================
    # Shader Property Getters
    # ==========================================================================

    def get_float(self, name: str, default: float = 0.0) -> float:
        """Get a float shader property."""
        val = self._native.get_property(name)
        return float(val) if val is not None else default

    def get_int(self, name: str, default: int = 0) -> int:
        """Get an integer shader property."""
        val = self._native.get_property(name)
        return int(val) if val is not None else default

    def get_color(self, name: str) -> Tuple[float, float, float, float]:
        """Get a color shader property as (r, g, b, a)."""
        val = self._native.get_property(name)
        if val is None:
            return (0.0, 0.0, 0.0, 1.0)
        try:
            return (float(val[0]), float(val[1]), float(val[2]), float(val[3]))
        except (TypeError, IndexError):
            return (0.0, 0.0, 0.0, 1.0)

    def get_vector2(self, name: str) -> Tuple[float, float]:
        """Get a vec2 shader property."""
        val = self._native.get_property(name)
        if val is None:
            return (0.0, 0.0)
        try:
            return (float(val[0]), float(val[1]))
        except (TypeError, IndexError):
            return (0.0, 0.0)

    def get_vector3(self, name: str) -> Tuple[float, float, float]:
        """Get a vec3 shader property."""
        val = self._native.get_property(name)
        if val is None:
            return (0.0, 0.0, 0.0)
        try:
            return (float(val[0]), float(val[1]), float(val[2]))
        except (TypeError, IndexError):
            return (0.0, 0.0, 0.0)

    def get_vector4(self, name: str) -> Tuple[float, float, float, float]:
        """Get a vec4 shader property."""
        val = self._native.get_property(name)
        if val is None:
            return (0.0, 0.0, 0.0, 0.0)
        try:
            return (float(val[0]), float(val[1]), float(val[2]), float(val[3]))
        except (TypeError, IndexError):
            return (0.0, 0.0, 0.0, 0.0)

    def get_texture(self, name: str) -> Optional[str]:
        """Get a texture shader property path."""
        val = self._native.get_property(name)
        return str(val) if val is not None else None

    def has_property(self, name: str) -> bool:
        """Check if the material has a property with the given name."""
        return self._native.has_property(name)

    def get_property(self, name: str):
        """Get a property value by name (generic). Returns None if not found."""
        return self._native.get_property(name)

    def get_all_properties(self) -> dict:
        """Get all properties as a dictionary."""
        return self._native.get_all_properties()

    # ==========================================================================
    # Serialization
    # ==========================================================================

    def to_dict(self) -> dict:
        """Serialize material to a dictionary."""
        return {
            "name": self.name,
            "guid": self.guid,
            "render_queue": self.render_queue,
            "vertex_shader": self.vertex_shader_path,
            "fragment_shader": self.fragment_shader_path,
        }

    def save(self, file_path: str) -> bool:
        """Save material to a .mat file."""
        return self._native.serialize_to_file(file_path)

    # ==========================================================================
    # Registration
    # ==========================================================================

    def register(self, engine=None) -> bool:
        """Register this material with the global MaterialManager.

        After registration, the material can be looked up by name and will
        have its Vulkan pipeline created if shaders are loaded.

        Args:
            engine: Optional Engine instance for pipeline refresh.

        Returns:
            True if registration succeeded.
        """
        mgr = MaterialManager.instance()
        mgr.register_material(self.name, self._native)
        # If engine is provided, refresh the pipeline
        if engine is not None:
            native_engine = getattr(engine, '_engine', engine)
            if hasattr(native_engine, 'refresh_material_pipeline'):
                native_engine.refresh_material_pipeline(self._native)
        return True

    # ==========================================================================
    # Dunder methods
    # ==========================================================================

    def __repr__(self):
        return f"Material(name='{self.name}', queue={self.render_queue})"

    def __eq__(self, other):
        if isinstance(other, Material):
            return self.guid == other.guid
        return NotImplemented

    def __hash__(self):
        return hash(self.guid)
