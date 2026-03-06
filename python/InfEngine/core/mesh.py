"""
Pythonic Mesh Data Wrapper (Phase 1)

Provides a clean Python API for defining and manipulating mesh data.
Wraps the C++ Vertex struct and mesh data lists for MeshRenderer.

Usage::

    # Create mesh data from vertices and indices
    mesh = MeshData()
    mesh.add_vertex(position=(0, 0, 0), normal=(0, 1, 0), uv=(0, 0))
    mesh.add_vertex(position=(1, 0, 0), normal=(0, 1, 0), uv=(1, 0))
    mesh.add_vertex(position=(0, 0, 1), normal=(0, 1, 0), uv=(0, 1))
    mesh.add_triangle(0, 1, 2)

    # Assign to MeshRenderer
    mesh_renderer.set_mesh(mesh.vertices_raw, mesh.indices_raw)

    # Use primitives
    mesh = MeshData.cube()
    mesh = MeshData.sphere()
    mesh = MeshData.plane()

    # Export for AI/CV (e.g. point cloud)
    positions = mesh.get_positions_as_list()
    normals = mesh.get_normals_as_list()
"""

from __future__ import annotations

from typing import List, Optional, Tuple

from InfEngine.lib import PrimitiveType, Scene, GameObject


class VertexData:
    """Python-side vertex data matching the C++ Vertex struct layout.

    Fields:
        position: (x, y, z) local-space position
        normal: (x, y, z) surface normal
        tangent: (x, y, z, w) tangent with handedness
        color: (r, g, b) vertex color
        uv: (u, v) primary UV coordinates
    """

    __slots__ = ("position", "normal", "tangent", "color", "uv")

    def __init__(
        self,
        position: Tuple[float, float, float] = (0.0, 0.0, 0.0),
        normal: Tuple[float, float, float] = (0.0, 1.0, 0.0),
        tangent: Tuple[float, float, float, float] = (1.0, 0.0, 0.0, 1.0),
        color: Tuple[float, float, float] = (1.0, 1.0, 1.0),
        uv: Tuple[float, float] = (0.0, 0.0),
    ):
        self.position = position
        self.normal = normal
        self.tangent = tangent
        self.color = color
        self.uv = uv

    def to_dict(self) -> dict:
        return {
            "position": list(self.position),
            "normal": list(self.normal),
            "tangent": list(self.tangent),
            "color": list(self.color),
            "uv": list(self.uv),
        }

    def __repr__(self):
        return f"Vertex(pos={self.position}, n={self.normal}, uv={self.uv})"


