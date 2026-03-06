"""
MeshRenderer — Python InfComponent wrapper for the C++ MeshRenderer component.

Exposes shadow settings and material access as CppProperty descriptors.
Mesh data access (vertices, normals, UVs, indices) is provided via
delegate methods.

Example::

    from InfEngine.components.builtin import MeshRenderer

    class MyShadowToggle(InfComponent):
        def start(self):
            mr = self.get_component(MeshRenderer)
            mr.casts_shadows = False
"""

from __future__ import annotations

from typing import Any, List, Optional, Tuple

from InfEngine.components.builtin_component import BuiltinComponent, CppProperty
from InfEngine.components.serialized_field import FieldType


class MeshRenderer(BuiltinComponent):
    """Python wrapper for the C++ MeshRenderer component.

    Properties delegate to the C++ ``MeshRenderer`` via CppProperty.
    """

    _cpp_type_name = "MeshRenderer"
    _component_category_ = "Rendering"

    # ---- Shadow settings ----
    casts_shadows = CppProperty(
        "casts_shadows",
        FieldType.BOOL,
        default=True,
        tooltip="Whether this renderer casts shadows",
    )
    receives_shadows = CppProperty(
        "receives_shadows",
        FieldType.BOOL,
        default=True,
        tooltip="Whether this renderer receives shadows",
    )

    # ------------------------------------------------------------------
    # Material (non-CppProperty — complex reference type)
    # ------------------------------------------------------------------

    @property
    def render_material(self):
        """The material used for rendering (InfMaterial or None)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.render_material
        return None

    @render_material.setter
    def render_material(self, value) -> None:
        cpp = self._cpp_component
        if cpp is not None:
            cpp.render_material = value

    def has_render_material(self) -> bool:
        """Check if a custom material is assigned."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.has_render_material()
        return False

    def get_effective_material(self):
        """Get the effective material (custom or default)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.get_effective_material()
        return None

    # ------------------------------------------------------------------
    # Mesh data access (read-only, for AI / CV / inspection)
    # ------------------------------------------------------------------

    def has_inline_mesh(self) -> bool:
        """Check if the renderer has inline mesh data."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.has_inline_mesh()
        return False

    @property
    def vertex_count(self) -> int:
        """Number of vertices in inline mesh (0 if using resource mesh)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.vertex_count
        return 0

    @property
    def index_count(self) -> int:
        """Number of indices in inline mesh (0 if using resource mesh)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.index_count
        return 0

    def get_positions(self) -> List[Tuple[float, float, float]]:
        """Get all vertex positions as (x, y, z) tuples."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.get_positions()
        return []

    def get_normals(self) -> List[Tuple[float, float, float]]:
        """Get all vertex normals as (x, y, z) tuples."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.get_normals()
        return []

    def get_uvs(self) -> List[Tuple[float, float]]:
        """Get all vertex UVs as (u, v) tuples."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.get_uvs()
        return []

    def get_indices(self) -> List[int]:
        """Get all indices as a flat list."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.get_indices()
        return []

    def serialize(self) -> str:
        """Serialize MeshRenderer to JSON string (delegates to C++)."""
        cpp = self._cpp_component
        if cpp is not None:
            return cpp.serialize()
        return "{}"
