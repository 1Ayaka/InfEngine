# Light

<div class="class-info">
类位于 <b>InfEngine.components.builtin</b>
</div>

**继承自:** [BuiltinComponent](BuiltinComponent.md)

## 描述

Python wrapper for the C++ Light component.

Properties delegate to the C++ ``Light`` object via CppProperty.
All changes are immediately reflected in the renderer.

<!-- USER CONTENT START --> description

<!-- USER CONTENT END -->

## 公共方法

| 方法 | 描述 |
|------|------|
| `get_light_view_matrix()` | Get the light's view matrix for shadow mapping. |
| `get_light_projection_matrix(shadow_extent: float = 20.0, near_plane: float = 0.1, far_plane: float = 100.0)` | Get the light's projection matrix for shadow mapping. |
| `serialize() → str` | Serialize Light to JSON string (delegates to C++). |

<!-- USER CONTENT START --> public_methods

<!-- USER CONTENT END -->

## 示例

```python
# TODO: Add example for Light
```

<!-- USER CONTENT START --> example

<!-- USER CONTENT END -->

## 另请参阅

<!-- USER CONTENT START --> see_also

<!-- USER CONTENT END -->
