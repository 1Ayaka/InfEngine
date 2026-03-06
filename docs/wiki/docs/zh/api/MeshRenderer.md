# MeshRenderer

<div class="class-info">
类位于 <b>InfEngine.components.builtin</b>
</div>

**继承自:** [BuiltinComponent](BuiltinComponent.md)

## 描述

Python wrapper for the C++ MeshRenderer component.

Properties delegate to the C++ ``MeshRenderer`` via CppProperty.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 属性

| 名称 | 类型 | 描述 |
|------|------|------|
| render_material | `` | The material used for rendering (InfMaterial or None). |
| vertex_count | `int` | Number of vertices in inline mesh (0 if using resource mesh). *(只读)* |
| index_count | `int` | Number of indices in inline mesh (0 if using resource mesh). *(只读)* |

<!-- USER CONTENT START --> properties

<!-- USER CONTENT END -->

## 公共方法

| 方法 | 描述 |
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

## 示例

```python
# TODO: Add example for MeshRenderer
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