class MeshData:
    """Python-side mesh data container.

    Stores vertices and indices in a format compatible with the C++
    MeshRenderer.SetMesh() API. Provides utilities for mesh construction,
    primitive generation, and data export for AI/CV pipelines.
    """

    def __init__(self):
        self._vertices: List[VertexData] = []
        self._indices: List[int] = []

    # ==========================================================================
    # Vertex / Index Construction
    # ==========================================================================

    def add_vertex(
        self,
        position: Tuple[float, float, float],
        normal: Tuple[float, float, float] = (0.0, 1.0, 0.0),
        uv: Tuple[float, float] = (0.0, 0.0),
        color: Tuple[float, float, float] = (1.0, 1.0, 1.0),
        tangent: Tuple[float, float, float, float] = (1.0, 0.0, 0.0, 1.0),
    ) -> int:
        """Add a vertex and return its index."""
        self._vertices.append(VertexData(position, normal, tangent, color, uv))
        return len(self._vertices) - 1

    def add_triangle(self, i0: int, i1: int, i2: int):
        """Add a triangle by three vertex indices."""
        self._indices.extend([i0, i1, i2])

    def add_quad(self, i0: int, i1: int, i2: int, i3: int):
        """Add a quad as two triangles (i0-i1-i2, i0-i2-i3)."""
        self._indices.extend([i0, i1, i2, i0, i2, i3])

    def clear(self):
        """Remove all vertices and indices."""
        self._vertices.clear()
        self._indices.clear()

    # ==========================================================================
    # Raw Data Access (for C++ MeshRenderer interop)
    # ==========================================================================

    @property
    def vertex_count(self) -> int:
        return len(self._vertices)

    @property
    def index_count(self) -> int:
        return len(self._indices)

    @property
    def triangle_count(self) -> int:
        return len(self._indices) // 3

    @property
    def vertices(self) -> List[VertexData]:
        """Get the vertex list (Python VertexData objects)."""
        return self._vertices

    @property
    def indices(self) -> List[int]:
        """Get the index list."""
        return list(self._indices)

    # ==========================================================================
    # Data Export (for AI/CV pipelines)
    # ==========================================================================

    def get_positions(self) -> List[Tuple[float, float, float]]:
        """Get all vertex positions as a list of (x, y, z) tuples."""
        return [v.position for v in self._vertices]

    def get_normals(self) -> List[Tuple[float, float, float]]:
        """Get all vertex normals as a list of (x, y, z) tuples."""
        return [v.normal for v in self._vertices]

    def get_uvs(self) -> List[Tuple[float, float]]:
        """Get all vertex UVs as a list of (u, v) tuples."""
        return [v.uv for v in self._vertices]

    def get_colors(self) -> List[Tuple[float, float, float]]:
        """Get all vertex colors as a list of (r, g, b) tuples."""
        return [v.color for v in self._vertices]

    def to_numpy_positions(self):
        """Convert positions to a NumPy array of shape (N, 3).

        Requires NumPy.
        """
        import numpy as np
        return np.array(self.get_positions(), dtype=np.float32)

    def to_numpy_normals(self):
        """Convert normals to a NumPy array of shape (N, 3).

        Requires NumPy.
        """
        import numpy as np
        return np.array(self.get_normals(), dtype=np.float32)

    def to_numpy_indices(self):
        """Convert indices to a NumPy array of shape (M,).

        Requires NumPy.
        """
        import numpy as np
        return np.array(self._indices, dtype=np.uint32)

    # ==========================================================================
    # Primitive Factory Methods
    # ==========================================================================

    @staticmethod
    def cube() -> "MeshData":
        """Create a unit cube mesh centered at origin.

        Uses the engine's built-in cube primitive data.
        """
        mesh = MeshData()
        # Unit cube: 8 corners, 36 indices (12 triangles)
        # Front face
        s = 0.5
        # Define the 24 vertices (4 per face for correct normals)
        faces = [
            # Front (+Z)
            (( s, -s,  s), (0, 0, 1), (1, 0)),
            (( s,  s,  s), (0, 0, 1), (1, 1)),
            ((-s,  s,  s), (0, 0, 1), (0, 1)),
            ((-s, -s,  s), (0, 0, 1), (0, 0)),
            # Back (-Z)
            ((-s, -s, -s), (0, 0, -1), (1, 0)),
            ((-s,  s, -s), (0, 0, -1), (1, 1)),
            (( s,  s, -s), (0, 0, -1), (0, 1)),
            (( s, -s, -s), (0, 0, -1), (0, 0)),
            # Top (+Y)
            ((-s,  s, -s), (0, 1, 0), (0, 0)),
            ((-s,  s,  s), (0, 1, 0), (0, 1)),
            (( s,  s,  s), (0, 1, 0), (1, 1)),
            (( s,  s, -s), (0, 1, 0), (1, 0)),
            # Bottom (-Y)
            ((-s, -s,  s), (0, -1, 0), (0, 0)),
            ((-s, -s, -s), (0, -1, 0), (0, 1)),
            (( s, -s, -s), (0, -1, 0), (1, 1)),
            (( s, -s,  s), (0, -1, 0), (1, 0)),
            # Right (+X)
            (( s, -s, -s), (1, 0, 0), (1, 0)),
            (( s,  s, -s), (1, 0, 0), (1, 1)),
            (( s,  s,  s), (1, 0, 0), (0, 1)),
            (( s, -s,  s), (1, 0, 0), (0, 0)),
            # Left (-X)
            ((-s, -s,  s), (-1, 0, 0), (1, 0)),
            ((-s,  s,  s), (-1, 0, 0), (1, 1)),
            ((-s,  s, -s), (-1, 0, 0), (0, 1)),
            ((-s, -s, -s), (-1, 0, 0), (0, 0)),
        ]
        for pos, norm, uv in faces:
            mesh.add_vertex(position=pos, normal=norm, uv=uv)
        # 6 faces × 2 triangles
        for face in range(6):
            base = face * 4
            mesh.add_triangle(base, base + 1, base + 2)
            mesh.add_triangle(base, base + 2, base + 3)
        return mesh

    @staticmethod
    def plane(width: float = 1.0, depth: float = 1.0) -> "MeshData":
        """Create a plane mesh in the XZ plane, centered at origin."""
        mesh = MeshData()
        hw, hd = width / 2, depth / 2
        mesh.add_vertex((-hw, 0, -hd), (0, 1, 0), (0, 0))
        mesh.add_vertex((-hw, 0,  hd), (0, 1, 0), (0, 1))
        mesh.add_vertex(( hw, 0,  hd), (0, 1, 0), (1, 1))
        mesh.add_vertex(( hw, 0, -hd), (0, 1, 0), (1, 0))
        mesh.add_quad(0, 1, 2, 3)
        return mesh

    # ==========================================================================
    # Dunder methods
    # ==========================================================================

    def __repr__(self):
        return (
            f"MeshData(vertices={self.vertex_count}, "
            f"indices={self.index_count}, "
            f"triangles={self.triangle_count})"
        )

    def __len__(self):
        return self.vertex_count
