# MeshData

<div class="class-info">
class in <b>InfEngine.core</b>
</div>

## Description

Python-side mesh data container.

Example::

    mesh = MeshData()
    mesh.add_vertex(position=(0, 0, 0), normal=(0, 1, 0), uv=(0, 0))
    mesh.add_vertex(position=(1, 0, 0), normal=(0, 1, 0), uv=(1, 0))
    mesh.add_vertex(position=(0, 0, 1), normal=(0, 1, 0), uv=(0, 1))
    mesh.add_triangle(0, 1, 2)

    # Primitives
    mesh = MeshData.cube()
    mesh = MeshData.plane()

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## Constructors

| Signature | Description |
|------|------|
| `MeshData.__init__() → None` |  |

<!-- USER CONTENT START --> constructors

<!-- USER CONTENT END -->

## Properties

| Name | Type | Description |
|------|------|------|
| vertex_count | `int` |  *(read-only)* |
| index_count | `int` |  *(read-only)* |
| triangle_count | `int` |  *(read-only)* |
| vertices | `List[VertexData]` |  *(read-only)* |
| indices | `List[int]` |  *(read-only)* |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## Public Methods

| Method | Description |
|------|------|
| `add_vertex(position: Tuple[float, float, float], normal: Tuple[float, float, float] = ..., uv: Tuple[float, float] = ..., color: Tuple[float, float, float] = ..., tangent: Tuple[float, float, float, float] = ...) → int` | Add a vertex and return its index. |
| `add_triangle(i0: int, i1: int, i2: int) → None` | Add a triangle by three vertex indices. |
| `add_quad(i0: int, i1: int, i2: int, i3: int) → None` | Add a quad as two triangles. |
| `clear() → None` | Remove all vertices and indices. |
| `get_positions() → List[Tuple[float, float, float]]` |  |
| `get_normals() → List[Tuple[float, float, float]]` |  |
| `get_uvs() → List[Tuple[float, float]]` |  |
| `get_colors() → List[Tuple[float, float, float]]` |  |
| `to_numpy_positions() → 'numpy.ndarray'` |  |
| `to_numpy_normals() → 'numpy.ndarray'` |  |
| `to_numpy_indices() → 'numpy.ndarray'` |  |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## Static Methods

| Method | Description |
|------|------|
| `static MeshData.cube() → MeshData` | Create a unit cube mesh centered at origin. |
| `static MeshData.plane(width: float = ..., depth: float = ...) → MeshData` | Create a plane mesh in the XZ plane. |

<!-- USER CONTENT START --> static_methods

<!-- USER CONTENT END -->

## Operators

| Method | Returns |
|------|------|
| `__repr__() → str` | `str` |
| `__len__() → int` | `int` |

<!-- USER CONTENT START --> operators

<!-- USER CONTENT END -->

## Example

```python
# TODO: Add example for MeshData
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## See Also

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
