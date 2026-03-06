"""
MeshCollider — Python BuiltinComponent wrapper for C++ MeshCollider.

Uses sibling ``MeshRenderer`` geometry when available. Static and kinematic
bodies use triangle-mesh collision; dynamic rigidbodies automatically use a
convex hull, matching common engine constraints.
"""

from __future__ import annotations

from InfEngine.components.builtin.collider import Collider
from InfEngine.components.builtin_component import CppProperty
from InfEngine.components.serialized_field import FieldType


class MeshCollider(Collider):
    """Python wrapper for the C++ MeshCollider component."""

    _cpp_type_name = "MeshCollider"

    convex = CppProperty(
        "convex",
        FieldType.BOOL,
        default=False,
        tooltip="Use convex hull collision. Dynamic rigidbodies force convex mode.",
    )

    # ------------------------------------------------------------------
    # Gizmos — green wireframe mesh bounds
    # ------------------------------------------------------------------

    def on_draw_gizmos_selected(self):
        """Draw green wireframe of the mesh collider bounds when selected."""
        from InfEngine.gizmos import Gizmos

        transform = self.transform
        if transform is None:
            return

        # Try to get mesh bounds from sibling MeshRenderer
        mr = self.get_component("MeshRenderer")
        if mr is None:
            return

        cpp_mr = getattr(mr, "_cpp_component", None)
        if cpp_mr is None:
            return

        # Compute AABB from mesh positions
        positions = cpp_mr.get_positions()
        if not positions:
            return

        min_x = min_y = min_z = float("inf")
        max_x = max_y = max_z = float("-inf")
        for px, py, pz in positions:
            if px < min_x: min_x = px
            if py < min_y: min_y = py
            if pz < min_z: min_z = pz
            if px > max_x: max_x = px
            if py > max_y: max_y = py
            if pz > max_z: max_z = pz

        cx = (min_x + max_x) * 0.5
        cy = (min_y + max_y) * 0.5
        cz = (min_z + max_z) * 0.5
        sx = max_x - min_x
        sy = max_y - min_y
        sz = max_z - min_z

        old_matrix = Gizmos.matrix
        old_color = Gizmos.color
        Gizmos.matrix = transform.local_to_world_matrix()
        Gizmos.color = (0.53, 1.0, 0.29)
        Gizmos.draw_wire_cube((cx, cy, cz), (sx, sy, sz))
        Gizmos.color = old_color
        Gizmos.matrix = old_matrix
