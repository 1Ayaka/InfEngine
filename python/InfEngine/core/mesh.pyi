"""Type stubs for InfEngine.core.mesh."""

from __future__ import annotations

from typing import List, Tuple


class VertexData:
    """Python-side vertex data matching the C++ Vertex struct layout.

    Fields:
        position: ``(x, y, z)`` local-space position.
        normal: ``(x, y, z)`` surface normal.
        tangent: ``(x, y, z, w)`` tangent with handedness.
        color: ``(r, g, b)`` vertex color.
        uv: ``(u, v)`` primary UV coordinates.
    """
    __slots__: tuple

    position: Tuple[float, float, float]
    normal: Tuple[float, float, float]
    tangent: Tuple[float, float, float, float]
    color: Tuple[float, float, float]
    uv: Tuple[float, float]

    def __init__(
        self,
        position: Tuple[float, float, float] = ...,
        normal: Tuple[float, float, float] = ...,
        tangent: Tuple[float, float, float, float] = ...,
        color: Tuple[float, float, float] = ...,
        uv: Tuple[float, float] = ...,
    ) -> None: ...
    def to_dict(self) -> dict: ...
    def __repr__(self) -> str: ...


class MeshData:
    """Python-side mesh data container.

    Example::

        mesh = MeshData()
        mesh.add_vertex(position=(0, 0, 0), normal=(0, 1, 0), uv=(0, 0))
        mesh.add_vertex(position=(1, 0, 0), normal=(0, 1, 0), uv=(1, 0))
        mesh.add_vertex(position=(0, 0, 1), normal=(0, 1, 0), uv=(0, 1))
        mesh.add_triangle(0, 1, 2)

        # Primitives
        mesh = MeshData.cube()
        mesh = MeshData.plane()
    """

    def __init__(self) -> None: ...

    # Construction
    def add_vertex(
        self,
        position: Tuple[float, float, float],
        normal: Tuple[float, float, float] = ...,
        uv: Tuple[float, float] = ...,
        color: Tuple[float, float, float] = ...,
        tangent: Tuple[float, float, float, float] = ...,
    ) -> int:
        """Add a vertex and return its index."""
        ...
    def add_triangle(self, i0: int, i1: int, i2: int) -> None:
        """Add a triangle by three vertex indices."""
        ...
    def add_quad(self, i0: int, i1: int, i2: int, i3: int) -> None:
        """Add a quad as two triangles."""
        ...
    def clear(self) -> None:
        """Remove all vertices and indices."""
        ...

    # Properties
    @property
    def vertex_count(self) -> int: ...
    @property
    def index_count(self) -> int: ...
    @property
    def triangle_count(self) -> int: ...
    @property
    def vertices(self) -> List[VertexData]: ...
    @property
    def indices(self) -> List[int]: ...

    # Data export
    def get_positions(self) -> List[Tuple[float, float, float]]: ...
    def get_normals(self) -> List[Tuple[float, float, float]]: ...
    def get_uvs(self) -> List[Tuple[float, float]]: ...
    def get_colors(self) -> List[Tuple[float, float, float]]: ...
    def to_numpy_positions(self) -> "numpy.ndarray": ...  # type: ignore[name-defined]
    def to_numpy_normals(self) -> "numpy.ndarray": ...  # type: ignore[name-defined]
    def to_numpy_indices(self) -> "numpy.ndarray": ...  # type: ignore[name-defined]

    # Primitives
    @staticmethod
    def cube() -> MeshData:
        """Create a unit cube mesh centered at origin."""
        ...
    @staticmethod
    def plane(width: float = ..., depth: float = ...) -> MeshData:
        """Create a plane mesh in the XZ plane."""
        ...

    def __repr__(self) -> str: ...
    def __len__(self) -> int: ...
