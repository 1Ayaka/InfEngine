# MeshRenderer

<div class="class-info">
class in <b>InfEngine.components.builtin</b>
</div>

**Inherits from:** [BuiltinComponent](BuiltinComponent.md)

## Description

Python wrapper for the C++ MeshRenderer component.

Properties delegate to the C++ ``MeshRenderer`` via CppProperty.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## Properties

| Name | Type | Description |
|------|------|------|
| render_material | `` | The material used for rendering (InfMaterial or None). |
| vertex_count | `int` | Number of vertices in inline mesh (0 if using resource mesh). *(read-only)* |
| index_count | `int` | Number of indices in inline mesh (0 if using resource mesh). *(read-only)* |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## Public Methods

| Method | Description |
|------|------|
| `has_render_material() → bool` | Check if a custom material is assigned. |
| `get_effective_material()` | Get the effective material (custom or default). |
| `has_inline_mesh() → bool` | Check if the renderer has inline mesh data. |
| `get_positions() → List[Tuple[float, float, float]]` | Get all vertex positions as (x, y, z) tuples. |
| `get_normals() → List[Tuple[float, float, float]]` | Get all vertex normals as (x, y, z) tuples. |
| `get_uvs() → List[Tuple[float, float]]` | Get all vertex UVs as (u, v) tuples. |
| `get_indices() → List[int]` | Get all indices as a flat list. |
| `serialize() → str` | Serialize MeshRenderer to JSON string (delegates to C++). |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## Example

```python
# TODO: Add example for MeshRenderer
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## See Also

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
